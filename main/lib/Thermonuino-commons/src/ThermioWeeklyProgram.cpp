#include "ThermioWeeklyProgram.h"

ThermioWeeklyProgram::ThermioWeeklyProgram()
    : storage_(0), firstPointAddress_(0), zoneCount_(0), pointsPerDay_(0) {}

void ThermioWeeklyProgram::begin(ThermioByteStorage &storage,
                                 uint16_t firstPointAddress,
                                 uint8_t zoneCount,
                                 uint8_t pointsPerDay) {
  storage_ = &storage;
  firstPointAddress_ = firstPointAddress;
  zoneCount_ = zoneCount;
  pointsPerDay_ = pointsPerDay > MaxPointsPerDay ? MaxPointsPerDay : pointsPerDay;
}

ThermioWeeklyProgram::Selection ThermioWeeklyProgram::current(
    uint8_t zone,
    uint8_t day,
    uint8_t slotOfDay,
    int8_t defaultTargetHalfC,
    bool allowPreviousDays) const {
  Selection selection = {false, normalizeDay(day), UnsetSlot, defaultTargetHalfC};
  if (!validZone(zone)) {
    return selection;
  }

  const uint8_t normalizedDay = normalizeDay(day);
  if (dayHasProgram(zone, normalizedDay)) {
    selection = pointAtOrBefore(zone, normalizedDay, slotOfDay);
    if (!selection.found && allowPreviousDays) {
      selection = latestPoint(zone, normalizedDay);
    }
    return selection;
  }

  if (!allowPreviousDays) {
    return selection;
  }

  const uint8_t inheritedDay = referenceDay(zone);
  if (inheritedDay < DaysPerWeek) {
    selection = pointAtOrBefore(zone, inheritedDay, slotOfDay);
    if (!selection.found) {
      selection = latestPoint(zone, inheritedDay);
    }
  }
  return selection;
}

bool ThermioWeeklyProgram::next(uint8_t zone,
                                uint8_t day,
                                uint8_t slotOfDay,
                                int8_t defaultTargetHalfC,
                                Selection &selection,
                                uint16_t &delaySlots) const {
  if (!validZone(zone)) {
    return false;
  }

  const Selection active = current(zone, day, slotOfDay, defaultTargetHalfC, true);
  uint8_t candidateSlots[SlotsPerDay / 8] = {};
  candidateSlots[0] = 1;
  for (uint8_t candidateDay = 0; candidateDay < DaysPerWeek; candidateDay++) {
    for (uint8_t i = 0; i < pointsPerDay_; i++) {
      Point point;
      if (read(zone, candidateDay, i, point) && point.slotOfDay < SlotsPerDay) {
        candidateSlots[point.slotOfDay / 8] |= (uint8_t)(1U << (point.slotOfDay % 8));
      }
    }
  }

  for (uint16_t offset = 1; offset <= DaysPerWeek * SlotsPerDay; offset++) {
    const uint16_t future = (uint16_t)slotOfDay + offset;
    const uint8_t candidateSlot = future % SlotsPerDay;
    if ((candidateSlots[candidateSlot / 8] & (1U << (candidateSlot % 8))) == 0) {
      continue;
    }
    const uint8_t candidateDay = normalizeDay((int16_t)day + future / SlotsPerDay);
    const Selection candidate = current(
        zone, candidateDay, candidateSlot, defaultTargetHalfC, true);
    if (candidate.targetHalfC != active.targetHalfC) {
      selection = candidate;
      delaySlots = offset;
      return true;
    }
  }
  return false;
}

bool ThermioWeeklyProgram::save(uint8_t zone,
                                uint8_t day,
                                uint8_t slotOfDay,
                                int8_t targetHalfC) {
  if (!validZone(zone) || slotOfDay >= SlotsPerDay) {
    return false;
  }

  const uint8_t normalizedDay = normalizeDay(day);
  if (storage_->read(referenceDayAddress(zone)) >= DaysPerWeek) {
    const uint8_t inheritedDay = mostProgrammedDay(zone);
    storage_->update(referenceDayAddress(zone),
                     inheritedDay < DaysPerWeek ? inheritedDay : normalizedDay);
  }

  uint8_t targetIndex = pointsPerDay_;
  for (uint8_t i = 0; i < pointsPerDay_; i++) {
    Point point;
    if (!read(zone, normalizedDay, i, point)) {
      continue;
    }
    if (point.slotOfDay == slotOfDay) {
      if (point.targetHalfC == targetHalfC) {
        return false;
      }
      targetIndex = i;
      break;
    }
    if (targetIndex == pointsPerDay_ && point.slotOfDay == UnsetSlot) {
      targetIndex = i;
    }
  }
  if (targetIndex == pointsPerDay_) {
    targetIndex = pointsPerDay_ - 1;
  }
  const Point point = {slotOfDay, targetHalfC};
  return write(zone, normalizedDay, targetIndex, point);
}

bool ThermioWeeklyProgram::clearZone(uint8_t zone) {
  if (!validZone(zone)) {
    return false;
  }
  bool ok = true;
  const Point empty = {UnsetSlot, UnsetTargetHalfC};
  for (uint8_t day = 0; day < DaysPerWeek; day++) {
    for (uint8_t point = 0; point < pointsPerDay_; point++) {
      ok = write(zone, day, point, empty) && ok;
    }
  }
  return storage_->update(referenceDayAddress(zone), UnsetSlot) && ok;
}

bool ThermioWeeklyProgram::clearAll() {
  bool ok = true;
  for (uint8_t zone = 0; zone < zoneCount_; zone++) {
    ok = clearZone(zone) && ok;
  }
  return ok;
}

bool ThermioWeeklyProgram::point(uint8_t zone,
                                 uint8_t day,
                                 uint8_t index,
                                 Point &point) const {
  return read(zone, day, index, point);
}

bool ThermioWeeklyProgram::hasExplicitPointBetween(uint8_t zone,
                                                   int32_t startAbsoluteSlot,
                                                   int32_t endAbsoluteSlot) const {
  if (!validZone(zone) || endAbsoluteSlot <= startAbsoluteSlot) {
    return false;
  }
  const int32_t firstDay = floorDiv(startAbsoluteSlot + 1, SlotsPerDay);
  const int32_t lastDay = floorDiv(endAbsoluteSlot, SlotsPerDay);
  for (int32_t absoluteDay = firstDay; absoluteDay <= lastDay; absoluteDay++) {
    const uint8_t day = normalizeDay((int16_t)(absoluteDay % DaysPerWeek));
    const int32_t dayStart = absoluteDay * SlotsPerDay;
    for (uint8_t i = 0; i < pointsPerDay_; i++) {
      Point point;
      if (!read(zone, day, i, point) || point.slotOfDay == UnsetSlot) {
        continue;
      }
      const int32_t absolutePoint = dayStart + point.slotOfDay;
      if (absolutePoint > startAbsoluteSlot && absolutePoint <= endAbsoluteSlot) {
        return true;
      }
    }
  }
  return false;
}

uint8_t ThermioWeeklyProgram::referenceDay(uint8_t zone) const {
  if (!validZone(zone)) {
    return UnsetSlot;
  }
  const uint8_t storedDay = storage_->read(referenceDayAddress(zone));
  return storedDay < DaysPerWeek ? storedDay : mostProgrammedDay(zone);
}

uint16_t ThermioWeeklyProgram::referenceDayAddress(uint8_t zone) const {
  return firstPointAddress_ +
      (uint16_t)zoneCount_ * DaysPerWeek * pointsPerDay_ * 2U + zone;
}

bool ThermioWeeklyProgram::validZone(uint8_t zone) const {
  return storage_ && storage_->ready() && zone < zoneCount_ && pointsPerDay_ > 0;
}

bool ThermioWeeklyProgram::read(uint8_t zone,
                                uint8_t day,
                                uint8_t index,
                                Point &point) const {
  point = {UnsetSlot, UnsetTargetHalfC};
  if (!validZone(zone) || index >= pointsPerDay_) {
    return false;
  }
  const uint16_t address = pointAddress(zone, day, index);
  point.slotOfDay = storage_->read(address);
  point.targetHalfC = (int8_t)storage_->read(address + 1);
  return true;
}

bool ThermioWeeklyProgram::write(uint8_t zone,
                                 uint8_t day,
                                 uint8_t index,
                                 const Point &point) const {
  if (!validZone(zone) || index >= pointsPerDay_) {
    return false;
  }
  const uint16_t address = pointAddress(zone, day, index);
  const bool slotOk = storage_->update(address, point.slotOfDay);
  const bool targetOk = storage_->update(address + 1, (uint8_t)point.targetHalfC);
  return slotOk && targetOk;
}

uint16_t ThermioWeeklyProgram::pointAddress(uint8_t zone,
                                            uint8_t day,
                                            uint8_t index) const {
  return firstPointAddress_ +
      (((uint16_t)zone * DaysPerWeek + normalizeDay(day)) * pointsPerDay_ + index) * 2U;
}

bool ThermioWeeklyProgram::dayHasProgram(uint8_t zone, uint8_t day) const {
  return pointCount(zone, day) > 0;
}

uint8_t ThermioWeeklyProgram::pointCount(uint8_t zone, uint8_t day) const {
  uint8_t count = 0;
  for (uint8_t i = 0; i < pointsPerDay_; i++) {
    Point point;
    if (read(zone, day, i, point) && point.slotOfDay != UnsetSlot) {
      count++;
    }
  }
  return count;
}

uint8_t ThermioWeeklyProgram::mostProgrammedDay(uint8_t zone) const {
  uint8_t bestDay = UnsetSlot;
  uint8_t bestCount = 0;
  for (uint8_t day = 0; day < DaysPerWeek; day++) {
    const uint8_t count = pointCount(zone, day);
    if (count > bestCount) {
      bestDay = day;
      bestCount = count;
    }
  }
  return bestDay;
}

ThermioWeeklyProgram::Selection ThermioWeeklyProgram::pointAtOrBefore(
    uint8_t zone,
    uint8_t day,
    uint8_t slotOfDay) const {
  Selection best = {false, day, UnsetSlot, UnsetTargetHalfC};
  for (uint8_t i = 0; i < pointsPerDay_; i++) {
    Point point;
    if (read(zone, day, i, point) && point.slotOfDay != UnsetSlot &&
        point.slotOfDay <= slotOfDay && (!best.found || point.slotOfDay >= best.sourceSlot)) {
      best = {true, day, point.slotOfDay, point.targetHalfC};
    }
  }
  return best;
}

ThermioWeeklyProgram::Selection ThermioWeeklyProgram::latestPoint(uint8_t zone,
                                                                  uint8_t day) const {
  return pointAtOrBefore(zone, day, SlotsPerDay - 1);
}

uint8_t ThermioWeeklyProgram::normalizeDay(int16_t day) {
  int16_t normalized = day % DaysPerWeek;
  if (normalized < 0) {
    normalized += DaysPerWeek;
  }
  return (uint8_t)normalized;
}

int32_t ThermioWeeklyProgram::floorDiv(int32_t value, int32_t divisor) {
  if (value >= 0) {
    return value / divisor;
  }
  return -((-value + divisor - 1) / divisor);
}
