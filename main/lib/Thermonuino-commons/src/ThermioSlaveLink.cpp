#include "ThermioSlaveLink.h"

ThermioSlaveLink::ThermioSlaveLink(uint8_t maxZone)
    : maxZone_(maxZone) {}

void ThermioSlaveLink::setLocalId(uint16_t id) {
  localId_ = id;
}

uint16_t ThermioSlaveLink::localId() const {
  return localId_;
}

void ThermioSlaveLink::setConsoleId(uint16_t id) {
  consoleId_ = id;
  consoleIdKnown_ = id != ThermioRfFrame::BroadcastId && id != localId_;
}

void ThermioSlaveLink::clearConsoleId() {
  consoleIdKnown_ = false;
  consoleId_ = ThermioRfFrame::BroadcastId;
}

bool ThermioSlaveLink::consoleIdKnown() const {
  return consoleIdKnown_;
}

uint16_t ThermioSlaveLink::consoleId() const {
  return consoleId_;
}

uint16_t ThermioSlaveLink::reportTargetId() const {
  return consoleIdKnown_ ? consoleId_ : ThermioRfFrame::BroadcastId;
}

bool ThermioSlaveLink::canLearnConsoleId(uint32_t nowMs, uint32_t learnWindowMs) const {
  return !consoleIdKnown_ && nowMs < learnWindowMs;
}

bool ThermioSlaveLink::learnConsoleIdFromResponse(uint16_t sourceId, uint32_t nowMs, uint32_t learnWindowMs) {
  if (!canLearnConsoleId(nowMs, learnWindowMs) ||
      sourceId == ThermioRfFrame::BroadcastId ||
      sourceId == localId_) {
    return false;
  }
  setConsoleId(sourceId);
  return true;
}

void ThermioSlaveLink::markAckReceived(uint8_t assignedZone) {
  consecutiveAckFailures_ = 0;
  consoleOffline_ = false;
  if (assignedZone >= 1 && assignedZone <= maxZone_) {
    assignedZone_ = assignedZone;
    pairZoneRequest_ = assignedZone;
  }
}

void ThermioSlaveLink::markAckMissed(uint32_t awakeWatchdogTicks) {
  if (consecutiveAckFailures_ < ConsoleOfflineAckFailures) {
    consecutiveAckFailures_++;
  }
  if (consecutiveAckFailures_ >= ConsoleOfflineAckFailures) {
    consoleOffline_ = true;
    consoleOfflineRetryAtWatchdogTick_ = awakeWatchdogTicks + ConsoleOfflineRetryWatchdogTicks;
  }
}

void ThermioSlaveLink::forceRetryByUser() {
  consecutiveAckFailures_ = 0;
  consoleOffline_ = false;
}

bool ThermioSlaveLink::consoleOffline() const {
  return consoleOffline_;
}

bool ThermioSlaveLink::offlineRetryDue(uint32_t awakeWatchdogTicks) const {
  return consoleOffline_ && (int32_t)(awakeWatchdogTicks - consoleOfflineRetryAtWatchdogTick_) >= 0;
}

bool ThermioSlaveLink::autoReportDue(uint32_t awakeWatchdogTicks, uint32_t autoStartTick, uint16_t intervalTicks) const {
  const bool retryDue = offlineRetryDue(awakeWatchdogTicks);
  return (!consoleOffline_ || retryDue) &&
      (int32_t)(awakeWatchdogTicks - autoStartTick) >= 0 &&
      (uint32_t)(awakeWatchdogTicks - lastReportAtWatchdogTick_) >= intervalTicks;
}

void ThermioSlaveLink::markReportAttemptStarted(uint32_t awakeWatchdogTicks) {
  if (offlineRetryDue(awakeWatchdogTicks)) {
    consoleOfflineRetryAtWatchdogTick_ = awakeWatchdogTicks + ConsoleOfflineRetryWatchdogTicks;
  }
  lastReportAtWatchdogTick_ = awakeWatchdogTicks;
}

uint8_t ThermioSlaveLink::nextPairZone() {
  if (pairZoneRequest_ < 1 || pairZoneRequest_ >= maxZone_) {
    pairZoneRequest_ = 1;
  } else {
    pairZoneRequest_++;
  }
  return pairZoneRequest_;
}

uint8_t ThermioSlaveLink::pairZoneRequest() const {
  return pairZoneRequest_;
}

uint8_t ThermioSlaveLink::assignedZone() const {
  return assignedZone_;
}
