#ifndef SONDE_RF_STATUS_SERVICE_H
#define SONDE_RF_STATUS_SERVICE_H

#include <Arduino.h>
#include <ThermioRfCc1101.h>

class SondeRfStatusService {
public:
  explicit SondeRfStatusService(ThermioRfCc1101 &radio) : radio_(radio) {
  }

  void begin();
  bool update(uint32_t now, bool force = false);
  bool recordConsoleResponse(uint32_t now);

  bool spiOk() const;
  bool consoleOk(uint32_t now) const;

private:
  static const uint32_t SpiRefreshMs = 300000;
  static const uint32_t ConsoleOkHoldMs = 900000;

  ThermioRfCc1101 &radio_;
  uint32_t nextSpiRefreshAt_ = 0;
  uint32_t lastConsoleResponseAt_ = 0;
  bool spiOk_ = false;
  bool consoleResponseSeen_ = false;
};

#endif
