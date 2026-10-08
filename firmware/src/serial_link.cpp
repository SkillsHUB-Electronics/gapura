#include "serial_link.h"

#include <Arduino.h>

void SerialLink::begin(Router& router) {
  router_ = &router;
  line_.reserve(kMaxLine);
  router.addSink([](const std::string& msg) {
    Serial.write(msg.data(), msg.size());
    Serial.write('\n');
  });
}

void SerialLink::loop() {
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') continue;
    if (c != '\n') {
      if (line_.size() < kMaxLine) {
        line_ += c;
      } else {
        overflow_ = true;
      }
      continue;
    }
    if (overflow_) {
      line_ = "{}";  // answered as BAD_ARGS by the router
      overflow_ = false;
    }
    if (!line_.empty()) {
      const std::string res = router_->handle(line_);
      Serial.write(res.data(), res.size());
      Serial.write('\n');
    }
    line_.clear();
  }
}
