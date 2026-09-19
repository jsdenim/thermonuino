#pragma once

#include <stdint.h>

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
  uint8_t confidence;
  bool hasLearnedTarget;
  bool heating;
  bool idle;
  bool explicitUserAction;
  bool presenceDetected;
  bool previousPresenceDetected;
  bool doorOpened;
  uint8_t doorOpenHabit;
  bool scheduleChanged;
  bool contradiction;
  bool candidateActive;
  int candidateHalf;
  uint8_t candidateCount;
  int installedPowerW;
  int requestedPowerW;
  uint8_t workload;
};

class ThermostatLearning {
 public:
  void setup(double defaultTempC);
  void reset();

  LearningDecision evaluate(
      int absoluteSlot,
      int zone,
      double measuredTempC,
      int userVariationHalf,
      bool explicitUserAction,
      bool temporaryOverride,
      bool learningEnabled,
      bool presenceDetected,
      bool doorOpened,
      bool replayOnly);

  int defaultTargetHalf() const { return defaultTargetHalf_; }

 private:
  struct SlotRule {
    int8_t targetHalf = kUnsetTempHalf;
    uint8_t confidence = 0;
    bool explicitRule = false;
    bool exceptionRule = false;
    int8_t candidateHalf = kUnsetTempHalf;
    uint8_t candidateCount = 0;
    int32_t candidateLastAbsoluteSlot = -1000000;
  };

  struct ActiveRule {
    bool found = false;
    int slot = -1;
    int targetHalf = kUnsetTempHalf;
    uint8_t confidence = 0;
    bool explicitRule = false;
    bool exceptionRule = false;
  };

  struct UserOverride {
    bool active = false;
    int8_t targetHalf = kUnsetTempHalf;
    int32_t startAbsoluteSlot = -1000000;
    uint8_t confidence = 0;
  };

  SlotRule rules_[kZones][kSlotsPerWeek];
  bool presence_[kZones][kSlotsPerWeek];
  uint8_t doorOpenHabit_[kZones][kSlotsPerWeek];
  UserOverride userOverrides_[kZones];
  int defaultTargetHalf_ = 34;

  static int normalizeSlot(int absoluteSlot);
  static int clampZone(int zone);
  static int halfFromCelsius(double tempC);
  static int dayFromSlot(int slotOfWeek);
  static int minuteOfDayFromSlot(int slotOfWeek);
  static bool sameHabit(int aHalf, int bHalf);

  ActiveRule findActiveRule(int zone, int absoluteSlot) const;
  ActiveRule findResolvedRuleForDay(
      int zone,
      int absoluteDay,
      int slotOfDay,
      int remainingDays,
      bool includeExceptions) const;
  ActiveRule findRuleInDayAtOrBefore(
      int zone,
      int day,
      int slotOfDay,
      bool includeExceptions) const;

  void reinforce(SlotRule& rule, uint8_t amount);
  void weaken(SlotRule& rule, uint8_t amount);
  void installRule(SlotRule& rule, int targetHalf, uint8_t confidence, bool explicitRule, bool exceptionRule);
  bool recordPassiveConfirmation(SlotRule& currentSlotRule, int targetHalf, int absoluteSlot);
  bool recordExplicitObservation(
      SlotRule& currentSlotRule,
      SlotRule* activeRule,
      uint8_t activeConfidence,
      bool forceException,
      int targetHalf,
      int absoluteSlot,
      bool hadContradiction);
};

double celsiusFromHalf(int halfDegrees);

}  // namespace thermonuino
