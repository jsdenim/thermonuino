#ifndef SONDE_RF_STATUS_SERVICE_H
#define SONDE_RF_STATUS_SERVICE_H

#include <Arduino.h>
#include <ThermioRfCc1101.h>

class SondeRfStatusService {
public:
  explicit SondeRfStatusService(ThermioRfCc1101 &radio) : radio_(radio) {
  }

  void begin();
  bool update(uint32_t watchdogTick, bool force = false);
  bool recordConsoleResponse(uint32_t watchdogTick);

  bool spiOk() const;
  bool consoleOk(uint32_t watchdogTick) const;

private:
  static const uint8_t SpiRefreshWatchdogTicks = 38; // ~5 min.
  static const uint8_t ConsoleOkHoldWatchdogTicks = 113; // ~15 min.

  ThermioRfCc1101 &radio_;
  uint32_t nextSpiRefreshAtWatchdogTick_ = 0;
  uint32_t lastConsoleResponseAtWatchdogTick_ = 0;
  bool spiOk_ = false;
  bool consoleResponseSeen_ = false;
};

#endif
