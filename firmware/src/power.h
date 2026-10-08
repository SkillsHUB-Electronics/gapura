// Battery monitor: voltage from the board ADC (or INA219 when fitted, which also
// gives current), charge state, state of charge.
#pragma once

#include <Adafruit_INA219.h>
#include <Arduino.h>

#include <functional>

enum class ChargeState : uint8_t { kDischarging, kCharging, kFull, kFault };

struct BatteryInfo {
  float v = 0;    // cell voltage
  float mA = 0;   // + discharging, - charging
  float mW = 0;
  uint8_t pct = 0;
  ChargeState state = ChargeState::kDischarging;
  bool low = false;
  bool critical = false;
};

const char* toString(ChargeState s);

class Power {
 public:
  using Handler = std::function<void(const BatteryInfo&)>;

  bool begin();  // false if INA219 not found (ADC voltage still works)
  void loop();

  const BatteryInfo& info() const { return info_; }
  bool sensorOk() const { return sensorOk_; }

  void setCapacityMah(uint16_t mah) { capacityMah_ = mah; }
  void setLowPct(uint8_t pct) { lowPct_ = pct; }

  void onSample(Handler h) { onSample_ = std::move(h); }  // every sample
  void onChange(Handler h) { onChange_ = std::move(h); }  // state/low/critical

 private:
  float readAdcVolts() const;
  ChargeState estimateState(float v, uint32_t now);
  void updateSoc(float voltSoc, uint32_t dtMs);

  Adafruit_INA219 ina_;
  bool sensorOk_ = false;
  bool haveVoltage_ = false;
  float trendRefV_ = 0;
  uint32_t trendRefMs_ = 0;
  float filtV_ = 0;
  BatteryInfo info_;
  float soc_ = -1;  // < 0: not initialised
  uint32_t lastSampleMs_ = 0;
  uint16_t capacityMah_ = 2000;
  uint8_t lowPct_ = 15;

  Handler onSample_;
  Handler onChange_;
};
