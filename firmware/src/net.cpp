#include "net.h"

#include <AsyncJson.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <Update.h>
#include <WiFi.h>

#include <algorithm>
#include <vector>

#include "config.h"

namespace {

constexpr size_t kMaxJobs = 8;

const char kSetupPage[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>RFID Setup</title>
<style>
:root{--bg:#f6f7f9;--card:#fff;--text:#111827;--muted:#6b7280;--accent:#2563eb;--border:#e5e7eb}
@media(prefers-color-scheme:dark){:root{--bg:#0b0f17;--card:#141a24;--text:#e5e7eb;--muted:#9ca3af;--border:#243042}}
*{box-sizing:border-box}body{margin:0;min-height:100vh;display:grid;place-items:center;background:var(--bg);
color:var(--text);font:16px/1.5 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;padding:16px}
form{width:100%;max-width:360px;background:var(--card);border:1px solid var(--border);border-radius:16px;padding:24px;
box-shadow:0 8px 24px rgba(0,0,0,.06)}h1{font-size:20px;margin:0 0 4px}p{color:var(--muted);margin:0 0 20px;font-size:14px}
label{display:block;font-size:13px;color:var(--muted);margin:12px 0 4px}
input{width:100%;padding:10px 12px;border:1px solid var(--border);border-radius:10px;background:transparent;color:inherit;font:inherit}
button{margin-top:20px;width:100%;padding:12px;border:0;border-radius:10px;background:var(--accent);color:#fff;font:600 15px system-ui}
</style></head><body>
<form method="post" action="/setup">
<h1>Wi-Fi setup</h1><p>Connect the Gapura reader to your network. You are on its own hotspot: this page and the dashboard are at <b>192.168.4.1</b>. The name <b>gapura.local</b> works only after the reader has joined your network.</p>
<button type="button" id="scan" style="margin:0 0 8px">Scan for networks</button>
<select id="nets" style="display:none;width:100%;padding:10px 12px;border:1px solid var(--border);border-radius:10px;background:transparent;color:inherit;font:inherit"></select>
<label for="s">Network name (SSID)</label><input id="s" name="ssid" required maxlength="32">
<label for="p">Password</label><input id="p" name="pass" type="password" maxlength="64">
<button>Save and restart</button>
</form>
<script>
const $=id=>document.getElementById(id),nets=$('nets');
$('scan').onclick=async()=>{const b=$('scan');b.textContent='Scanning…';
 for(let i=0;i<12;i++){try{const r=await(await fetch('/setup/scan')).json();
  if(!r.scanning){nets.replaceChildren(new Option('Choose a network',''),...r.networks.map(n=>new Option(n.ssid+(n.secure?' 🔒':'')+'  '+n.rssi+' dBm, '+n.band+' GHz',n.ssid)));
   nets.style.display='block';b.textContent='Scan again';return}}catch(e){}
  await new Promise(r=>setTimeout(r,1000))}
 b.textContent='Scan for networks'};
nets.onchange=()=>{if(nets.value)$('s').value=nets.value};
</script></body></html>)HTML";

const char kNoDashboard[] PROGMEM =
    "Gapura reader is running. Dashboard not installed: "
    "run `npm run build` in dashboard/ and `pio run -t uploadfs` in firmware/.";

std::string busyResponse(const std::string& line) {
  JsonDocument req;
  deserializeJson(req, line);
  JsonDocument res;
  res["id"] = req["id"];
  res["ok"] = false;
  res["error"]["code"] = errCode(Err::kBusy);
  res["error"]["msg"] = errMsg(Err::kBusy);
  std::string out;
  serializeJson(res, out);
  return out;
}

String htmlEscape(const String& s) {
  String out;
  for (char c : s) {
    switch (c) {
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '&': out += "&amp;"; break;
      case '"': out += "&quot;"; break;
      default: out += c;
    }
  }
  return out;
}

}  // namespace

Net::Net() : server_(80), ws_("/ws") {}

void Net::begin(Router& router, Store& store, Led& led) {
  router_ = &router;
  store_ = &store;
  led_ = &led;

  LittleFS.begin(true);
  WiFi.setHostname(store.config().name.c_str());
  WiFi.setAutoReconnect(true);

  router.addSink([this](const std::string& msg) {
    if (ws_.count()) ws_.textAll(msg.c_str(), msg.size());
  });

  setupRoutes();
  if (store.config().wifiSsid.isEmpty()) {
    startAp();
  } else {
    startSta();
  }
  server_.begin();
}

void Net::startSta() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(store_->config().wifiSsid.c_str(), store_->config().wifiPass.c_str());
  mode_ = Mode::kConnecting;
  modeSinceMs_ = millis();
}

void Net::startAp() {
  const String& name = store_->config().name;  // "Gapura-XXXX"
  const String ssid = "Gapura-Setup-" + name.substring(name.length() - 4);
  if (WiFi.getMode() != WIFI_OFF) WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid.c_str(), cfg::kDefaultPassword);
  mode_ = Mode::kSetupAp;
  modeSinceMs_ = millis();
}

bool Net::authorized(AsyncWebServerRequest* req) const {
  String given;
  if (req->hasHeader("Authorization")) {
    given = req->header("Authorization");
    if (!given.startsWith("Bearer ")) return false;
    given = given.substring(7);
  } else if (req->hasParam("token")) {
    given = req->getParam("token")->value();
  }
  const String& token = store_->config().token;
  if (given.length() != token.length()) return false;
  uint8_t diff = 0;  // constant time
  for (size_t i = 0; i < token.length(); i++) diff |= given[i] ^ token[i];
  return diff == 0;
}

void Net::setupRoutes() {
  // Dashboard dev server (Vite on localhost) calls the device directly.
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Authorization, Content-Type");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");

  server_.on("/api/cmd", HTTP_POST, [this](AsyncWebServerRequest* req, JsonVariant& json) {
    if (!authorized(req)) {
      req->send(401, "application/json",
                R"({"id":null,"ok":false,"error":{"code":"UNAUTHORIZED","msg":"Missing or invalid token"}})");
      return;
    }
    Job job;
    serializeJson(json, job.line);
    std::lock_guard<std::mutex> lock(jobsMutex_);
    if (jobs_.size() >= kMaxJobs) {
      req->send(503, "application/json", busyResponse(job.line).c_str());
      return;
    }
    job.http = req->pause();
    jobs_.push_back(std::move(job));
  });

  ws_.handleHandshake([this](AsyncWebServerRequest* req) { return authorized(req); });
  ws_.onEvent([this](AsyncWebSocket*, AsyncWebSocketClient* client, AwsEventType type,
                     void* arg, uint8_t* data, size_t len) {
    if (type != WS_EVT_DATA) return;
    auto* info = static_cast<AwsFrameInfo*>(arg);
    // Only single-frame text messages; commands are small.
    if (!info->final || info->index != 0 || info->len != len || info->opcode != WS_TEXT) return;
    Job job;
    job.line.assign(reinterpret_cast<char*>(data), len);
    job.wsClient = client->id();
    std::lock_guard<std::mutex> lock(jobsMutex_);
    if (jobs_.size() >= kMaxJobs) {
      client->text(busyResponse(job.line).c_str());
      return;
    }
    jobs_.push_back(std::move(job));
  });
  server_.addHandler(&ws_);

  // OTA: multipart upload of firmware.bin, or littlefs.bin with ?target=fs.
  server_.on(
      "/api/ota", HTTP_POST,
      [this](AsyncWebServerRequest* req) {
        if (!authorized(req)) {
          return req->send(401, "application/json",
                           R"({"ok":false,"error":{"code":"UNAUTHORIZED","msg":"Missing or invalid token"}})");
        }
        if (Update.hasError() || !Update.isFinished()) {
          return req->send(500, "application/json",
                           R"({"ok":false,"error":{"code":"WRITE_FAILED","msg":"Update failed"}})");
        }
        req->send(200, "application/json", R"({"ok":true})");
        reboot_ = true;
      },
      [this](AsyncWebServerRequest* req, const String&, size_t index, uint8_t* data,
             size_t len, bool final) {
        if (!authorized(req)) return;
        if (index == 0) {
          const bool fs = req->hasParam("target") && req->getParam("target")->value() == "fs";
          if (fs) LittleFS.end();
          Update.begin(UPDATE_SIZE_UNKNOWN, fs ? U_SPIFFS : U_FLASH);
        }
        if (Update.isRunning() && !Update.hasError() && Update.write(data, len) != len) {
          Update.abort();
        }
        if (final && Update.isRunning()) Update.end(true);
      });

  server_.on("/setup", HTTP_GET, [this](AsyncWebServerRequest* req) {
    if (mode_ != Mode::kSetupAp) return req->send(404);
    req->send(200, "text/html", kSetupPage);
  });
  server_.on("/setup", HTTP_POST, [this](AsyncWebServerRequest* req) {
    if (mode_ != Mode::kSetupAp) return req->send(404);
    if (!req->hasParam("ssid", true)) return req->send(400, "text/plain", "ssid required");
    Config& c = store_->config();
    c.wifiSsid = req->getParam("ssid", true)->value();
    c.wifiPass = req->hasParam("pass", true) ? req->getParam("pass", true)->value() : "";
    store_->save();
    req->send(200, "text/html",
              "<!doctype html><meta name=viewport content='width=device-width'>"
              "<body style='font:16px system-ui;padding:24px'><h2>Saved</h2>"
              "<p>The reader restarts and joins <b>" + htmlEscape(c.wifiSsid) + "</b>.</p>"
              "<p>Open <b>http://gapura.local</b> and sign in with the device password "
              "(default <code>" + String(cfg::kDefaultPassword) + "</code>). Change it in Settings.</p></body>");
    reboot_ = true;
  });

  // Setup hotspot only: lets the setup page list nearby networks (open, local).
  server_.on("/setup/scan", HTTP_GET, [this](AsyncWebServerRequest* req) {
    if (mode_ != Mode::kSetupAp) return req->send(404);
    JsonDocument doc;
    wifiScan(doc.to<JsonObject>());
    String body;
    serializeJson(doc, body);
    req->send(200, "application/json", body);
  });

  server_.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  server_.onNotFound([this](AsyncWebServerRequest* req) {
    if (req->method() == HTTP_OPTIONS) return req->send(204);
    if (mode_ == Mode::kSetupAp) return req->redirect("/setup");
    req->send(200, "text/plain", kNoDashboard);
  });
}

void Net::wifiScan(JsonObject out) {
  const int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) {
    out["scanning"] = true;
    return;
  }
  if (n >= 0) {
    // Strongest first, one entry per SSID, hidden networks skipped.
    std::vector<int> order;
    for (int i = 0; i < n; i++) {
      if (WiFi.SSID(i).length()) order.push_back(i);
    }
    std::sort(order.begin(), order.end(), [](int a, int b) { return WiFi.RSSI(a) > WiFi.RSSI(b); });
    JsonArray list = out["networks"].to<JsonArray>();
    std::vector<String> seen;
    for (int i : order) {
      const String ssid = WiFi.SSID(i);
      if (std::find(seen.begin(), seen.end(), ssid) != seen.end() || list.size() >= 20) continue;
      seen.push_back(ssid);
      JsonObject o = list.add<JsonObject>();
      o["ssid"] = ssid;
      o["rssi"] = WiFi.RSSI(i);
      o["channel"] = WiFi.channel(i);
      o["band"] = WiFi.channel(i) > 14 ? "5" : "2.4";
      o["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    }
    WiFi.scanDelete();
    out["scanning"] = false;
    if (scanReconnect_) {  // rejoin the network the scan interrupted
      scanReconnect_ = false;
      WiFi.setAutoReconnect(true);
      if (!attempt_) WiFi.reconnect();
    }
    return;
  }
  // No scan yet: scanning needs the station interface (keep the setup AP up).
  if (WiFi.getMode() == WIFI_AP) WiFi.mode(WIFI_AP_STA);
  else if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
  // Hopping across both bands while joined drops the link and returns nothing, so
  // leave the network first (the scan takes about 12 s) and rejoin afterwards.
  if (!attempt_ && WiFi.status() == WL_CONNECTED) {
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false);
    scanReconnect_ = true;
    delay(100);
  }
  WiFi.scanNetworks(true, true);  // async, also hidden SSIDs (skipped below)
  out["scanning"] = true;
}

bool Net::wifiConnect(const String& ssid, const String& pass) {
  if (ssid.isEmpty() || ssid.length() > 32 || pass.length() > 63) return false;
  if (!pass.isEmpty() && pass.length() < 8) return false;  // WPA needs 8 to 63
  if (attempt_) return false;
  prevSsid_ = store_->config().wifiSsid;
  prevPass_ = store_->config().wifiPass;
  attemptSsid_ = ssid;
  attemptPass_ = pass;
  lastError_ = "";
  attempt_ = true;
  attemptMs_ = millis();
  if (WiFi.getMode() == WIFI_AP) WiFi.mode(WIFI_AP_STA);  // keep the setup AP while trying
  else if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
  scanReconnect_ = false;
  WiFi.setAutoReconnect(false);  // do not fight the old network while joining the new one
  WiFi.disconnect(false);
  WiFi.begin(ssid.c_str(), pass.c_str());
  return true;
}

void Net::wifiStatus(JsonObject out) const {
  const char* state = attempt_ ? "connecting"
                      : !lastError_.isEmpty() ? "failed"
                      : connected() ? "connected" : "idle";
  out["state"] = state;
  out["ssid"] = attempt_ ? attemptSsid_ : (connected() ? WiFi.SSID() : String(""));
  out["ip"] = ip();
  out["rssi"] = rssi();
  out["error"] = lastError_;
}

void Net::onConnected() {
  mode_ = Mode::kConnected;
  everConnected_ = true;
  if (!mdns_ && MDNS.begin(cfg::kMdnsHost)) {
    MDNS.addService("http", "tcp", 80);
    mdns_ = true;
  }
}

void Net::handleAttempt(uint32_t now) {
  const wl_status_t st = WiFi.status();
  if (st == WL_CONNECTED && WiFi.SSID() == attemptSsid_) {
    Config& c = store_->config();
    c.wifiSsid = attemptSsid_;
    c.wifiPass = attemptPass_;
    store_->save();
    attempt_ = false;
    WiFi.setAutoReconnect(true);
    if (mode_ == Mode::kSetupAp) {  // leave the setup hotspot
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
    }
    onConnected();
    return;
  }
  const uint32_t waited = now - attemptMs_;
  const char* error = nullptr;
  if (st == WL_CONNECT_FAILED && waited > 2000) error = "Wrong password";
  else if (st == WL_NO_SSID_AVAIL && waited > 8000) error = "Network not found";
  else if (waited > cfg::kWifiConnectTimeoutMs) error = "Could not connect";
  if (!error) return;
  attempt_ = false;
  lastError_ = error;
  WiFi.setAutoReconnect(true);
  if (mode_ != Mode::kSetupAp && !prevSsid_.isEmpty()) {  // go back to the previous network
    WiFi.mode(WIFI_OFF);  // a full driver reset is what makes the old network rejoin reliably
    delay(100);
    WiFi.mode(WIFI_STA);
    WiFi.begin(prevSsid_.c_str(), prevPass_.c_str());
    mode_ = Mode::kConnecting;
    modeSinceMs_ = now;
  }
}

void Net::loop() {
  const uint32_t now = millis();
  if (attempt_) handleAttempt(now);
  switch (mode_) {
    case Mode::kConnecting:
      if (WiFi.status() == WL_CONNECTED) {
        mode_ = Mode::kConnected;
        everConnected_ = true;
        if (!mdns_ && MDNS.begin(cfg::kMdnsHost)) {
          MDNS.addService("http", "tcp", 80);
          mdns_ = true;
        }
      } else if (!everConnected_ && now - modeSinceMs_ > cfg::kWifiConnectTimeoutMs) {
        startAp();  // never connected since boot: offer setup
      }
      break;
    case Mode::kConnected:
      if (WiFi.status() != WL_CONNECTED) {
        mode_ = Mode::kConnecting;  // auto-reconnect keeps trying
        modeSinceMs_ = now;
      }
      break;
    default:
      break;
  }

  led_->set(Led::kSetupAp, mode_ == Mode::kSetupAp);
  led_->set(Led::kWifiConnecting, mode_ == Mode::kConnecting);

  runJobs();
  ws_.cleanupClients();
}

void Net::runJobs() {
  while (true) {
    Job job;
    {
      std::lock_guard<std::mutex> lock(jobsMutex_);
      if (jobs_.empty()) return;
      job = std::move(jobs_.front());
      jobs_.pop_front();
    }
    const std::string res = router_->handle(job.line);
    if (job.wsClient) {
      ws_.text(job.wsClient, res.c_str(), res.size());
    } else if (auto req = job.http.lock()) {
      req->send(200, "application/json", res.c_str());
    }
  }
}

String Net::ip() const {
  if (mode_ == Mode::kSetupAp) return WiFi.softAPIP().toString();
  return connected() ? WiFi.localIP().toString() : String();
}

int Net::rssi() const { return connected() ? WiFi.RSSI() : 0; }
