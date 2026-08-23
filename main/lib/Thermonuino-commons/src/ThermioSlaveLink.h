#pragma once

#include <Arduino.h>

#include "ThermioRfFrame.h"

class ThermioSlaveLink {
public:
  static constexpr uint8_t ConsoleOfflineAckFailures = 10;
  static constexpr uint16_t ConsoleOfflineRetryWatchdogTicks = 5400;

  explicit ThermioSlaveLink(uint8_t maxZone);

  void setLocalId(uint16_t id);
  uint16_t localId() const;

  void setConsoleId(uint16_t id);
  void clearConsoleId();
  bool consoleIdKnown() const;
  uint16_t consoleId() const;
  uint16_t reportTargetId() const;

  bool canLearnConsoleId(uint32_t nowMs, uint32_t learnWindowMs) const;
  bool learnConsoleIdFromResponse(uint16_t sourceId, uint32_t nowMs, uint32_t learnWindowMs);

  void markAckReceived(uint8_t assignedZone);
  void markAckMissed(uint32_t awakeWatchdogTicks);
  void forceRetryByUser();

  bool consoleOffline() const;
  bool offlineRetryDue(uint32_t awakeWatchdogTicks) const;
  bool autoReportDue(uint32_t awakeWatchdogTicks, uint32_t autoStartTick, uint16_t intervalTicks) const;
  void markReportAttemptStarted(uint32_t awakeWatchdogTicks);

  uint8_t nextPairZone();
  uint8_t pairZoneRequest() const;
  uint8_t assignedZone() const;

private:
  uint8_t maxZone_;
  uint16_t localId_ = ThermioRfFrame::BroadcastId;
  bool consoleIdKnown_ = false;
  uint16_t consoleId_ = ThermioRfFrame::BroadcastId;
  uint8_t consecutiveAckFailures_ = 0;
  bool consoleOffline_ = false;
  uint32_t consoleOfflineRetryAtWatchdogTick_ = 0;
  uint32_t lastReportAtWatchdogTick_ = 0;
  uint8_t assignedZone_ = 0;
  uint8_t pairZoneRequest_ = 0;
};
