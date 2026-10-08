#include "power.h"

#include <Wire.h>

#include "config.h"
#include "util.h"

const char* toString(ChargeState s) {
  switch (s) {
    case ChargeState::kCharging: return "charging";
    case ChargeState::kFull: return "full";
    case ChargeState::kFault: return "fault";
    default: return "discharging";
  }
}

bool Power::begin() {
  // Attach the pin as an ADC channel first: setting the attenuation on a pin
  // not yet read logs "Pin is not configured as analog channel" at boot.
  analogReadMilliVolts(pins::kBatAdc);
  analogSetPinAttenuation(pins::kBatAdc, ADC_11db);
  Wire.begin(pins::kI2cSda, pins::kI2cScl);
  sensorOk_ = ina_.begin();
  if (sensorOk_) ina_.setCalibration_32V_2A();  // 0.1 ohm shunt
  return sensorOk_;
}

// Mean of 16 samples, undoing the board's 200k/100k divider.
float Power::readAdcVolts() const {
  uint32_t mv = 0;
  for (uint8_t i = 0; i < 16; i++) mv += analogReadMilliVolts(pins::kBatAdc);
  return mv / 16.0f / 1000.0f * cfg::kBatDividerRatio;
}

// The ETA6098 has no status pin, so the state is inferred: with an INA219 from
// the current sign, otherwise from the voltage trend over kTrendWindowMs.
ChargeState Power::estimateState(float v, uint32_t now) {
  if (v < cfg::kBatMinPresentV) return ChargeState::kFault;
  if (sensorOk_) {
    if (info_.mA < -20) return ChargeState::kCharging;
    if (info_.mA > 20) return ChargeState::kDischarging;
    return v >= cfg::kFullV ? ChargeState::kFull : info_.state;
  }
  if (!trendRefMs_) {
    trendRefMs_ = now;
    trendRefV_ = v;
    return ChargeState::kDischarging;
  }
  if (now - trendRefMs_ < cfg::kTrendWindowMs) return info_.state;
  const float delta = v - trendRefV_;
  trendRefMs_ = now;
  trendRefV_ = v;
  if (delta > cfg::kTrendRiseV) return ChargeState::kCharging;
  if (delta < -cfg::kTrendRiseV / 2) return ChargeState::kDischarging;
  if (v >= cfg::kFullV) return ChargeState::kFull;  // flat and high: topped up on USB
  return info_.state;
}

void Power::loop() {
  const uint32_t now = millis();
  if (lastSampleMs_ && now - lastSampleMs_ < cfg::kPowerSampleMs) return;
  const bool first = lastSampleMs_ == 0;
  const uint32_t dt = first ? 0 : now - lastSampleMs_;
  lastSampleMs_ = now ? now : 1;

  const BatteryInfo prev = info_;

  if (sensorOk_) {
    // Bus voltage is taken at VIN-; add the shunt drop to get the cell.
    info_.v = ina_.getBusVoltage_V() + ina_.getShuntVoltage_mV() / 1000.0f;
    info_.mA = ina_.getCurrent_mA();
    info_.mW = ina_.getPower_mW();
  } else {
    const float raw = readAdcVolts();
    filtV_ = filtV_ == 0 ? raw : filtV_ + (raw - filtV_) * 0.2f;
    info_.v = filtV_;
  }
  haveVoltage_ = info_.v >= cfg::kBatMinPresentV;
  info_.state = estimateState(info_.v, now);
  if (haveVoltage_) {
    updateSoc(soc::fromVoltage(info_.v), dt);
    info_.pct = static_cast<uint8_t>(soc_ + 0.5f);
  } else {
    info_.pct = 0;
  }

  const bool onBattery = info_.state == ChargeState::kDischarging;
  info_.low = haveVoltage_ && onBattery && info_.pct < lowPct_;
  info_.critical = haveVoltage_ && onBattery && info_.pct < cfg::kBatteryCriticalPct;

  if (onSample_) onSample_(info_);
  if (onChange_ && (first || prev.state != info_.state || prev.low != info_.low ||
                    prev.critical != info_.critical)) {
    onChange_(info_);
  }
}

// Coulomb counting, anchored to the voltage curve when the cell is resting.
void Power::updateSoc(float voltSoc, uint32_t dtMs) {
  if (soc_ < 0) {
    soc_ = voltSoc;
    return;
  }
  const float hours = dtMs / 3600000.0f;
  soc_ -= info_.mA * hours / capacityMah_ * 100.0f;
  if (fabsf(info_.mA) < cfg::kRestingMa) soc_ += (voltSoc - soc_) * 0.02f;
  if (info_.state == ChargeState::kFull) soc_ = 100;
  soc_ = constrain(soc_, 0.0f, 100.0f);
}
