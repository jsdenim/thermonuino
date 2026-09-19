#include "SondeDataService.h"

#include <Wire.h>

void SondeDataService::begin() {
  nextSensorRefreshAt_ = 0;
}

bool SondeDataService::update(uint32_t now, bool force) {
  if (!force && (int32_t)(now - nextSensorRefreshAt_) < 0) {
    return false;
  }

  nextSensorRefreshAt_ = now + SensorRefreshMs;
  int16_t newTemp = currentTempDeciC_;
  const bool newValid = readAhtTemperatureDeciC(newTemp);
  if (newValid) {
    rawTempDeciC_ = newTemp;
    newTemp = roundToHalfDegree(newTemp + temperatureOffsetDeciC_);
  }
  const bool changed = newValid != currentTempKnown_ ||
      (newValid && newTemp != currentTempDeciC_);

  currentTempKnown_ = newValid;
  if (newValid) {
    currentTempDeciC_ = newTemp;
  }
  return changed;
}

void SondeDataService::pauseUntil(uint32_t until) {
  if ((int32_t)(until - nextSensorRefreshAt_) > 0) {
    nextSensorRefreshAt_ = until;
  }
}

void SondeDataService::setTemperatureOffsetDeciC(int16_t offsetDeciC) {
  temperatureOffsetDeciC_ = offsetDeciC;
}

bool SondeDataService::currentTempKnown() const {
  return currentTempKnown_;
}

int16_t SondeDataService::currentTempDeciC() const {
  return currentTempDeciC_;
}

int16_t SondeDataService::rawTempDeciC() const {
  return rawTempDeciC_;
}

int16_t SondeDataService::temperatureOffsetDeciC() const {
  return temperatureOffsetDeciC_;
}

bool SondeDataService::readAhtStatus(uint8_t &status) {
  Wire.requestFrom(AhtAddr, (uint8_t)1);
  if (Wire.available() != 1) {
    return false;
  }

  status = Wire.read();
  return true;
}

bool SondeDataService::initAht() {
  Wire.beginTransmission(AhtAddr);
  Wire.write(0xBE);
  Wire.write(0x08);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) {
    return false;
  }

  delay(10);
  uint8_t status = 0;
  return readAhtStatus(status);
}

bool SondeDataService::readAhtTemperatureDeciC(int16_t &temperatureDeciC) {
  uint8_t status = 0;
  if (!readAhtStatus(status)) {
    return false;
  }

  if ((status & 0x08) == 0 && !initAht()) {
    return false;
  }

  Wire.beginTransmission(AhtAddr);
  Wire.write(0xAC);
  Wire.write(0x33);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) {
    return false;
  }

  delay(80);
  uint8_t data[6] = {0};
  Wire.requestFrom(AhtAddr, (uint8_t)6);
  for (uint8_t i = 0; i < sizeof(data); i++) {
    if (!Wire.available()) {
      return false;
    }
    data[i] = Wire.read();
  }

  if ((data[0] & 0x80) != 0) {
    return false;
  }

  const uint32_t rawTemp =
      (((uint32_t)data[3] & 0x0F) << 16) |
      ((uint32_t)data[4] << 8) |
      data[5];
  temperatureDeciC = (int16_t)((rawTemp * 2000UL + 524288UL) / 1048576UL) - 500;
  return true;
}

int16_t SondeDataService::roundToHalfDegree(int16_t tempDeciC) {
  if (tempDeciC >= 0) {
    return ((tempDeciC + 2) / 5) * 5;
  }
  return ((tempDeciC - 2) / 5) * 5;
}
