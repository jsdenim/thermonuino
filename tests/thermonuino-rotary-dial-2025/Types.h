#pragma once
#include <Arduino.h>


inline int16_t clampTq(int16_t tq) {
  return tq < TQ_MIN ? TQ_MIN : (tq > TQ_MAX ? TQ_MAX : tq);
}


struct RfReading {
  bool ok = false;
  bool lowBatt = false;
  int16_t Tq = 20 * 4;
  uint32_t lastMs = 0;
};
struct Vacation {
  bool active = false;
  uint32_t endEpoch = 0;
  uint8_t days = 0;
};


struct ZoneCtl {
  uint8_t U_base = 64;
  uint8_t duty = 0;
  int32_t I = 0;
  uint16_t Uavg = 0;
};


enum class UiState : uint8_t { Idle = 0,
                               Adjust,
                               Menu };


enum class RampProfile : uint8_t { Soft = 0,
                                   Normal = 1,
                                   Fast = 2 };
