// Pushes each tag to a server (HTTP/HTTPS POST, JSON, Bearer token) from its own
// task, so a slow server never blocks the RFID loop. Fixed readers need no phone.
#pragma once

#include <Arduino.h>

#include <string>

#include "store.h"

class Webhook {
 public:
  void begin(Store& store);
  bool enabled() const;
  // Queues a JSON body; dropped if the queue is full or no URL is set.
  void post(const std::string& body);
  struct Result {
    int status;      // HTTP code, negative = transport error
    uint32_t ms;     // round trip
    char body[104];  // start of the reply, printable only
  };
  // True once per finished POST; call from loop() to publish the result.
  bool takeResult(Result& out);
  // 0 = nothing sent yet, > 0 = HTTP status, < 0 = transport error.
  int lastStatus() const { return lastStatus_; }

 private:
  static void task(void* self);
  void run();

  Store* store_ = nullptr;
  void* queue_ = nullptr;   // QueueHandle_t of Item
  void* results_ = nullptr; // QueueHandle_t of Result
  volatile int lastStatus_ = 0;
};
