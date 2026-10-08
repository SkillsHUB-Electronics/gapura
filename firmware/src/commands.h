// Binds the protocol commands (docs/PROTOCOL.md) to the device modules.
#pragma once

#include "ble.h"
#include "buzzer.h"
#include "led.h"
#include "net.h"
#include "power.h"
#include "rfid.h"
#include "router.h"
#include "store.h"
#include "webhook.h"

struct Device {
  Rfid& rfid;
  Power& power;
  Store& store;
  Led& led;
  Net& net;
  Ble& ble;
  Webhook& webhook;
  Buzzer& buzzer;
};

void registerCommands(Router& router, Device& dev);
void batteryToJson(const BatteryInfo& b, JsonObject out);
// Set by the `reboot` command; main restarts after the response is sent.
bool rebootRequested();
// Set by the `sleep` command; main enters deep sleep after the response is sent.
bool sleepRequested();
