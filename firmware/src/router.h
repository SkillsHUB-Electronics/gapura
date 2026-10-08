// JSON command router and event bus shared by every link (USB, Wi-Fi, BLE).
// Contract: docs/PROTOCOL.md. No Arduino dependency (unit-tested natively).
#pragma once

#include <ArduinoJson.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

enum class Err : uint8_t {
  kOk,
  kNoCard,
  kAuthFailed,
  kReadFailed,
  kWriteFailed,
  kForbiddenBlock,
  kUnsupportedCard,
  kBadArgs,
  kUnauthorized,
  kBusy,
  kUnknownCmd,
  kLowBattery,
  kBadSignature,
  kNoReader,
};

const char* errCode(Err e);
const char* errMsg(Err e);

class Router {
 public:
  // Fills `data` on success; returns the error code otherwise.
  using Handler = std::function<Err(JsonObjectConst args, JsonObject data)>;
  using Sink = std::function<void(const std::string& line)>;

  void on(const char* cmd, Handler h);
  // One request line in, one response line out (no trailing newline).
  std::string handle(const std::string& line);

  void addSink(Sink s) { sinks_.push_back(std::move(s)); }
  // Serialises {"event":…, "data":…} once and sends it to every sink.
  void publish(const char* event, JsonVariantConst data);

  void onResult(std::function<void(Err)> cb) { onResult_ = std::move(cb); }

 private:
  std::vector<std::pair<std::string, Handler>> handlers_;
  std::vector<Sink> sinks_;
  std::function<void(Err)> onResult_;
};
