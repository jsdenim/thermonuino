#include "SondeRfStatusService.h"

void SondeRfStatusService::begin() {
  radio_.beginPins();
  radio_.sleep();
  nextSpiRefreshAt_ = 0;
}

bool SondeRfStatusService::update(uint32_t now, bool force) {
  if (!force && (int32_t)(now - nextSpiRefreshAt_) < 0) {
    return false;
  }
  nextSpiRefreshAt_ = now + SpiRefreshMs;

  radio_.wake();
  const bool newSpiOk = radio_.testSpi();
  radio_.sleep();

  const bool changed = newSpiOk != spiOk_;
  spiOk_ = newSpiOk;
  return changed;
}

bool SondeRfStatusService::recordConsoleResponse(uint32_t now) {
  const bool wasOk = consoleOk(now);
  lastConsoleResponseAt_ = now;
  consoleResponseSeen_ = true;
  return wasOk != consoleOk(now);
}

bool SondeRfStatusService::spiOk() const {
  return spiOk_;
}

bool SondeRfStatusService::consoleOk(uint32_t now) const {
  return consoleResponseSeen_ &&
      (uint32_t)(now - lastConsoleResponseAt_) <= ConsoleOkHoldMs;
}
