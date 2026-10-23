#pragma once

#include <stdint.h>

class ThermioSetpointPolicy {
public:
  struct Input {
    int16_t programTarget;
    int16_t overrideTarget;
    uint32_t overrideStartedAt;
    uint32_t now;
    uint32_t overrideDuration;
    bool overrideActive;
    bool programEnabled;
    bool fallbackProgramExists;
    bool supersededByProgram;
  };

  struct Decision {
    int16_t target;
    bool overrideApplied;
    bool clearOverride;
  };

  static Decision evaluate(const Input &input);
};
