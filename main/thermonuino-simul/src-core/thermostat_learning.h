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
  bool scheduleChanged;
  bool contradiction;
  bool candidateActive;
  int candidateHalf;
  uint8_t candidateCount;
};

class ThermostatLearning {
 public:
  void setup(double defaultTempC);
  void reset();

  LearningDecision evaluate(
      int absoluteSlot,
      int zone,
      double measuredTempC,
      int userTargetHalf,
      bool explicitUserAction,
      bool temporaryOverride,
      bool presenceDetected,
      bool replayOnly);

  int defaultTargetHalf() const { return defaultTargetHalf_; }

 private:
  struct SlotRule {
    int8_t targetHalf = kUnsetTempHalf;
    uint8_t confidence = 0;
    int8_t candidateHalf = kUnsetTempHalf;
    uint8_t candidateCount = 0;
    int32_t candidateLastAbsoluteSlot = -1000000;
  };

  struct ActiveRule {
    bool found = false;
    int slot = -1;
    int targetHalf = kUnsetTempHalf;
    uint8_t confidence = 0;
  };

  SlotRule rules_[kZones][kSlotsPerWeek];
  bool presence_[kZones][kSlotsPerWeek];
  int defaultTargetHalf_ = 34;

  static int normalizeSlot(int absoluteSlot);
  static int clampZone(int zone);
  static int halfFromCelsius(double tempC);
  static int dayFromSlot(int slotOfWeek);
  static int minuteOfDayFromSlot(int slotOfWeek);
  static bool sameHabit(int aHalf, int bHalf);

  ActiveRule findActiveRule(int zone, int slotOfWeek) const;
  bool dayHasAnyRule(int zone, int day) const;
  ActiveRule findSameDayRule(int zone, int slotOfWeek) const;
  ActiveRule findFallbackRule(int zone, int slotOfWeek) const;

  void reinforce(SlotRule& rule, uint8_t amount);
  void weaken(SlotRule& rule, uint8_t amount);
  void installRule(SlotRule& rule, int targetHalf, uint8_t confidence);
  bool recordExplicitObservation(
      SlotRule& currentSlotRule,
      SlotRule* activeRule,
      int targetHalf,
      int absoluteSlot,
      bool hadContradiction);
};

double celsiusFromHalf(int halfDegrees);

}  // namespace thermonuino
