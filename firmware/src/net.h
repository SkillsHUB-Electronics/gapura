// Wi-Fi link: STA with setup-AP fallback, mDNS, REST /api/cmd, WebSocket /ws,
// dashboard from LittleFS. Requests arrive on the AsyncTCP task and are
// queued; loop() runs them through the router so RFID access stays on one task.
#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include <deque>
#include <mutex>
#include <string>

#include "led.h"
#include "router.h"
#include "store.h"

class Net {
 public:
  enum class Mode : uint8_t { kOff, kConnecting, kConnected, kSetupAp };

  Net();
  void begin(Router& router, Store& store, Led& led);
  void loop();

  Mode mode() const { return mode_; }
  bool connected() const { return mode_ == Mode::kConnected; }
  String ip() const;
  int rssi() const;
  size_t clients() const { return ws_.count(); }

  // Non-blocking scan: call until `scanning` is false, then `networks` holds the
  // results (strongest first, one entry per SSID).
  void wifiScan(JsonObject out);
  // Starts joining a network; the result is read with wifiStatus(). Saved on success.
  bool wifiConnect(const String& ssid, const String& pass);
  void wifiStatus(JsonObject out) const;
  bool rebootRequested() const { return reboot_; }

 private:
  struct Job {
    std::string line;
    AsyncWebServerRequestPtr http;  // set for REST
    uint32_t wsClient = 0;          // set for WebSocket
  };

  void handleAttempt(uint32_t now);
  void onConnected();
  void startSta();
  void startAp();
  void setupRoutes();
  bool authorized(AsyncWebServerRequest* req) const;
  void enqueue(Job job);
  void runJobs();

  AsyncWebServer server_;
  AsyncWebSocket ws_;
  Router* router_ = nullptr;
  Store* store_ = nullptr;
  Led* led_ = nullptr;

  Mode mode_ = Mode::kOff;
  uint32_t modeSinceMs_ = 0;
  bool mdns_ = false;
  bool everConnected_ = false;
  bool reboot_ = false;

  // wifiConnect() in progress
  bool attempt_ = false;
  uint32_t attemptMs_ = 0;
  String attemptSsid_, attemptPass_, prevSsid_, prevPass_;
  String lastError_;
  bool scanReconnect_ = false;  // a scan dropped the station link; rejoin when it ends

  std::mutex jobsMutex_;
  std::deque<Job> jobs_;
};
