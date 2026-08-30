#ifndef SONDE_BATTERY_SERVICE_H
#define SONDE_BATTERY_SERVICE_H

#include <Arduino.h>

class SondeBatteryService {
public:
  struct Pins {
    uint8_t batterySense;
  };

  explicit SondeBatteryService(const Pins &pins) : pins_(pins) {
  }

  void begin();
  bool update(uint32_t now, bool force = false);

  uint16_t batteryMv() const;
  bool batteryKnown() const;
  bool batteryLow() const;
  bool batteryCritical() const;

private:
  static const uint32_t RefreshMs = 300000;
  static const uint16_t AdcReferenceMv = 3300;
  static const uint16_t DividerMultiplier = 2;
  static const uint16_t NoBatteryThresholdMv = 50;
  static const uint16_t LowThresholdMv = 2400;
  static const uint16_t CriticalThresholdMv = 2200;

  Pins pins_;
  uint32_t nextRefreshAt_ = 0;
  uint16_t batteryMv_ = 0;
  bool batteryKnown_ = false;
  bool batteryLow_ = false;
  bool batteryCritical_ = false;

  uint16_t readBatteryMv();
};

#endif
