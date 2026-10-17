#pragma once

#include <stdint.h>

#include <ThermioByteStorage.h>
#include <ThermioWeeklyProgram.h>

namespace thermonuino {

constexpr int kZones = 4;
constexpr int kSlotsPerDay = 96;
constexpr int kDaysPerWeek = 7;
constexpr int kSlotsPerWeek = kSlotsPerDay * kDaysPerWeek;
constexpr int kUnsetTempHalf = -128;

struct LearningDecision {
  int absoluteSlot;
  int slotOfWeek;
  int day;
  int hour;
  int minute;
  int zone;
  int targetHalf;
  int defaultTargetHalf;
  int sourceSlot;
  int sourceDay;
  bool hasProgramTarget;
  bool heating;
  bool idle;
  bool explicitUserAction;
  bool committedUserAction;
  bool temporaryOverrideActive;
  bool presenceDetected;
  bool previousPresenceDetected;
  bool doorOpened;
  uint8_t doorOpenHabit;
  bool scheduleChanged;
  int installedPowerW;
  int requestedPowerW;
  uint8_t workload;
};

class ThermostatLearning {
 public:
  void setup(double defaultTempC, ThermioByteStorage &storage);
  void reset();
  void loadTypicalWeek();

  LearningDecision evaluate(
      int absoluteSlot,
      int zone,
      double measuredTempC,
      int userVariationHalf,
      bool explicitUserAction,
      bool commitSetpoint,
      bool learningEnabled,
      bool presenceDetected,
      bool doorOpened,
      bool replayOnly);

  int defaultTargetHalf() const { return defaultTargetHalf_; }

 private:
  struct ActiveProgram {
    bool found = false;
    int slot = -1;
    int targetHalf = kUnsetTempHalf;
  };

  struct TemporarySetpoint {
    bool active = false;
    int8_t targetHalf = kUnsetTempHalf;
    int32_t startAbsoluteSlot = -1000000;
  };

  ThermioWeeklyProgram program_;
  bool presence_[kZones][kSlotsPerWeek];
  uint8_t doorOpenHabit_[kZones][kSlotsPerWeek];
  TemporarySetpoint temporary_[kZones];
  int defaultTargetHalf_ = 34;

  static int normalizeSlot(int absoluteSlot);
  static int clampZone(int zone);
  static int halfFromCelsius(double tempC);
  static int dayFromSlot(int slotOfWeek);
  static int minuteOfDayFromSlot(int slotOfWeek);

  ActiveProgram findProgramAtOrBefore(int zone, int absoluteSlot) const;
  bool installProgramPoint(int zone, int slotOfWeek, int targetHalf);
  bool hasProgramAfterTemporaryStart(int zone, int temporaryStartSlot, int absoluteSlot) const;
  bool temporaryApplies(int zone, int absoluteSlot, const ActiveProgram& active) const;
};

double celsiusFromHalf(int halfDegrees);

}  // namespace thermonuino
