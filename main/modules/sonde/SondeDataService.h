#ifndef SONDE_DATA_SERVICE_H
#define SONDE_DATA_SERVICE_H

#include <Arduino.h>

class SondeDataService {
public:
  void begin();
  bool update(uint32_t now, bool force = false);

  bool currentTempKnown() const;
  int16_t currentTempDeciC() const;

private:
  static const uint8_t AhtAddr = 0x38;
  static const uint32_t SensorRefreshMs = 30000;

  uint32_t nextSensorRefreshAt_ = 0;
  int16_t currentTempDeciC_ = 0;
  bool currentTempKnown_ = false;

  bool readAhtStatus(uint8_t &status);
  bool initAht();
  bool readAhtTemperatureDeciC(int16_t &temperatureDeciC);
  int16_t roundToHalfDegree(int16_t tempDeciC);
};

#endif
