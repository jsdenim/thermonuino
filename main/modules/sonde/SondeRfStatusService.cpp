#include "SondeRfStatusService.h"

void SondeRfStatusService::begin() {
  radio_.beginPins();
  radio_.sleep();
  nextSpiRefreshAtWatchdogTick_ = 0;
}

bool SondeRfStatusService::update(uint32_t watchdogTick, bool force) {
  if (!force && (int32_t)(watchdogTick - nextSpiRefreshAtWatchdogTick_) < 0) {
    return false;
  }
  nextSpiRefreshAtWatchdogTick_ = watchdogTick + SpiRefreshWatchdogTicks;

  radio_.wake();
  const bool newSpiOk = radio_.testSpi();
  radio_.sleep();

  const bool changed = newSpiOk != spiOk_;
  spiOk_ = newSpiOk;
  return changed;
}

bool SondeRfStatusService::recordConsoleResponse(uint32_t watchdogTick) {
  const bool wasOk = consoleOk(watchdogTick);
  lastConsoleResponseAtWatchdogTick_ = watchdogTick;
  consoleResponseSeen_ = true;
  return wasOk != consoleOk(watchdogTick);
}

bool SondeRfStatusService::spiOk() const {
  return spiOk_;
}

bool SondeRfStatusService::consoleOk(uint32_t watchdogTick) const {
  return consoleResponseSeen_ &&
      (uint32_t)(watchdogTick - lastConsoleResponseAtWatchdogTick_) <=
          ConsoleOkHoldWatchdogTicks;
}
