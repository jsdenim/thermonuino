#pragma once

#include <stdint.h>

namespace ThermioStorageLayout {

static const uint16_t ScheduleMagicAddress = 0;
static const uint16_t ScheduleFirstPointAddress = 1;
static const uint8_t ScheduleMagicValue = 0x53;
static const uint8_t SchedulePointsPerDay = 6;

}  // namespace ThermioStorageLayout
