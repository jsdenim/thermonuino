#include "thermostat_learning.h"

#include <algorithm>
#include <cmath>

#include <ThermioStorageLayout.h>

namespace thermonuino {

namespace {

constexpr int kTemporaryOverrideSlots = 8;
constexpr int kTypicalMorningSlot = 6 * 4 + 2;
constexpr int kTypicalEveningSlot = 16 * 4 + 2;
constexpr int kTypicalNightSlot = 21 * 4;
constexpr int kTypicalMorningHalf = 39;
constexpr int kTypicalEveningHalf = 41;
constexpr int kTypicalNightHalf = 35;
constexpr uint8_t kDoorOpenHabitMax = 12;
constexpr int kInstalledPowerW = 2100;
constexpr int kMaxMaintenancePowerW = kInstalledPowerW;
constexpr int kHoldPowerW = 900;
constexpr int kDoorOpenAnticipationPowerStepW = 120;

}  // namespace

void ThermostatLearning::setup(double defaultTempC, ThermioByteStorage &storage) {
  defaultTargetHalf_ = halfFromCelsius(defaultTempC);
  program_.begin(storage,
                 ThermioStorageLayout::ScheduleFirstPointAddress,
                 kZones,
                 ThermioStorageLayout::SchedulePointsPerDay);
  reset();
}

void ThermostatLearning::reset() {
  program_.clearAll();
  for (int zone = 0; zone < kZones; zone++) {
    for (int slot = 0; slot < kSlotsPerWeek; slot++) {
      presence_[zone][slot] = false;
      doorOpenHabit_[zone][slot] = 0;
    }
    temporary_[zone] = TemporarySetpoint{};
  }
}

void ThermostatLearning::loadTypicalWeek() {
  program_.clearAll();
  for (int zone = 0; zone < kZones; zone++) {
    temporary_[zone] = TemporarySetpoint{};
    for (int day = 0; day < kDaysPerWeek; day++) {
      const int dayStart = day * kSlotsPerDay;
      installProgramPoint(zone, dayStart + kTypicalMorningSlot, kTypicalMorningHalf);
      installProgramPoint(zone, dayStart + kTypicalEveningSlot, kTypicalEveningHalf);
      installProgramPoint(zone, dayStart + kTypicalNightSlot, kTypicalNightHalf);
    }
  }
}

uint8_t ThermostatLearning::referenceDay(int zone) const {
  return program_.referenceDay(clampZone(zone));
}

uint16_t ThermostatLearning::referenceDayAddress(int zone) const {
  return program_.referenceDayAddress(clampZone(zone));
}

LearningDecision ThermostatLearning::evaluate(
    int absoluteSlot,
    int zone,
    double measuredTempC,
    int userVariationHalf,
    bool explicitUserAction,
    bool commitSetpoint,
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

  ActiveProgram active = findProgramAtOrBefore(zone, absoluteSlot);
  int targetHalf = active.found ? active.targetHalf : defaultTargetHalf_;
  bool changed = false;
  bool temporaryActive = false;
  const bool userAction = explicitUserAction && userVariationHalf != 0;
  const bool commitAction = explicitUserAction && commitSetpoint;
  const int requestedTargetHalf = targetHalf + userVariationHalf;

  if (!learningEnabled) {
    targetHalf = requestedTargetHalf;
    if (!replayOnly && userAction) {
      temporary_[zone] = TemporarySetpoint{};
    }
  } else if (!replayOnly && commitAction) {
    const int committedTargetHalf = userAction
        ? requestedTargetHalf
        : (temporary_[zone].active ? temporary_[zone].targetHalf : targetHalf);
    changed = installProgramPoint(zone, slotOfWeek, committedTargetHalf);
    temporary_[zone] = TemporarySetpoint{
        true,
        static_cast<int8_t>(committedTargetHalf),
        absoluteSlot};
    targetHalf = committedTargetHalf;
    active = findProgramAtOrBefore(zone, absoluteSlot);
  } else if (!replayOnly && userAction) {
    temporary_[zone] = TemporarySetpoint{
        true,
        static_cast<int8_t>(requestedTargetHalf),
        absoluteSlot};
    targetHalf = requestedTargetHalf;
    temporaryActive = true;
  } else if (temporaryApplies(zone, absoluteSlot, active)) {
    targetHalf = temporary_[zone].targetHalf;
    temporaryActive = true;
  } else if (!replayOnly && temporary_[zone].active) {
    temporary_[zone] = TemporarySetpoint{};
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

  LearningDecision decision{};
  decision.absoluteSlot = absoluteSlot;
  decision.slotOfWeek = slotOfWeek;
  decision.day = day;
  decision.hour = minutes / 60;
  decision.minute = minutes % 60;
  decision.zone = zone;
  decision.targetHalf = targetHalf;
  ThermioWeeklyProgram::Selection nextSelection;
  uint16_t nextDelaySlots = 0;
  decision.hasNextProgramTarget = program_.next(
      zone,
      day,
      slotOfWeek % kSlotsPerDay,
      (int8_t)defaultTargetHalf_,
      nextSelection,
      nextDelaySlots);
  decision.nextTargetHalf = decision.hasNextProgramTarget ?
      nextSelection.targetHalfC : targetHalf;
  decision.nextTargetDelaySlots = decision.hasNextProgramTarget ? nextDelaySlots : 0;
  decision.defaultTargetHalf = defaultTargetHalf_;
  decision.sourceSlot = active.found ? active.slot : -1;
  decision.sourceDay = active.found ? dayFromSlot(active.slot) : -1;
  decision.hasProgramTarget = active.found;
  decision.heating = heating;
  decision.idle = idle;
  decision.explicitUserAction = userAction || commitAction;
  decision.committedUserAction = commitAction && learningEnabled;
  decision.temporaryOverrideActive = temporaryActive;
  decision.presenceDetected = presence_[zone][slotOfWeek];
  decision.previousPresenceDetected = previousPresence;
  decision.doorOpened = doorOpened && !replayOnly;
  decision.doorOpenHabit = doorOpenHabit_[zone][slotOfWeek];
  decision.scheduleChanged = changed;
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

ThermostatLearning::ActiveProgram ThermostatLearning::findProgramAtOrBefore(
    int zone,
    int absoluteSlot) const {
  const int slotOfWeek = normalizeSlot(absoluteSlot);
  const int slotOfDay = slotOfWeek % kSlotsPerDay;
  const int absoluteDay = absoluteSlot >= 0
      ? absoluteSlot / kSlotsPerDay
      : -(((-absoluteSlot) + kSlotsPerDay - 1) / kSlotsPerDay);

  const uint8_t day = (uint8_t)(((absoluteDay % kDaysPerWeek) + kDaysPerWeek) % kDaysPerWeek);
  const ThermioWeeklyProgram::Selection selection = program_.current(
      zone,
      day,
      slotOfDay,
      (int8_t)defaultTargetHalf_,
      absoluteDay > 0);
  if (!selection.found) {
    return ActiveProgram{};
  }
  return ActiveProgram{
      true,
      (int)selection.sourceDay * kSlotsPerDay + selection.sourceSlot,
      selection.targetHalfC};
}

bool ThermostatLearning::installProgramPoint(int zone, int slotOfWeek, int targetHalf) {
  const int normalizedSlot = normalizeSlot(slotOfWeek);
  return program_.save(
      zone,
      normalizedSlot / kSlotsPerDay,
      normalizedSlot % kSlotsPerDay,
      (int8_t)targetHalf);
}

bool ThermostatLearning::hasProgramAfterTemporaryStart(
    int zone,
    int temporaryStartSlot,
    int absoluteSlot) const {
  const int lastSlot = std::min(absoluteSlot, temporaryStartSlot + kSlotsPerWeek);
  return program_.hasExplicitPointBetween(zone, temporaryStartSlot, lastSlot);
}

bool ThermostatLearning::temporaryApplies(
    int zone,
    int absoluteSlot,
    const ActiveProgram& active) const {
  const TemporarySetpoint& temporary = temporary_[zone];
  if (!temporary.active) {
    return false;
  }

  const int elapsedSlots = absoluteSlot - temporary.startAbsoluteSlot;
  if (elapsedSlots < 0) {
    return false;
  }
  if (hasProgramAfterTemporaryStart(zone, temporary.startAbsoluteSlot, absoluteSlot)) {
    return false;
  }
  if (elapsedSlots < kTemporaryOverrideSlots) {
    return true;
  }

  return !active.found;
}

double celsiusFromHalf(int halfDegrees) {
  return halfDegrees / 2.0;
}

}  // namespace thermonuino
