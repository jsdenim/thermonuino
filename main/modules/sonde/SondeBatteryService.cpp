#include "SondeBatteryService.h"

void SondeBatteryService::begin() {
  pinMode(pins_.batterySense, INPUT);
  nextRefreshAtWatchdogTick_ = 0;
}

bool SondeBatteryService::update(uint32_t watchdogTick, bool force) {
  if (!force && (int32_t)(watchdogTick - nextRefreshAtWatchdogTick_) < 0) {
    return false;
  }
  nextRefreshAtWatchdogTick_ = watchdogTick + RefreshWatchdogTicks;

  const uint16_t newBatteryMv = readBatteryMv();
  const bool newKnown = newBatteryMv > NoBatteryThresholdMv;
  const bool newLow = newKnown && newBatteryMv <= LowThresholdMv;
  const bool newCritical = newKnown && newBatteryMv <= CriticalThresholdMv;
  const bool changed = newKnown != batteryKnown_ ||
      newBatteryMv != batteryMv_ ||
      newLow != batteryLow_ ||
      newCritical != batteryCritical_;

  batteryMv_ = newBatteryMv;
  batteryKnown_ = newKnown;
  batteryLow_ = newLow;
  batteryCritical_ = newCritical;
  return changed;
}

uint16_t SondeBatteryService::batteryMv() const {
  return batteryMv_;
}

bool SondeBatteryService::batteryKnown() const {
  return batteryKnown_;
}

bool SondeBatteryService::batteryLow() const {
  return batteryLow_;
}

bool SondeBatteryService::batteryCritical() const {
  return batteryCritical_;
}

uint16_t SondeBatteryService::readBatteryMv() {
  pinMode(pins_.batterySense, OUTPUT);
  digitalWrite(pins_.batterySense, LOW);
  delay(2);
  pinMode(pins_.batterySense, INPUT);
  (void)analogRead(pins_.batterySense);

  uint16_t sum = 0;
  for (uint8_t i = 0; i < 4; i++) {
    sum += analogRead(pins_.batterySense);
  }
  const uint16_t adc = (sum + 2) / 4;
  return (uint32_t)adc * AdcReferenceMv * DividerMultiplier / 1023UL;
}
