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
constexpr int kUserOverrideSlots = 8;

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
    }
    userOverrides_[zone] = UserOverride{};
  }
}

LearningDecision ThermostatLearning::evaluate(
    int absoluteSlot,
    int zone,
    double measuredTempC,
    int userTargetHalf,
    bool explicitUserAction,
    bool temporaryOverride,
    bool presenceDetected,
    bool replayOnly) {
  zone = clampZone(zone);
  const int slotOfWeek = normalizeSlot(absoluteSlot);
  const int day = dayFromSlot(slotOfWeek);
  const int minutes = minuteOfDayFromSlot(slotOfWeek);
  const bool previousPresence = presence_[zone][slotOfWeek];

  if (!replayOnly) {
    presence_[zone][slotOfWeek] = presenceDetected;
  }

  ActiveRule active = findActiveRule(zone, slotOfWeek);
  int targetHalf = active.found ? active.targetHalf : defaultTargetHalf_;
  bool changed = false;
  bool contradiction = false;
  bool userOverrideActive = false;

  if (!replayOnly && !explicitUserAction && userOverrides_[zone].active) {
    const UserOverride& override = userOverrides_[zone];
    const int elapsedSlots = absoluteSlot - override.startAbsoluteSlot;
    if (elapsedSlots >= 0 && elapsedSlots < kUserOverrideSlots) {
      userOverrideActive = true;
    } else {
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

  if (!replayOnly && explicitUserAction && !temporaryOverride) {
    userOverrides_[zone].active = true;
    userOverrides_[zone].targetHalf = static_cast<int8_t>(userTargetHalf);
    userOverrides_[zone].startAbsoluteSlot = absoluteSlot;
    userOverrides_[zone].confidence = kConfidenceExplicitInitial;

    SlotRule& currentSlotRule = rules_[zone][slotOfWeek];
    SlotRule* activeRule = active.found ? &rules_[zone][active.slot] : nullptr;
    contradiction = active.found && !sameHabit(userTargetHalf, active.targetHalf);
    changed = recordExplicitObservation(
        currentSlotRule,
        activeRule,
        userTargetHalf,
        absoluteSlot,
        contradiction);

    active = findActiveRule(zone, slotOfWeek);
    targetHalf = userTargetHalf;
  } else if (userOverrideActive) {
    targetHalf = userOverrides_[zone].targetHalf;
  } else if (!replayOnly && !temporaryOverride && active.found && active.slot == slotOfWeek) {
    reinforce(rules_[zone][active.slot], kConfidencePassiveBoost);
    active.confidence = rules_[zone][active.slot].confidence;
  }

  if (temporaryOverride) {
    targetHalf = userTargetHalf;
  }

  const double targetC = celsiusFromHalf(targetHalf);
  const bool heating = measuredTempC < targetC - 0.2;
  const bool idle = measuredTempC > targetC + 0.2;

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
  decision.explicitUserAction = explicitUserAction && !temporaryOverride;
  decision.presenceDetected = presence_[zone][slotOfWeek];
  decision.previousPresenceDetected = previousPresence;
  decision.scheduleChanged = changed;
  decision.contradiction = contradiction;
  decision.candidateActive = slotRule.candidateHalf != kUnsetTempHalf;
  decision.candidateHalf = slotRule.candidateHalf;
  decision.candidateCount = slotRule.candidateCount;
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

ThermostatLearning::ActiveRule ThermostatLearning::findActiveRule(int zone, int slotOfWeek) const {
  for (int distance = 0; distance < kSlotsPerWeek; distance++) {
    int slot = slotOfWeek - distance;
    if (slot < 0) {
      slot += kSlotsPerWeek;
    }
    const SlotRule& rule = rules_[zone][slot];
    if (rule.targetHalf != kUnsetTempHalf) {
      return ActiveRule{true, slot, rule.targetHalf, rule.confidence};
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

void ThermostatLearning::installRule(SlotRule& rule, int targetHalf, uint8_t confidence) {
  rule.targetHalf = static_cast<int8_t>(targetHalf);
  rule.confidence = std::min<uint8_t>(kConfidenceMax, confidence);
  rule.candidateHalf = kUnsetTempHalf;
  rule.candidateCount = 0;
  rule.candidateLastAbsoluteSlot = -1000000;
}

bool ThermostatLearning::recordExplicitObservation(
    SlotRule& currentSlotRule,
    SlotRule* activeRule,
    int targetHalf,
    int absoluteSlot,
    bool hadContradiction) {
  if (currentSlotRule.targetHalf == kUnsetTempHalf && activeRule == nullptr) {
    installRule(currentSlotRule, targetHalf, kConfidenceExplicitInitial);
    return true;
  }

  if (currentSlotRule.targetHalf != kUnsetTempHalf && sameHabit(currentSlotRule.targetHalf, targetHalf)) {
    currentSlotRule.targetHalf = static_cast<int8_t>(targetHalf);
    reinforce(currentSlotRule, kConfidenceExplicitBoost);
    currentSlotRule.candidateHalf = kUnsetTempHalf;
    currentSlotRule.candidateCount = 0;
    return false;
  }

  if (!hadContradiction) {
    installRule(currentSlotRule, targetHalf, kConfidenceExplicitInitial);
    return true;
  }

  if (activeRule != nullptr) {
    weaken(*activeRule, kConfidenceContradictionPenalty);
  }

  const uint8_t activeConfidence = activeRule != nullptr ? activeRule->confidence : 0;
  if (currentSlotRule.targetHalf == kUnsetTempHalf && activeConfidence < kConfidenceStable) {
    installRule(currentSlotRule, targetHalf, kConfidenceExplicitInitial);
    return true;
  }

  const bool sameCandidate =
      currentSlotRule.candidateHalf != kUnsetTempHalf &&
      sameHabit(currentSlotRule.candidateHalf, targetHalf) &&
      absoluteSlot - currentSlotRule.candidateLastAbsoluteSlot <= kCandidateFreshSlots;

  if (sameCandidate) {
    currentSlotRule.candidateCount = std::min<uint8_t>(3, currentSlotRule.candidateCount + 1);
  } else {
    currentSlotRule.candidateHalf = static_cast<int8_t>(targetHalf);
    currentSlotRule.candidateCount = 1;
  }
  currentSlotRule.candidateLastAbsoluteSlot = absoluteSlot;

  if (currentSlotRule.candidateCount >= 2 || activeConfidence <= 2) {
    installRule(currentSlotRule, targetHalf, kConfidenceReplace);
    return true;
  }

  return false;
}

double celsiusFromHalf(int halfDegrees) {
  return halfDegrees / 2.0;
}

}  // namespace thermonuino
