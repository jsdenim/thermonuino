#pragma once

#include <stdint.h>

#include "ThermioByteStorage.h"

class ThermioWeeklyProgram {
 public:
  static const uint8_t DaysPerWeek = 7;
  static const uint8_t SlotsPerDay = 96;
  static const uint8_t MaxPointsPerDay = 8;
  static const uint8_t UnsetSlot = 0xFF;
  static const int8_t UnsetTargetHalfC = -128;

  struct Point {
    uint8_t slotOfDay;
    int8_t targetHalfC;
  };

  struct Selection {
    bool found;
    uint8_t sourceDay;
    uint8_t sourceSlot;
    int8_t targetHalfC;
  };

  ThermioWeeklyProgram();

  void begin(ThermioByteStorage &storage,
             uint16_t firstPointAddress,
             uint8_t zoneCount,
             uint8_t pointsPerDay);

  Selection current(uint8_t zone,
                    uint8_t day,
                    uint8_t slotOfDay,
                    int8_t defaultTargetHalfC,
                    bool allowPreviousDays = true) const;

  bool next(uint8_t zone,
            uint8_t day,
            uint8_t slotOfDay,
            int8_t defaultTargetHalfC,
            Selection &selection,
            uint16_t &delaySlots) const;

  bool save(uint8_t zone, uint8_t day, uint8_t slotOfDay, int8_t targetHalfC);
  bool clearZone(uint8_t zone);
  bool clearAll();
  uint8_t referenceDay(uint8_t zone) const;
  uint16_t referenceDayAddress(uint8_t zone) const;
  bool hasExplicitPointBetween(uint8_t zone,
                               int32_t startAbsoluteSlot,
                               int32_t endAbsoluteSlot) const;

 private:
  ThermioByteStorage *storage_;
  uint16_t firstPointAddress_;
  uint8_t zoneCount_;
  uint8_t pointsPerDay_;

  bool validZone(uint8_t zone) const;
  bool read(uint8_t zone, uint8_t day, uint8_t index, Point &point) const;
  bool write(uint8_t zone, uint8_t day, uint8_t index, const Point &point) const;
  uint16_t pointAddress(uint8_t zone, uint8_t day, uint8_t index) const;
  bool dayHasProgram(uint8_t zone, uint8_t day) const;
  uint8_t pointCount(uint8_t zone, uint8_t day) const;
  uint8_t mostProgrammedDay(uint8_t zone) const;
  Selection pointAtOrBefore(uint8_t zone, uint8_t day, uint8_t slotOfDay) const;
  Selection latestPoint(uint8_t zone, uint8_t day) const;
  static uint8_t normalizeDay(int16_t day);
  static int32_t floorDiv(int32_t value, int32_t divisor);
};
