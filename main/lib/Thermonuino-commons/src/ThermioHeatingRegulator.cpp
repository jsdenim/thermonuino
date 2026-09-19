#include "ThermioHeatingRegulator.h"

#include <stdlib.h>

void ThermioHeatingRegulator::reset() {
  for (uint8_t i = 0; i < ZoneCount; i++) {
    zones_[i].holdBtuPerHour = DefaultHoldBtuPerHour;
    zones_[i].confidence = 0;
  }
  responseBtuPerC_ = DefaultResponseBtuPerC;
}

ThermioHeatingRegulator::Decision ThermioHeatingRegulator::decide(
    uint8_t zone,
    int16_t measuredDeciC,
    int16_t targetDeciC,
    uint16_t installedPowerW,
    bool doorOpen,
    bool stopMode) const {
  Decision decision = {};
  const uint8_t index = clampZone(zone);
  if (installedPowerW == 0) {
    installedPowerW = DefaultInstalledPowerW;
  }

  decision.installedPowerW = installedPowerW;
  decision.learnedHoldBtuPerHour = zones_[index].confidence > 0 ?
      zones_[index].holdBtuPerHour :
      DefaultHoldBtuPerHour;
  decision.learnedResponseBtuPerC = responseBtuPerC_;
  decision.holdConfidence = zones_[index].confidence;

  if (doorOpen || stopMode || targetDeciC <= 0) {
    return decision;
  }

  uint16_t maintenance = decision.learnedHoldBtuPerHour;
  if (measuredDeciC > targetDeciC + 3) {
    maintenance = 0;
  } else if (measuredDeciC > targetDeciC + 1) {
    maintenance = (uint32_t)maintenance * 35UL / 100UL;
  }

  uint16_t catchup = 0;
  if (targetDeciC > measuredDeciC + CatchupDeadbandDeciC) {
    const uint16_t effectiveDeficitDeciC =
        targetDeciC - measuredDeciC - CatchupDeadbandDeciC;
    const uint32_t requestedCatchup =
        (uint32_t)effectiveDeficitDeciC * responseBtuPerC_ / 10UL;
    catchup = requestedCatchup > 65535UL ? 65535 : (uint16_t)requestedCatchup;
  }

  uint32_t requested = (uint32_t)maintenance + catchup;
  const uint16_t maxBtu = installedBtuPerHour(installedPowerW);
  if (requested > maxBtu) {
    requested = maxBtu;
  }

  decision.maintenanceBtuPerHour = maintenance;
  decision.catchupBtuPerHour = catchup;
  decision.requestedBtuPerHour = requested;
  decision.requestedPowerW = powerFromBtu(decision.requestedBtuPerHour);
  decision.workload = workloadFromBtu(decision.requestedBtuPerHour, installedPowerW);
  decision.heating = decision.workload > 0;
  return decision;
}

void ThermioHeatingRegulator::observe(uint8_t zone,
                                      int16_t measuredBeforeDeciC,
                                      int16_t measuredAfterDeciC,
                                      uint16_t heatBtuPerHour,
                                      uint16_t maintenanceBtuPerHour,
                                      bool learningEnabled) {
  if (!learningEnabled) {
    return;
  }

  const int16_t deltaDeciC = measuredAfterDeciC - measuredBeforeDeciC;
  const int32_t responseBtuPerC = (int32_t)responseBtuPerC_;
  const int32_t observedHold =
      (int32_t)heatBtuPerHour -
      ((int32_t)deltaDeciC * responseBtuPerC * 4L / 10L);
  const uint16_t clampedHold = observedHold <= 0 ? 0 :
      observedHold > installedBtuPerHour(DefaultInstalledPowerW) ?
      installedBtuPerHour(DefaultInstalledPowerW) :
      (uint16_t)observedHold;

  const uint8_t index = clampZone(zone);
  if (zones_[index].confidence == 0) {
    zones_[index].holdBtuPerHour = clampedHold;
  } else {
    zones_[index].holdBtuPerHour =
        blendU16(zones_[index].holdBtuPerHour, clampedHold, HoldLearningRatePercent);
  }
  if (zones_[index].confidence < 12) {
    zones_[index].confidence++;
  }

  const int32_t extraHeatBtuPerHour = (int32_t)heatBtuPerHour - maintenanceBtuPerHour;
  if (extraHeatBtuPerHour > 1600 && deltaDeciC >= ResponseLearningMinDeltaDeciC) {
    const uint32_t extraHeatBtu = (uint32_t)extraHeatBtuPerHour / 4UL;
    uint32_t observedResponse = extraHeatBtu * 10UL / (uint16_t)deltaDeciC;
    if (observedResponse < MinResponseBtuPerC) {
      observedResponse = MinResponseBtuPerC;
    } else if (observedResponse > MaxResponseBtuPerC) {
      observedResponse = MaxResponseBtuPerC;
    }
    responseBtuPerC_ = blendU32(responseBtuPerC_, observedResponse, ResponseLearningRatePercent);
  }
}

uint16_t ThermioHeatingRegulator::learnedHoldBtuPerHour(uint8_t zone) const {
  return zones_[clampZone(zone)].holdBtuPerHour;
}

uint8_t ThermioHeatingRegulator::holdConfidence(uint8_t zone) const {
  return zones_[clampZone(zone)].confidence;
}

uint32_t ThermioHeatingRegulator::learnedResponseBtuPerC() const {
  return responseBtuPerC_;
}

uint8_t ThermioHeatingRegulator::clampZone(uint8_t zone) {
  if (zone >= ZoneCount) {
    return ZoneCount - 1;
  }
  return zone;
}

uint16_t ThermioHeatingRegulator::installedBtuPerHour(uint16_t installedPowerW) {
  return ((uint32_t)installedPowerW * BtuPerWattHourX1000 + 500UL) / 1000UL;
}

uint16_t ThermioHeatingRegulator::workloadFromBtu(uint16_t requestedBtuPerHour,
                                                  uint16_t installedPowerW) {
  const uint16_t maxBtu = installedBtuPerHour(installedPowerW);
  if (requestedBtuPerHour == 0) {
    return 0;
  }
  if (maxBtu == 0) {
    return 255;
  }
  const uint32_t scaled = (uint32_t)requestedBtuPerHour * 255UL + maxBtu / 2;
  const uint32_t workload = scaled / maxBtu;
  return workload > 255 ? 255 : (uint16_t)workload;
}

uint16_t ThermioHeatingRegulator::powerFromBtu(uint16_t requestedBtuPerHour) {
  return ((uint32_t)requestedBtuPerHour * 1000UL + BtuPerWattHourX1000 / 2) /
      BtuPerWattHourX1000;
}

uint16_t ThermioHeatingRegulator::blendU16(uint16_t current, uint16_t observed, uint8_t percent) {
  return ((uint32_t)current * (100U - percent) + (uint32_t)observed * percent + 50U) / 100U;
}

uint32_t ThermioHeatingRegulator::blendU32(uint32_t current, uint32_t observed, uint8_t percent) {
  return (current * (100U - percent) + observed * percent + 50U) / 100U;
}
