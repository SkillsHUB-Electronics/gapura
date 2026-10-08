// 13.56 MHz reader chips behind one small interface, so the MIFARE Classic
// logic in rfid.cpp (blocks, card security) works with an RC522 or a PN532.
// Both sit on the same FSPI pins (config.h); only one is fitted at a time.
#pragma once

#include <Arduino.h>
#include <MFRC522.h>

struct HfCard {
  uint8_t uid[10];
  uint8_t uidLen = 0;
  uint8_t sak = 0;
};

class HfReader {
 public:
  virtual ~HfReader() = default;
  // Starts the chip. False if it does not answer (wiring or power problem).
  virtual bool begin() = 0;
  virtual const char* chip() const = 0;  // "RC522" / "PN532"
  virtual uint8_t version() const = 0;   // RC522 VersionReg, PN532 firmware version
  // Wakes (halted cards too) and selects one ISO 14443A card.
  virtual bool select(HfCard& card) = 0;
  // MIFARE Classic Crypto1 authentication of the sector holding `block`.
  virtual bool auth(uint8_t block, bool keyB, const uint8_t key[6], const HfCard& card) = 0;
  virtual bool read(uint8_t block, uint8_t out[16]) = 0;
  virtual bool write(uint8_t block, const uint8_t in[16]) = 0;
  // Ends the card session (halt, stop crypto).
  virtual void halt() = 0;
  // RF field on/off, for time slots shared with the 125 kHz reader.
  virtual void field(bool on) = 0;
  virtual void powerDown() = 0;
};

class Rc522Reader : public HfReader {
 public:
  Rc522Reader();
  bool begin() override;
  const char* chip() const override { return "RC522"; }
  uint8_t version() const override { return version_; }
  bool select(HfCard& card) override;
  bool auth(uint8_t block, bool keyB, const uint8_t key[6], const HfCard& card) override;
  bool read(uint8_t block, uint8_t out[16]) override;
  bool write(uint8_t block, const uint8_t in[16]) override;
  void halt() override;
  void field(bool on) override;
  void powerDown() override;

 private:
  MFRC522 mfrc_;
  uint8_t version_ = 0;
};

// Own PN532 SPI framing (normal information frames, LSB first): the stock
// libraries hide the SAK and the RF field commands that this firmware needs.
class Pn532Reader : public HfReader {
 public:
  bool begin() override;
  const char* chip() const override { return "PN532"; }
  uint8_t version() const override { return version_; }
  bool select(HfCard& card) override;
  bool auth(uint8_t block, bool keyB, const uint8_t key[6], const HfCard& card) override;
  bool read(uint8_t block, uint8_t out[16]) override;
  bool write(uint8_t block, const uint8_t in[16]) override;
  void halt() override;
  void field(bool on) override;
  void powerDown() override;

 private:
  // Sends `cmd` + params, waits for the ACK and the reply; `resp` gets the
  // reply data after the command code. Returns the data length, or -1.
  int command(uint8_t cmd, const uint8_t* params, uint8_t len, uint8_t* resp, uint8_t respMax,
              uint32_t timeoutMs = 100);
  // InDataExchange with target 1; true when the card status byte is 0.
  bool exchange(const uint8_t* data, uint8_t len, uint8_t* resp = nullptr, uint8_t respLen = 0);
  void writeFrame(uint8_t cmd, const uint8_t* params, uint8_t len);
  bool waitReady(uint32_t timeoutMs);
  bool readAck();
  int readFrame(uint8_t cmd, uint8_t* resp, uint8_t respMax);
  void begin_();
  void end_();

  uint8_t version_ = 0;
  bool listed_ = false;
};
