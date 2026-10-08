#include "router.h"

const char* errCode(Err e) {
  switch (e) {
    case Err::kOk: return "OK";
    case Err::kNoCard: return "NO_CARD";
    case Err::kAuthFailed: return "AUTH_FAILED";
    case Err::kReadFailed: return "READ_FAILED";
    case Err::kWriteFailed: return "WRITE_FAILED";
    case Err::kForbiddenBlock: return "FORBIDDEN_BLOCK";
    case Err::kUnsupportedCard: return "UNSUPPORTED_CARD";
    case Err::kBadArgs: return "BAD_ARGS";
    case Err::kUnauthorized: return "UNAUTHORIZED";
    case Err::kBusy: return "BUSY";
    case Err::kUnknownCmd: return "UNKNOWN_CMD";
    case Err::kLowBattery: return "LOW_BATTERY";
    case Err::kBadSignature: return "BAD_SIGNATURE";
    case Err::kNoReader: return "NO_READER";
  }
  return "UNKNOWN";
}

const char* errMsg(Err e) {
  switch (e) {
    case Err::kOk: return "OK";
    case Err::kNoCard: return "No card in the field";
    case Err::kAuthFailed: return "Authentication failed";
    case Err::kReadFailed: return "Read failed";
    case Err::kWriteFailed: return "Write failed";
    case Err::kForbiddenBlock: return "Block 0 and sector trailers are write-protected";
    case Err::kUnsupportedCard: return "Card type not supported for this command";
    case Err::kBadArgs: return "Invalid or missing arguments";
    case Err::kUnauthorized: return "Missing or invalid token";
    case Err::kBusy: return "Another RFID operation is running";
    case Err::kUnknownCmd: return "Unknown command";
    case Err::kLowBattery: return "Battery critically low";
    case Err::kBadSignature: return "Card signature does not match";
    case Err::kNoReader: return "No 13.56 MHz reader in this reader mode";
  }
  return "Unknown error";
}

void Router::on(const char* cmd, Handler h) {
  handlers_.emplace_back(cmd, std::move(h));
}

std::string Router::handle(const std::string& line) {
  JsonDocument req;
  JsonDocument res;
  Err err = Err::kOk;

  if (deserializeJson(req, line) || !req.is<JsonObject>()) {
    res["id"] = nullptr;
    err = Err::kBadArgs;
  } else {
    res["id"] = req["id"];
    const char* cmd = req["cmd"];
    if (!cmd) {
      err = Err::kBadArgs;
    } else {
      err = Err::kUnknownCmd;
      for (auto& [name, handler] : handlers_) {
        if (name == cmd) {
          JsonDocument data;
          JsonObject obj = data.to<JsonObject>();
          err = handler(req["args"].as<JsonObjectConst>(), obj);
          if (err == Err::kOk && obj.size()) res["data"] = data;
          break;
        }
      }
    }
  }

  res["ok"] = err == Err::kOk;
  if (err != Err::kOk) {
    res["error"]["code"] = errCode(err);
    res["error"]["msg"] = errMsg(err);
  }
  if (onResult_) onResult_(err);

  std::string out;
  serializeJson(res, out);
  return out;
}

void Router::publish(const char* event, JsonVariantConst data) {
  if (sinks_.empty()) return;
  JsonDocument doc;
  doc["event"] = event;
  doc["data"] = data;
  std::string out;
  serializeJson(doc, out);
  for (auto& sink : sinks_) sink(out);
}
