#include "ble.h"

#include <NimBLEDevice.h>

namespace {

constexpr const char* kService = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
constexpr const char* kCmd = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
constexpr const char* kResp = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";
constexpr size_t kMaxChunk = 180;
constexpr size_t kMaxLine = 1024;
constexpr size_t kMaxQueued = 8;

class ServerCallbacks : public NimBLEServerCallbacks {
 public:
  explicit ServerCallbacks(Ble& ble) : ble_(ble) {}
  void onConnect(NimBLEServer*, NimBLEConnInfo& info) override {
    ble_.handleConnect(info.getConnHandle(), info.getMTU());
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override {
    ble_.handleDisconnect();
  }
  void onMTUChange(uint16_t mtu, NimBLEConnInfo& info) override {
    ble_.handleConnect(info.getConnHandle(), mtu);
  }
  void onAuthenticationComplete(NimBLEConnInfo& info) override {
    ble_.handleSecured(info.isEncrypted() && info.isAuthenticated());
  }

 private:
  Ble& ble_;
};

class CmdCallbacks : public NimBLECharacteristicCallbacks {
 public:
  explicit CmdCallbacks(Ble& ble) : ble_(ble) {}
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
    ble_.handleWrite(c->getValue());
  }

 private:
  Ble& ble_;
};

}  // namespace

void Ble::begin(Router& router, Store& store) {
  router_ = &router;
  const Config& c = store.config();

  NimBLEDevice::init(c.name.c_str());
  NimBLEDevice::setMTU(kMaxChunk + 3);
  // Bonding + MITM + secure connections, the phone asks for the passkey.
  NimBLEDevice::setSecurityAuth(true, true, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
  NimBLEDevice::setSecurityPasskey(c.blePasskey);

  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks(*this));
  server->advertiseOnDisconnect(true);

  NimBLEService* service = server->createService(kService);
  NimBLECharacteristic* cmd = service->createCharacteristic(
      kCmd, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR |
                NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN);
  cmd->setCallbacks(new CmdCallbacks(*this));
  resp_ = service->createCharacteristic(
      kResp, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ |
                 NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::READ_AUTHEN);
  service->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(kService);
  // flags + 128-bit UUID fill most of the 31-byte packet; name goes in the scan response
  NimBLEAdvertisementData scan;
  scan.setName(c.name.c_str());
  adv->setScanResponseData(scan);
  adv->start();

  // Events only reach a paired, encrypted client.
  router.addSink([this](const std::string& msg) {
    if (secured_) send(msg);
  });
}

void Ble::handleConnect(uint16_t, uint16_t mtu) {
  connected_ = true;
  mtu_ = mtu;
}

void Ble::handleDisconnect() {
  connected_ = false;
  secured_ = false;
  rx_.clear();
}

void Ble::handleSecured(bool ok) { secured_ = ok; }

void Ble::handleWrite(const std::string& chunk) {
  for (char ch : chunk) {
    if (ch == '\r') continue;
    if (ch != '\n') {
      if (rx_.size() < kMaxLine) rx_ += ch;
      continue;
    }
    if (!rx_.empty()) {
      std::lock_guard<std::mutex> lock(mutex_);
      if (lines_.size() < kMaxQueued) lines_.push_back(rx_);
    }
    rx_.clear();
  }
}

void Ble::loop() {
  while (true) {
    std::string line;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (lines_.empty()) return;
      line = std::move(lines_.front());
      lines_.pop_front();
    }
    send(router_->handle(line));
  }
}

void Ble::send(const std::string& msg) {
  if (!connected_ || !resp_) return;
  const std::string framed = msg + '\n';
  const size_t chunk = std::min<size_t>(kMaxChunk, mtu_ > 3 ? mtu_ - 3 : 20);
  for (size_t pos = 0; pos < framed.size(); pos += chunk) {
    const size_t n = std::min(chunk, framed.size() - pos);
    resp_->notify(reinterpret_cast<const uint8_t*>(framed.data() + pos), n);
  }
}
