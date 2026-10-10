#ifndef SONDE_DATA_SERVICE_H
#define SONDE_DATA_SERVICE_H

#include <Arduino.h>

class SondeDataService {
public:
  void begin();
  bool update(uint32_t now, bool force = false);
  void pauseUntil(uint32_t until);
  void setTemperatureOffsetDeciC(int16_t offsetDeciC);

  bool currentTempKnown() const;
  int16_t currentTempDeciC() const;
  int16_t rawTempDeciC() const;
  bool humidityKnown() const;
  uint16_t humidityDeciPct() const;
  int16_t temperatureOffsetDeciC() const;

private:
  static const uint8_t AhtAddr = 0x38;
  static const uint32_t SensorRefreshMs = 30000;

  uint32_t nextSensorRefreshAt_ = 0;
  int16_t currentTempDeciC_ = 0;
  int16_t rawTempDeciC_ = 0;
  uint16_t humidityDeciPct_ = 0;
  int16_t temperatureOffsetDeciC_ = 0;
  bool currentTempKnown_ = false;
  bool humidityKnown_ = false;

  bool readAhtStatus(uint8_t &status);
  bool initAht();
  bool readAhtMeasurement(int16_t &temperatureDeciC, uint16_t &humidityDeciPct);
  int16_t roundToHalfDegree(int16_t tempDeciC);
};

#endif
