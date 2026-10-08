// Card readers: polling with tag detected / removed callbacks, and MIFARE
// Classic block operations used by the command router. The 13.56 MHz chip
// (RC522 or PN532, hf.h) and the optional RDM6300 (125 kHz) are picked by
// `reader.mode`; combined modes alternate time slots, one antenna on at a time.
#pragma once

#include <Arduino.h>

#include <functional>
#include <string>

#include "hf.h"
#include "rdm6300.h"
#include "router.h"
#include "util.h"

struct TagInfo {
  String uid;   // "DE AD BE EF"
  String type;  // "MIFARE 1KB", "EM4100"
  const char* reader = "rc522";  // "rc522", "pn532" or "rdm6300"
  bool lf = false;  // 125 kHz tag: UID only, no blocks or credential
};

struct MifareKey {
  uint8_t bytes[6];
  bool typeB;
};

using Block = uint8_t[mifare::kBlockSize];

class Rfid {
 public:
  using TagHandler = std::function<void(const TagInfo&)>;

  // Starts the readers of `mode`. Returns false if the 13.56 MHz chip does
  // not answer (the RDM6300 cannot be probed; it only sends frames).
  bool begin(reader::Mode mode);
  // Call every loop; polls the 13.56 MHz reader at cfg::kRfidPollMs.
  void loop();

  reader::Mode mode() const { return mode_; }
  // 13.56 MHz chip: "RC522" / "PN532", or nullptr in RDM6300-only mode.
  const char* hfChip() const { return hf_ ? hf_->chip() : nullptr; }
  bool hfReady() const { return hf_ && hfReady_; }
  bool hasLf() const { return lf_ != nullptr; }
  uint8_t version() const { return hf_ ? hf_->version() : 0; }
  // Every fitted reader that can be checked is answering.
  bool ready() const { return hf_ ? hfReady_ : true; }
  // Readers off before deep sleep.
  void powerDown();

  void onTag(TagHandler h) { onTag_ = std::move(h); }
  void onRemoved(TagHandler h) { onRemoved_ = std::move(h); }

  // Tag events are only raised while scanning (default on).
  void setScanning(bool on) { scanning_ = on; }
  bool scanning() const { return scanning_; }

  // Blocks until a card (either frequency) is present or the timeout expires.
  bool waitUid(uint32_t timeoutMs, TagInfo& out);
  Err readBlock(uint8_t block, const MifareKey& key, Block out);
  // MIFARE commands return kNoReader without a 13.56 MHz reader.
  // Refuses block 0 and sector trailers.
  Err writeBlock(uint8_t block, const MifareKey& key, const Block in);
  Err readSector(uint8_t sector, const MifareKey& key, Block out[4]);

  // Secured cards (cardsec.h): per-card keys, signed credential.
  // Credential text (up to 95 chars) in blocks 4-6 and 8-10, signed in block 12.
  // Blank sectors are keyed with `factoryKey`, locked ones need the derived key B.
  Err writeCredential(const uint8_t secret[16], const uint8_t factoryKey[6], const char* text,
                      String* uid = nullptr, bool* newlyKeyed = nullptr);
  Err readCredential(const uint8_t secret[16], std::string& text, String* uid = nullptr);
  // Wipes the credential and puts `factoryKey` back as key A and B with the
  // factory access bits, so the card is blank again. Needs this secret (key B).
  Err resetCard(const uint8_t secret[16], const uint8_t factoryKey[6], String* uid = nullptr);

 private:
  // Presence of one reader's tag: raises tag / removed with debounce.
  struct Presence {
    TagInfo current;
    bool has = false;
    bool reported = false;  // tag event sent for current
    uint8_t misses = 0;
    String lastUid;
    uint32_t lastTagMs = 0;
  };
  void seen(Presence& p, const TagInfo& tag, uint32_t now);
  void missed(Presence& p, uint8_t limit);

  enum class Slot : uint8_t { kHf, kLf };
  // Combined modes: switches antennas when the current slot is over.
  void updateSlot(uint32_t now);
  void setSlot(Slot s, uint32_t now);
  // Before a MIFARE command: 13.56 MHz slot now, and kept while it runs.
  Err useHf();
  bool combined() const { return hf_ && lf_; }

  bool readPresentCard(TagInfo& out);
  bool readLfTag(TagInfo& out);
  // Wakes and selects one MIFARE Classic card (in card_).
  Err selectCard();
  // Authenticates the sector of `block` with the derived key B, or with the
  // factory key A when the sector is still blank (`blank` tells which).
  Err authForWrite(uint8_t block, const uint8_t keyB[6], const uint8_t factoryKey[6], bool& blank);
  // Selects the card and authenticates the sector holding `block`.
  Err open(uint8_t block, const MifareKey& key);
  bool auth(uint8_t block, bool keyB, const uint8_t key[6]) {
    return hf_->auth(block, keyB, key, card_);
  }
  void close();
  static String formatUid(const uint8_t* uid, uint8_t len);
  static String typeName(uint8_t sak);

  reader::Mode mode_ = reader::Mode::kRc522;
  Rc522Reader rc522_;
  Pn532Reader pn532_;
  Rdm6300 rdm_;
  HfReader* hf_ = nullptr;
  Rdm6300* lf_ = nullptr;
  bool hfReady_ = false;
  bool scanning_ = true;
  HfCard card_;

  Presence hfTag_;
  Presence lfTag_;
  uint32_t lastPollMs_ = 0;
  Slot slot_ = Slot::kHf;
  uint32_t slotStartMs_ = 0;
  bool lfSeenInSlot_ = false;
  uint32_t lastLfFrameMs_ = 0;

  TagHandler onTag_;
  TagHandler onRemoved_;
};
