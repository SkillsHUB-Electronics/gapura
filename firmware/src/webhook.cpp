#include "webhook.h"

#include <HTTPClient.h>
#include <NetworkClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <esp_heap_caps.h>
#include <freertos/queue.h>
#include <mbedtls/platform.h>
#include <time.h>

namespace {
constexpr size_t kMaxBody = 384;
constexpr uint8_t kQueueDepth = 4;
constexpr uint32_t kTimeoutMs = 4000;

struct Item {
  char body[kMaxBody];
};

// The prebuilt SDK keeps every mbedTLS allocation in internal RAM, and with BLE
// on that is too small for a TLS handshake (record buffers of 16 KB, about
// 40 KB in all). Large blocks go to PSRAM instead; small ones stay internal
// for speed. Internal RAM is the fallback (no PSRAM, or PSRAM full).
constexpr size_t kTlsPsramMin = 1024;

void* tlsCalloc(size_t n, size_t size) {
  void* p = nullptr;
  if (n * size >= kTlsPsramMin) p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  return p;
}

void tlsFree(void* p) { heap_caps_free(p); }
}  // namespace

void Webhook::begin(Store& store) {
  store_ = &store;
  if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0) mbedtls_platform_set_calloc_free(tlsCalloc, tlsFree);
  queue_ = xQueueCreate(kQueueDepth, sizeof(Item));
  results_ = xQueueCreate(kQueueDepth, sizeof(Result));
  xTaskCreate(&Webhook::task, "webhook", 8192, this, 1, nullptr);
}

bool Webhook::enabled() const { return store_ && !store_->config().webhookUrl.isEmpty(); }

void Webhook::post(const std::string& body) {
  if (!enabled() || !queue_ || body.size() >= kMaxBody) return;
  Item item;
  memcpy(item.body, body.c_str(), body.size() + 1);
  xQueueSend(static_cast<QueueHandle_t>(queue_), &item, 0);  // full: drop the newest
}

bool Webhook::takeResult(Result& out) {
  return results_ && xQueueReceive(static_cast<QueueHandle_t>(results_), &out, 0) == pdTRUE;
}

void Webhook::task(void* self) { static_cast<Webhook*>(self)->run(); }

void Webhook::run() {
  bool timeSet = false;
  for (;;) {
    Item item;
    if (xQueueReceive(static_cast<QueueHandle_t>(queue_), &item, portMAX_DELAY) != pdTRUE) continue;
    const Config& c = store_->config();
    Result res{};
    auto finish = [&](int status, const String& body) {
      res.status = status;
      lastStatus_ = status;
      size_t n = 0;
      for (size_t i = 0; i < body.length() && n < sizeof(res.body) - 1; i++) {
        const char ch = body[i];
        res.body[n++] = (ch >= 0x20 && ch < 0x7F) ? ch : ' ';
      }
      res.body[n] = 0;
      xQueueSend(static_cast<QueueHandle_t>(results_), &res, 0);
    };
    if (c.webhookUrl.isEmpty() || WiFi.status() != WL_CONNECTED) {
      finish(-1, "no Wi-Fi or URL");
      continue;
    }
    const bool https = c.webhookUrl.startsWith("https://");
    if (https && !timeSet) {  // certificate validation needs the clock
      configTzTime("UTC0", "pool.ntp.org", "time.google.com");
      for (int i = 0; i < 20 && time(nullptr) < 1700000000; i++) vTaskDelay(pdMS_TO_TICKS(250));
      timeSet = time(nullptr) >= 1700000000;
    }

    HTTPClient http;
    http.setTimeout(kTimeoutMs);
    http.setConnectTimeout(kTimeoutMs);
    NetworkClient plain;
    NetworkClientSecure secure;
    bool ok;
    if (https) {
      secure.useBuiltinCACertBundle();
      ok = http.begin(secure, c.webhookUrl);
    } else {
      ok = http.begin(plain, c.webhookUrl);
    }
    if (!ok) {
      finish(-1, "bad URL");
      continue;
    }
    http.addHeader("Content-Type", "application/json");
    if (!c.webhookToken.isEmpty()) http.addHeader("Authorization", "Bearer " + c.webhookToken);
    const uint32_t t0 = millis();
    const int code = http.POST(reinterpret_cast<uint8_t*>(item.body), strlen(item.body));
    res.ms = millis() - t0;
    String reply;
    if (code > 0 && http.getSize() <= 2048) reply = http.getString();
    else if (code <= 0) {
      // Say why: the TLS stack's own error, the clock and the largest free block
      // (a handshake needs about 40 KB in one piece).
      reply = HTTPClient::errorToString(code);
      char tls[80];
      if (https && secure.lastError(tls, sizeof(tls)) != 0) reply += String(" [") + tls + "]";
      reply += String(" heap ") + ESP.getMaxAllocHeap() + (time(nullptr) < 1700000000 ? " no-clock" : "");
    }
    http.end();
    finish(code, reply);
  }
}
