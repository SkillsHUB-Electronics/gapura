// BLE link (NimBLE): Nordic-UART-style service, passkey pairing, one client.
// Writes to CMD are reassembled up to '\n' and queued for loop(); responses and
// events are notified on RESP in chunks that fit the negotiated MTU.
#pragma once

#include <Arduino.h>

#include <deque>
#include <mutex>
#include <string>

#include "router.h"
#include "store.h"

class NimBLECharacteristic;

class Ble {
 public:
  void begin(Router& router, Store& store);
  void loop();

  bool connected() const { return connected_; }
  bool secured() const { return secured_; }

  // Called from NimBLE callbacks (host task).
  void handleConnect(uint16_t conn, uint16_t mtu);
  void handleDisconnect();
  void handleSecured(bool ok);
  void handleWrite(const std::string& chunk);

 private:
  void send(const std::string& msg);

  Router* router_ = nullptr;
  NimBLECharacteristic* resp_ = nullptr;
  volatile bool connected_ = false;
  volatile bool secured_ = false;
  volatile uint16_t mtu_ = 23;

  std::string rx_;  // host task only
  std::mutex mutex_;
  std::deque<std::string> lines_;
};
