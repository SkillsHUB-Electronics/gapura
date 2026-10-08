#include "keyboard.h"

#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

namespace {

constexpr uint32_t kKeyGapMs = 12;
constexpr uint8_t kShift = 0x02;
constexpr uint8_t kEnter = 0x28;

// Boot-protocol style keyboard, report id 1: modifiers, reserved, 6 keys.
uint8_t kReportMap[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
    0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,
    0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,
    0xC0};

class ServerCallbacks : public NimBLEServerCallbacks {
 public:
  explicit ServerCallbacks(BleKeyboard& kb) : kb_(kb) {}
  void onConnect(NimBLEServer*, NimBLEConnInfo&) override { kb_.handleConnect(); }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override { kb_.handleDisconnect(); }
  void onAuthenticationComplete(NimBLEConnInfo& info) override {
    kb_.handleSecured(info.isEncrypted() && info.isAuthenticated());
  }

 private:
  BleKeyboard& kb_;
};

}  // namespace

void BleKeyboard::begin(Store& store) {
  const Config& c = store.config();
  NimBLEDevice::init(c.name.c_str());
  NimBLEDevice::setSecurityAuth(true, true, true);  // bonding + MITM + secure connections
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);  // the phone asks for the passkey
  NimBLEDevice::setSecurityPasskey(c.blePasskey);

  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks(*this));
  server->advertiseOnDisconnect(true);

  auto* hid = new NimBLEHIDDevice(server);
  hid->setManufacturer("Gapura");
  hid->setPnp(0x02, 0x303A, 0x0001, 0x0100);
  hid->setHidInfo(0x00, 0x01);
  hid->setReportMap(kReportMap, sizeof(kReportMap));
  hid->setBatteryLevel(100);
  input_ = hid->getInputReport(1);

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->setAppearance(0x03C1);  // keyboard
  adv->addServiceUUID(hid->getHidService()->getUUID());
  NimBLEAdvertisementData scan;
  scan.setName(c.name.c_str());
  adv->setScanResponseData(scan);
  adv->start();
}

bool BleKeyboard::toKey(char c, Key& out) {
  if (c >= 'a' && c <= 'z') out = {0, static_cast<uint8_t>(0x04 + (c - 'a'))};
  else if (c >= 'A' && c <= 'Z') out = {kShift, static_cast<uint8_t>(0x04 + (c - 'A'))};
  else if (c >= '1' && c <= '9') out = {0, static_cast<uint8_t>(0x1E + (c - '1'))};
  else if (c == '0') out = {0, 0x27};
  else if (c == ' ') out = {0, 0x2C};
  else if (c == '-') out = {0, 0x2D};
  else if (c == '_') out = {kShift, 0x2D};
  else if (c == '.') out = {0, 0x37};
  else if (c == ',') out = {0, 0x36};
  else if (c == '/') out = {0, 0x38};
  else if (c == '@') out = {kShift, 0x1F};
  else return false;
  return true;
}

void BleKeyboard::type(const std::string& text, bool enter) {
  if (!secured_) return;  // nobody paired: do not queue stale input
  for (char ch : text) {
    Key k;
    if (toKey(ch, k)) queue_.push_back(k);
  }
  if (enter) queue_.push_back({0, kEnter});
}

void BleKeyboard::sendReport(uint8_t mod, uint8_t code) {
  if (!input_ || !secured_) return;
  const uint8_t report[8] = {mod, 0, code, 0, 0, 0, 0, 0};
  input_->setValue(report, sizeof(report));
  input_->notify();
}

// Alternates key-down and key-up so repeated characters ("11") register.
void BleKeyboard::loop() {
  if (queue_.empty() && !keyDown_) return;
  if (!secured_) {
    queue_.clear();
    keyDown_ = false;
    return;
  }
  if (millis() - lastMs_ < kKeyGapMs) return;
  lastMs_ = millis();
  if (keyDown_) {
    sendReport(0, 0);
    keyDown_ = false;
    return;
  }
  const Key k = queue_.front();
  queue_.pop_front();
  sendReport(k.mod, k.code);
  keyDown_ = true;
}
