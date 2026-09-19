#include "thermostat_learning.h"

#include <algorithm>
#include <cmath>

namespace thermonuino {

namespace {

constexpr uint8_t kConfidenceExplicitInitial = 8;
constexpr uint8_t kConfidenceExplicitBoost = 2;
constexpr uint8_t kConfidencePassiveBoost = 1;
constexpr uint8_t kConfidenceContradictionPenalty = 1;
constexpr uint8_t kConfidenceReplace = 6;
constexpr uint8_t kConfidenceStable = 10;
constexpr uint8_t kConfidenceMax = 12;
constexpr int kCandidateFreshSlots = kSlotsPerWeek * 3;
constexpr int kCandidateRepeatMinSlots = kSlotsPerWeek - kSlotsPerDay;
constexpr int kUserOverrideSlots = 8;
constexpr uint8_t kDoorOpenHabitMax = 12;
constexpr int kInstalledPowerW = 7000;
constexpr int kMinMaintenancePowerW = 500;
constexpr int kMaxMaintenancePowerW = 6000;
constexpr int kHoldPowerW = 900;
constexpr int kDoorOpenAnticipationPowerStepW = 120;

}  // namespace

void ThermostatLearning::setup(double defaultTempC) {
  defaultTargetHalf_ = halfFromCelsius(defaultTempC);
  reset();
}

void ThermostatLearning::reset() {
  for (int zone = 0; zone < kZones; zone++) {
    for (int slot = 0; slot < kSlotsPerWeek; slot++) {
      rules_[zone][slot] = SlotRule{};
      presence_[zone][slot] = false;
      doorOpenHabit_[zone][slot] = 0;
    }
    userOverrides_[zone] = UserOverride{};
  }
}

LearningDecision ThermostatLearning::evaluate(
    int absoluteSlot,
    int zone,
    double measuredTempC,
    int userVariationHalf,
    bool explicitUserAction,
    bool temporaryOverride,
    bool learningEnabled,
    bool presenceDetected,
    bool doorOpened,
    bool replayOnly) {
  zone = clampZone(zone);
  const int slotOfWeek = normalizeSlot(absoluteSlot);
  const int day = dayFromSlot(slotOfWeek);
  const int minutes = minuteOfDayFromSlot(slotOfWeek);
  const bool previousPresence = presence_[zone][slotOfWeek];

  if (!replayOnly) {
    presence_[zone][slotOfWeek] = presenceDetected;
    uint8_t& habit = doorOpenHabit_[zone][slotOfWeek];
    if (doorOpened) {
      habit = std::min<uint8_t>(kDoorOpenHabitMax, habit + 1);
    } else if (habit > 0) {
      habit--;
    }
  }

  ActiveRule active = findActiveRule(zone, absoluteSlot);
  int targetHalf = active.found ? active.targetHalf : defaultTargetHalf_;
  const int userTargetHalf = targetHalf + userVariationHalf;
  bool changed = false;
  bool contradiction = false;
  bool userOverrideActive = false;

  if (!explicitUserAction && userOverrides_[zone].active) {
    const UserOverride& override = userOverrides_[zone];
    const int elapsedSlots = absoluteSlot - override.startAbsoluteSlot;
    if (elapsedSlots >= 0 && elapsedSlots < kUserOverrideSlots) {
      userOverrideActive = true;
    } else if (!replayOnly) {
      const bool habitualSame =
          active.found && sameHabit(active.targetHalf, override.targetHalf);
      const bool scheduledTransition =
          active.found && active.slot == slotOfWeek && !habitualSame;
      const bool habitualStronger =
          active.found &&
          !habitualSame &&
          active.confidence > override.confidence;
      if (habitualSame || scheduledTransition || habitualStronger) {
        userOverrides_[zone] = UserOverride{};
      } else {
        userOverrideActive = true;
      }
    }
  }

  if (!learningEnabled) {
    targetHalf = defaultTargetHalf_ + userVariationHalf;
    userOverrides_[zone] = UserOverride{};
  } else if (!replayOnly && explicitUserAction && !temporaryOverride) {
    userOverrides_[zone].active = true;
    userOverrides_[zone].targetHalf = static_cast<int8_t>(userTargetHalf);
    userOverrides_[zone].startAbsoluteSlot = absoluteSlot;
    userOverrides_[zone].confidence = kConfidenceExplicitInitial;

    SlotRule& currentSlotRule = rules_[zone][slotOfWeek];
    SlotRule* activeRule = active.found && active.slot == slotOfWeek ? &rules_[zone][active.slot] : nullptr;
    contradiction = active.found && userTargetHalf != active.targetHalf;
    const bool activeFromAnotherDay = active.found && dayFromSlot(active.slot) != day;
    const bool forceException =
        contradiction && (activeFromAnotherDay || active.exceptionRule || !active.explicitRule);
    changed = recordExplicitObservation(
        currentSlotRule,
        activeRule,
        active.found ? active.confidence : 0,
        forceException,
        userTargetHalf,
        absoluteSlot,
        contradiction);
    if (forceException && active.found && active.slot != slotOfWeek) {
      const int activeTransition = active.slot % kSlotsPerDay;
      const int currentSlotOfDay = slotOfWeek % kSlotsPerDay;
      if (activeTransition <= currentSlotOfDay) {
        const int dayStart = day * kSlotsPerDay;
        int backfillStart = activeTransition;
        if (active.exceptionRule) {
          for (int scan = activeTransition; scan >= 0; scan--) {
            const SlotRule& scanRule = rules_[zone][dayStart + scan];
            if (scanRule.exceptionRule) {
              backfillStart = scan;
            } else if (scanRule.targetHalf != kUnsetTempHalf) {
              break;
            }
          }
        }

        for (int fill = backfillStart; fill <= currentSlotOfDay; fill++) {
          SlotRule& transitionRule = rules_[zone][dayStart + fill];
          if (fill == backfillStart || transitionRule.exceptionRule) {
            if (!transitionRule.explicitRule || transitionRule.exceptionRule) {
              installRule(
                  transitionRule,
                  userTargetHalf,
                  kConfidenceExplicitInitial,
                  true,
                  true);
              changed = true;
            }
          }
        }
      }
    }

    active = findActiveRule(zone, absoluteSlot);
    targetHalf = userTargetHalf;
  } else if (userOverrideActive) {
    targetHalf = userOverrides_[zone].targetHalf;
  } else if (!replayOnly && !temporaryOverride && active.found) {
    SlotRule& currentSlotRule = rules_[zone][slotOfWeek];
    if (active.slot == slotOfWeek) {
      reinforce(currentSlotRule, kConfidencePassiveBoost);
      active.confidence = currentSlotRule.confidence;
    } else {
      const bool sameTransitionTime = (active.slot % kSlotsPerDay) == (slotOfWeek % kSlotsPerDay);
      if (currentSlotRule.targetHalf != kUnsetTempHalf || sameTransitionTime) {
        changed = recordPassiveConfirmation(currentSlotRule, active.targetHalf, absoluteSlot);
        if (changed) {
          active = findActiveRule(zone, absoluteSlot);
        }
      }
    }
  }

  if (temporaryOverride && learningEnabled) {
    targetHalf = userTargetHalf;
  }

  const double targetC = celsiusFromHalf(targetHalf);
  const double tempErrorC = targetC - measuredTempC;
  const bool heating = measuredTempC <= targetC + 0.1;
  const bool idle = measuredTempC > targetC + 0.2;
  int requestedPowerW = 0;
  if (tempErrorC > 0.2) {
    requestedPowerW = static_cast<int>(tempErrorC * 1800.0);
    requestedPowerW = std::max(kHoldPowerW, requestedPowerW);
    requestedPowerW = std::min(kMaxMaintenancePowerW, requestedPowerW);
  } else if (heating) {
    requestedPowerW = kHoldPowerW;
  }
  const int anticipationReduction =
      static_cast<int>(doorOpenHabit_[zone][slotOfWeek]) * kDoorOpenAnticipationPowerStepW;
  requestedPowerW = std::max(0, requestedPowerW - anticipationReduction);
  const uint8_t workload = static_cast<uint8_t>(
      std::min(255, std::max(0, (requestedPowerW * 255 + kInstalledPowerW / 2) / kInstalledPowerW)));

  SlotRule& slotRule = rules_[zone][slotOfWeek];
  LearningDecision decision{};
  decision.absoluteSlot = absoluteSlot;
  decision.slotOfWeek = slotOfWeek;
  decision.day = day;
  decision.hour = minutes / 60;
  decision.minute = minutes % 60;
  decision.zone = zone;
  decision.targetHalf = targetHalf;
  decision.defaultTargetHalf = defaultTargetHalf_;
  decision.sourceSlot = active.found ? active.slot : -1;
  decision.sourceDay = active.found ? dayFromSlot(active.slot) : -1;
  decision.confidence = active.found ? active.confidence : 0;
  decision.hasLearnedTarget = active.found;
  decision.heating = heating;
  decision.idle = idle;
  decision.explicitUserAction = explicitUserAction && !temporaryOverride && learningEnabled;
  decision.presenceDetected = presence_[zone][slotOfWeek];
  decision.previousPresenceDetected = previousPresence;
  decision.doorOpened = doorOpened && !replayOnly;
  decision.doorOpenHabit = doorOpenHabit_[zone][slotOfWeek];
  decision.scheduleChanged = changed;
  decision.contradiction = contradiction;
  decision.candidateActive = slotRule.candidateHalf != kUnsetTempHalf;
  decision.candidateHalf = slotRule.candidateHalf;
  decision.candidateCount = slotRule.candidateCount;
  decision.installedPowerW = kInstalledPowerW;
  decision.requestedPowerW = requestedPowerW;
  decision.workload = workload;
  return decision;
}

int ThermostatLearning::normalizeSlot(int absoluteSlot) {
  int slot = absoluteSlot % kSlotsPerWeek;
  if (slot < 0) {
    slot += kSlotsPerWeek;
  }
  return slot;
}

int ThermostatLearning::clampZone(int zone) {
  return std::min(kZones - 1, std::max(0, zone));
}

int ThermostatLearning::halfFromCelsius(double tempC) {
  return static_cast<int>(std::lround(tempC * 2.0));
}

int ThermostatLearning::dayFromSlot(int slotOfWeek) {
  return slotOfWeek / kSlotsPerDay;
}

int ThermostatLearning::minuteOfDayFromSlot(int slotOfWeek) {
  return (slotOfWeek % kSlotsPerDay) * 15;
}

bool ThermostatLearning::sameHabit(int aHalf, int bHalf) {
  return std::abs(aHalf - bHalf) <= 1;
}

ThermostatLearning::ActiveRule ThermostatLearning::findActiveRule(int zone, int absoluteSlot) const {
  const int slotOfWeek = normalizeSlot(absoluteSlot);
  const int day = dayFromSlot(slotOfWeek);
  const int slotOfDay = slotOfWeek % kSlotsPerDay;
  const int absoluteDay = absoluteSlot >= 0
      ? absoluteSlot / kSlotsPerDay
      : -(((-absoluteSlot) + kSlotsPerDay - 1) / kSlotsPerDay);
  return findResolvedRuleForDay(zone, absoluteDay, slotOfDay, kDaysPerWeek, true);
}

ThermostatLearning::ActiveRule ThermostatLearning::findResolvedRuleForDay(
    int zone,
    int absoluteDay,
    int slotOfDay,
    int remainingDays,
    bool includeExceptions) const {
  if (remainingDays <= 0) {
    return ActiveRule{};
  }

  int day = absoluteDay % kDaysPerWeek;
  if (day < 0) {
    day += kDaysPerWeek;
  }
  ActiveRule sameDay = findRuleInDayAtOrBefore(zone, day, slotOfDay, includeExceptions);

  if (absoluteDay <= 0) {
    return sameDay;
  }

  ActiveRule inherited = findResolvedRuleForDay(
      zone,
      absoluteDay - 1,
      slotOfDay,
      remainingDays - 1,
      false);
  if (!sameDay.found) {
    if (!inherited.found) {
      inherited = findResolvedRuleForDay(
          zone,
          absoluteDay - 1,
          kSlotsPerDay - 1,
          remainingDays - 1,
          false);
    }
    return inherited;
  }
  if (!inherited.found) {
    return sameDay;
  }

  const int sameDayTransition = sameDay.slot % kSlotsPerDay;
  const int inheritedTransition = inherited.slot % kSlotsPerDay;
  if (inheritedTransition > sameDayTransition && inheritedTransition <= slotOfDay) {
    return inherited;
  }

  return sameDay;
}

ThermostatLearning::ActiveRule ThermostatLearning::findRuleInDayAtOrBefore(
    int zone,
    int day,
    int slotOfDay,
    bool includeExceptions) const {
  const int dayStart = day * kSlotsPerDay;
  for (int slot = dayStart + slotOfDay; slot >= dayStart; slot--) {
    const SlotRule& rule = rules_[zone][slot];
    if (rule.targetHalf != kUnsetTempHalf && (includeExceptions || !rule.exceptionRule)) {
      return ActiveRule{
          true,
          slot,
          rule.targetHalf,
          rule.confidence,
          rule.explicitRule,
          rule.exceptionRule};
    }
  }
  return ActiveRule{};
}

void ThermostatLearning::reinforce(SlotRule& rule, uint8_t amount) {
  if (rule.targetHalf == kUnsetTempHalf) {
    return;
  }
  rule.confidence = std::min<uint8_t>(kConfidenceMax, rule.confidence + amount);
}

void ThermostatLearning::weaken(SlotRule& rule, uint8_t amount) {
  if (rule.targetHalf == kUnsetTempHalf) {
    return;
  }
  rule.confidence = rule.confidence > amount ? static_cast<uint8_t>(rule.confidence - amount) : 0;
}

void ThermostatLearning::installRule(
    SlotRule& rule,
    int targetHalf,
    uint8_t confidence,
    bool explicitRule,
    bool exceptionRule) {
  rule.targetHalf = static_cast<int8_t>(targetHalf);
  rule.confidence = std::min<uint8_t>(kConfidenceMax, confidence);
  rule.explicitRule = explicitRule;
  rule.exceptionRule = exceptionRule;
  rule.candidateHalf = kUnsetTempHalf;
  rule.candidateCount = 0;
  rule.candidateLastAbsoluteSlot = -1000000;
}

bool ThermostatLearning::recordPassiveConfirmation(
    SlotRule& currentSlotRule,
    int targetHalf,
    int absoluteSlot) {
  (void)absoluteSlot;

  if (currentSlotRule.targetHalf != kUnsetTempHalf) {
    if (sameHabit(currentSlotRule.targetHalf, targetHalf)) {
      reinforce(currentSlotRule, kConfidenceExplicitBoost);
    }
    return false;
  }

  installRule(currentSlotRule, targetHalf, kConfidenceExplicitInitial, false, false);
  return true;
}

bool ThermostatLearning::recordExplicitObservation(
    SlotRule& currentSlotRule,
    SlotRule* activeRule,
    uint8_t activeConfidence,
    bool forceException,
    int targetHalf,
    int absoluteSlot,
    bool hadContradiction) {
  if (currentSlotRule.targetHalf == kUnsetTempHalf && activeRule == nullptr) {
    installRule(currentSlotRule, targetHalf, kConfidenceExplicitInitial, true, forceException);
    return true;
  }

  if (currentSlotRule.targetHalf != kUnsetTempHalf && sameHabit(currentSlotRule.targetHalf, targetHalf)) {
    currentSlotRule.targetHalf = static_cast<int8_t>(targetHalf);
    currentSlotRule.explicitRule = true;
    reinforce(currentSlotRule, kConfidenceExplicitBoost);
    currentSlotRule.candidateHalf = kUnsetTempHalf;
    currentSlotRule.candidateCount = 0;
    return false;
  }

  if (!hadContradiction) {
    installRule(currentSlotRule, targetHalf, kConfidenceExplicitInitial, true, false);
    return true;
  }

  if (currentSlotRule.targetHalf != kUnsetTempHalf && !currentSlotRule.explicitRule) {
    installRule(currentSlotRule, targetHalf, kConfidenceExplicitInitial, true, true);
    return true;
  }

  if (activeRule != nullptr) {
    weaken(*activeRule, kConfidenceContradictionPenalty);
  }

  if (currentSlotRule.targetHalf == kUnsetTempHalf && activeConfidence < kConfidenceStable) {
    installRule(currentSlotRule, targetHalf, kConfidenceExplicitInitial, true, false);
    return true;
  }

  const int candidateAge = absoluteSlot - currentSlotRule.candidateLastAbsoluteSlot;
  const bool sameCandidate =
      currentSlotRule.candidateHalf != kUnsetTempHalf &&
      sameHabit(currentSlotRule.candidateHalf, targetHalf) &&
      candidateAge >= 0 &&
      candidateAge <= kCandidateFreshSlots;
  const bool separateObservation = candidateAge >= kCandidateRepeatMinSlots;

  if (sameCandidate && separateObservation) {
    currentSlotRule.candidateCount = std::min<uint8_t>(3, currentSlotRule.candidateCount + 1);
  } else if (sameCandidate) {
    currentSlotRule.candidateCount = std::max<uint8_t>(1, currentSlotRule.candidateCount);
  } else {
    currentSlotRule.candidateHalf = static_cast<int8_t>(targetHalf);
    currentSlotRule.candidateCount = 1;
  }
  currentSlotRule.candidateLastAbsoluteSlot = absoluteSlot;

  if (currentSlotRule.candidateCount >= 2 || activeConfidence <= 2) {
    installRule(currentSlotRule, targetHalf, kConfidenceReplace, true, activeConfidence >= kConfidenceStable);
    return true;
  }

  return false;
}

double celsiusFromHalf(int halfDegrees) {
  return halfDegrees / 2.0;
}

}  // namespace thermonuino
