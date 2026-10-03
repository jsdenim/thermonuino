#pragma once

#include <stdint.h>

class ThermioHeatingRegulator {
 public:
  static const uint8_t ZoneCount = 4;
  static const uint16_t DefaultInstalledPowerW = 2100;
  static const uint16_t SlotMinutes = 15;

  struct Decision {
    uint16_t installedPowerW;
    uint16_t requestedPowerW;
    uint16_t workload;
    uint16_t learnedHoldBtuPerHour;
    uint32_t learnedResponseBtuPerC;
    uint16_t maintenanceBtuPerHour;
    uint16_t catchupBtuPerHour;
    uint16_t requestedBtuPerHour;
    uint8_t holdConfidence;
    bool heating;
  };

  void reset();
  void resetZone(uint8_t zone);

  Decision decide(uint8_t zone,
                  int16_t measuredDeciC,
                  int16_t targetDeciC,
                  uint16_t installedPowerW,
                  bool doorOpen,
                  bool stopMode) const;

  void observe(uint8_t zone,
               int16_t measuredBeforeDeciC,
               int16_t measuredAfterDeciC,
               uint16_t installedPowerW,
               uint16_t heatBtuPerHour,
               uint16_t maintenanceBtuPerHour,
               bool learningEnabled);

  uint16_t learnedHoldBtuPerHour(uint8_t zone) const;
  uint8_t holdConfidence(uint8_t zone) const;
  uint32_t learnedResponseBtuPerC() const;

 private:
  struct ZoneMemory {
    uint16_t holdBtuPerHour;
    uint8_t confidence;
  };

  static const uint8_t DefaultHoldPercentOfInstalledPower = 15;
  static const uint8_t HoldConfidenceMax = 12;
  static const uint32_t DefaultResponseBtuPerC = 16000;
  static const uint32_t MinResponseBtuPerC = 4000;
  static const uint32_t MaxResponseBtuPerC = 80000;
  static const uint8_t CatchupDeadbandDeciC = 2;
  static const uint8_t CatchupSofteningDivisor = 4;
  static const uint8_t MaintenanceAtTargetPercent = 50;
  static const uint8_t MaintenanceFadeOutDeciC = 10;
  static const uint8_t ResponseLearningMinDeltaDeciC = 2;
  static const uint8_t HoldLearningRateUntrustedPercent = 45;
  static const uint8_t HoldLearningRateTrustedPercent = 6;
  static const uint8_t ResponseLearningRatePercent = 8;
  static const uint16_t BtuPerWattHourX1000 = 3412;

  ZoneMemory zones_[ZoneCount];
  uint32_t responseBtuPerC_;

  static uint8_t clampZone(uint8_t zone);
  static uint16_t defaultHoldBtuPerHour(uint16_t installedPowerW);
  static uint8_t holdLearningRatePercent(uint8_t confidence);
  static uint16_t installedBtuPerHour(uint16_t installedPowerW);
  static uint16_t workloadFromBtu(uint16_t requestedBtuPerHour, uint16_t installedPowerW);
  static uint16_t powerFromBtu(uint16_t requestedBtuPerHour);
  static uint16_t blendU16(uint16_t current, uint16_t observed, uint8_t percent);
  static uint32_t blendU32(uint32_t current, uint32_t observed, uint8_t percent);
};
