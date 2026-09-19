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
  int16_t temperatureOffsetDeciC() const;
  uint8_t historyCount() const;
  int16_t historyDeciC(uint8_t index) const;
  void recordCurrentToHistory();
  void clearHistory();

private:
  static const uint8_t AhtAddr = 0x38;
  static const uint32_t SensorRefreshMs = 30000;
  static const uint8_t MaxHistoryCount = 12;

  uint32_t nextSensorRefreshAt_ = 0;
  int16_t currentTempDeciC_ = 0;
  int16_t rawTempDeciC_ = 0;
  int16_t temperatureOffsetDeciC_ = 0;
  int16_t historyDeciC_[MaxHistoryCount] = {0};
  uint8_t historyCount_ = 0;
  bool currentTempKnown_ = false;

  bool readAhtStatus(uint8_t &status);
  bool initAht();
  bool readAhtTemperatureDeciC(int16_t &temperatureDeciC);
  void appendHistory(int16_t tempDeciC);
  int16_t roundToHalfDegree(int16_t tempDeciC);
};

#endif
