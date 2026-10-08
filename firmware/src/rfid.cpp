#include "rfid.h"

#include <esp_task_wdt.h>

#include "cardsec.h"
#include "config.h"

bool Rfid::begin(reader::Mode mode) {
  mode_ = mode;
  if (reader::hasRc522(mode)) hf_ = &rc522_;
  else if (reader::hasPn532(mode)) hf_ = &pn532_;
  if (hf_) hfReady_ = hf_->begin();
  if (reader::hasRdm6300(mode)) {
    lf_ = &rdm_;
    rdm_.begin();
  }
  if (combined()) setSlot(Slot::kHf, millis());
  else if (lf_) rdm_.power(true);  // alone (or the 13.56 MHz chip is missing): always on
  return ready();
}

void Rfid::loop() {
  if (!hf_ && !lf_) return;
  const uint32_t now = millis();
  updateSlot(now);

  if (lf_ && (!combined() || slot_ == Slot::kLf)) {
    TagInfo tag;
    if (readLfTag(tag)) {
      lfSeenInSlot_ = true;
      lastLfFrameMs_ = now;
      seen(lfTag_, tag, now);
    } else if (!combined() && lfTag_.has && now - lastLfFrameMs_ >= cfg::kLfGoneMs) {
      missed(lfTag_, 1);  // the module repeats its frame while the tag stays
    }
  }

  if (hf_ && hfReady_ && (!combined() || slot_ == Slot::kHf) &&
      now - lastPollMs_ >= cfg::kRfidPollMs) {
    lastPollMs_ = now;
    TagInfo tag;
    if (readPresentCard(tag)) seen(hfTag_, tag, now);
    else missed(hfTag_, cfg::kTagRemovedMisses);
  }
}

void Rfid::seen(Presence& p, const TagInfo& tag, uint32_t now) {
  p.misses = 0;
  if (p.has && tag.uid == p.current.uid) return;
  // A different card replaced the previous one without a gap.
  if (p.has && p.reported && onRemoved_) onRemoved_(p.current);

  const bool bounce = tag.uid == p.lastUid && now - p.lastTagMs < cfg::kTagDebounceMs;
  p.current = tag;
  p.has = true;
  p.reported = !bounce && scanning_;
  p.lastUid = tag.uid;
  p.lastTagMs = now;
  if (p.reported && onTag_) onTag_(tag);
}

void Rfid::missed(Presence& p, uint8_t limit) {
  if (!p.has || ++p.misses < limit) return;
  p.has = false;
  p.misses = 0;
  if (p.reported && onRemoved_) onRemoved_(p.current);
}

// The 125 kHz tag is removed when a whole slot passes without its frame.
void Rfid::updateSlot(uint32_t now) {
  if (!combined()) return;
  const uint32_t len = slot_ == Slot::kHf ? cfg::kHfSlotMs : cfg::kLfSlotMs;
  if (now - slotStartMs_ < len) return;
  if (slot_ == Slot::kLf) {
    if (!lfSeenInSlot_) missed(lfTag_, 1);
    setSlot(Slot::kHf, now);
  } else {
    setSlot(Slot::kLf, now);
  }
}

void Rfid::setSlot(Slot s, uint32_t now) {
  if (s == Slot::kHf) {
    rdm_.power(false);
    hf_->field(true);
  } else {
    hf_->field(false);
    rdm_.power(true);
    lfSeenInSlot_ = false;
  }
  slot_ = s;
  slotStartMs_ = now;
}

Err Rfid::useHf() {
  if (!hf_) return Err::kNoReader;
  if (combined()) {
    if (slot_ != Slot::kHf) {
      setSlot(Slot::kHf, millis());
      delay(5);  // field up before the card is woken
    }
    slotStartMs_ = millis();  // a full slot for this command
  }
  return Err::kOk;
}

bool Rfid::readPresentCard(TagInfo& out) {
  HfCard card;
  if (!hf_->select(card)) return false;
  out.uid = formatUid(card.uid, card.uidLen);
  out.type = typeName(card.sak);
  out.reader = reader::hasPn532(mode_) ? "pn532" : "rc522";
  out.lf = false;
  hf_->halt();
  return true;
}

bool Rfid::readLfTag(TagInfo& out) {
  uint8_t id[em4100::kIdLen];
  if (!rdm_.poll(id)) return false;
  out.uid = formatUid(id, sizeof(id));
  out.type = "EM4100";
  out.reader = "rdm6300";
  out.lf = true;
  return true;
}

String Rfid::formatUid(const uint8_t* uid, uint8_t len) {
  static const char kHex[] = "0123456789ABCDEF";
  String s;
  s.reserve(len * 3);
  for (uint8_t i = 0; i < len; i++) {
    if (i) s += ' ';
    s += kHex[uid[i] >> 4];
    s += kHex[uid[i] & 0x0F];
  }
  return s;
}

String Rfid::typeName(uint8_t sak) {
  return String(MFRC522::PICC_GetTypeName(MFRC522::PICC_GetType(sak)));
}

bool Rfid::waitUid(uint32_t timeoutMs, TagInfo& out) {
  const uint32_t start = millis();
  do {
    updateSlot(millis());
    if (hf_ && hfReady_ && (!combined() || slot_ == Slot::kHf) && readPresentCard(out)) return true;
    if (lf_ && (!combined() || slot_ == Slot::kLf) && readLfTag(out)) return true;
    esp_task_wdt_reset();
    delay(20);
  } while (millis() - start < timeoutMs);
  return false;
}

Err Rfid::open(uint8_t block, const MifareKey& key) {
  const Err err = selectCard();
  if (err != Err::kOk) return err;
  if (!auth(block, key.typeB, key.bytes)) {
    close();
    return Err::kAuthFailed;
  }
  return Err::kOk;
}

void Rfid::close() { hf_->halt(); }

Err Rfid::readBlock(uint8_t block, const MifareKey& key, Block out) {
  if (block >= mifare::kBlockCount) return Err::kBadArgs;
  Err err = open(block, key);
  if (err != Err::kOk) return err;
  err = hf_->read(block, out) ? Err::kOk : Err::kReadFailed;
  close();
  return err;
}

Err Rfid::writeBlock(uint8_t block, const MifareKey& key, const Block in) {
  if (block >= mifare::kBlockCount) return Err::kBadArgs;
  if (!mifare::isWritable(block)) return Err::kForbiddenBlock;
  Err err = open(block, key);
  if (err != Err::kOk) return err;
  err = hf_->write(block, in) ? Err::kOk : Err::kWriteFailed;
  close();
  return err;
}

Err Rfid::readSector(uint8_t sector, const MifareKey& key, Block out[4]) {
  if (sector >= mifare::kSectorCount) return Err::kBadArgs;
  const uint8_t first = sector * 4;
  Err err = open(first + 3, key);
  if (err != Err::kOk) return err;
  for (uint8_t i = 0; i < 4 && err == Err::kOk; i++) {
    if (!hf_->read(first + i, out[i])) err = Err::kReadFailed;
  }
  close();
  return err;
}

Err Rfid::selectCard() {
  const Err err = useHf();
  if (err != Err::kOk) return err;
  if (!hf_->select(card_)) return Err::kNoCard;
  const MFRC522::PICC_Type type = MFRC522::PICC_GetType(card_.sak);
  if (type != MFRC522::PICC_TYPE_MIFARE_MINI && type != MFRC522::PICC_TYPE_MIFARE_1K &&
      type != MFRC522::PICC_TYPE_MIFARE_4K) {
    close();
    return Err::kUnsupportedCard;
  }
  return Err::kOk;
}

Err Rfid::authForWrite(uint8_t block, const uint8_t keyB[6], const uint8_t factoryKey[6],
                       bool& blank) {
  if (auth(block, true, keyB)) {
    blank = false;
    return Err::kOk;
  }
  close();  // a failed auth needs a fresh select
  Err err = selectCard();
  if (err != Err::kOk) return err;
  if (!auth(block, false, factoryKey)) {
    close();
    return Err::kAuthFailed;
  }
  blank = true;
  return Err::kOk;
}

Err Rfid::writeCredential(const uint8_t secret[16], const uint8_t factoryKey[6], const char* text,
                          String* uid, bool* newlyKeyed) {
  const size_t len = text ? strlen(text) : 0;
  if (len == 0 || len > cardsec::kMaxCredential) return Err::kBadArgs;
  for (size_t i = 0; i < len; i++) {
    if (text[i] < 0x20 || text[i] >= 0x7F) return Err::kBadArgs;
  }
  Err err = selectCard();
  if (err != Err::kOk) return err;
  if (uid) *uid = formatUid(card_.uid, card_.uidLen);
  const cardsec::Keys keys = cardsec::deriveKeys(secret, card_.uid, card_.uidLen);

  uint8_t data[cardsec::kCredLen] = {0};
  memcpy(data, text, len);
  uint8_t mac[16];
  cardsec::computeCredMac(secret, card_.uid, card_.uidLen, data, mac);
  uint8_t trailer[16];
  cardsec::buildTrailer(keys, trailer);

  bool anyBlank = false;
  // Data sectors first, the mac sector last: an interrupted write never leaves a
  // valid signature over half-written data.
  for (size_t si = 0; si < 2; si++) {
    const cardsec::CredSector& sec = cardsec::kCredSectors[si];
    bool blank = false;
    err = authForWrite(sec.first, keys.b, factoryKey, blank);
    if (err != Err::kOk) return err;
    bool ok = true;
    for (uint8_t i = 0; i < 3 && ok; i++) {
      ok = hf_->write(sec.first + i, data + 16 * (si * 3 + i));
    }
    if (ok && blank) ok = hf_->write(sec.trailer, trailer);
    if (!ok) {
      close();
      return Err::kWriteFailed;
    }
    anyBlank = anyBlank || blank;
  }
  bool blank = false;
  err = authForWrite(cardsec::kMacBlock, keys.b, factoryKey, blank);
  if (err != Err::kOk) return err;
  bool ok = hf_->write(cardsec::kMacBlock, mac);
  if (ok && blank) ok = hf_->write(cardsec::kMacTrailer, trailer);
  close();
  if (newlyKeyed) *newlyKeyed = anyBlank || blank;
  return ok ? Err::kOk : Err::kWriteFailed;
}

Err Rfid::readCredential(const uint8_t secret[16], std::string& text, String* uid) {
  Err err = selectCard();
  if (err != Err::kOk) return err;
  if (uid) *uid = formatUid(card_.uid, card_.uidLen);
  const cardsec::Keys keys = cardsec::deriveKeys(secret, card_.uid, card_.uidLen);
  const auto authA = [&](uint8_t block) { return auth(block, false, keys.a); };
  uint8_t data[cardsec::kCredLen], mac[16];
  for (size_t si = 0; si < 2; si++) {
    const cardsec::CredSector& sec = cardsec::kCredSectors[si];
    if (!authA(sec.first)) {
      close();
      return Err::kAuthFailed;
    }
    for (uint8_t i = 0; i < 3; i++) {
      if (!hf_->read(sec.first + i, data + 16 * (si * 3 + i))) {
        close();
        return Err::kReadFailed;
      }
    }
  }
  if (!authA(cardsec::kMacBlock)) {
    close();
    return Err::kAuthFailed;
  }
  if (!hf_->read(cardsec::kMacBlock, mac)) {
    close();
    return Err::kReadFailed;
  }
  uint8_t expected[16];
  cardsec::computeCredMac(secret, card_.uid, card_.uidLen, data, expected);
  close();
  if (!cardsec::macEqual(mac, expected)) return Err::kBadSignature;
  text.clear();
  for (size_t i = 0; i < sizeof(data) && data[i]; i++) {
    if (data[i] >= 0x20 && data[i] < 0x7F) text += static_cast<char>(data[i]);
  }
  return Err::kOk;
}

Err Rfid::resetCard(const uint8_t secret[16], const uint8_t factoryKey[6], String* uid) {
  Err err = selectCard();
  if (err != Err::kOk) return err;
  if (uid) *uid = formatUid(card_.uid, card_.uidLen);
  const cardsec::Keys keys = cardsec::deriveKeys(secret, card_.uid, card_.uidLen);
  const auto authB = [&](uint8_t block) { return auth(block, true, keys.b); };
  // The mac sector is always locked once a credential was written: it proves the secret.
  if (!authB(cardsec::kMacBlock)) {
    close();
    return Err::kAuthFailed;  // not locked with this secret (or already blank)
  }
  uint8_t zero[16] = {0};
  uint8_t trailer[16];
  memcpy(trailer, factoryKey, 6);
  memcpy(trailer + 6, cardsec::kFactoryAccessBits, 4);
  memcpy(trailer + 10, factoryKey, 6);

  bool ok = true;
  // Data sectors: wipe if they were locked, skip if still blank.
  for (size_t si = 0; si < 2 && ok; si++) {
    const cardsec::CredSector& sec = cardsec::kCredSectors[si];
    if (!authB(sec.first)) {
      close();  // not provisioned: re-select and carry on
      err = selectCard();
      if (err != Err::kOk) return err;
      continue;
    }
    for (uint8_t i = 0; i < 3 && ok; i++) {
      ok = hf_->write(sec.first + i, zero);
    }
    ok = ok && hf_->write(sec.trailer, trailer);
  }
  // The mac sector last: signature first, trailer at the very end.
  if (ok && !authB(cardsec::kMacBlock)) ok = false;
  ok = ok && hf_->write(cardsec::kMacBlock, zero) &&
       hf_->write(cardsec::kMacTrailer, trailer);
  close();
  return ok ? Err::kOk : Err::kWriteFailed;
}

void Rfid::powerDown() {
  if (hf_ && hfReady_) hf_->powerDown();
  if (lf_) rdm_.power(false);
}
