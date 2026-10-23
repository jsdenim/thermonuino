#include "ThermioSetpointPolicy.h"

ThermioSetpointPolicy::Decision ThermioSetpointPolicy::evaluate(const Input &input) {
  Decision decision = {input.programTarget, false, false};
  if (!input.overrideActive) {
    return decision;
  }

  if (!input.programEnabled) {
    decision.target = input.overrideTarget;
    decision.overrideApplied = true;
    return decision;
  }

  const int32_t elapsed = (int32_t)(input.now - input.overrideStartedAt);
  if (elapsed < 0 || input.supersededByProgram) {
    decision.clearOverride = true;
    return decision;
  }

  if ((uint32_t)elapsed < input.overrideDuration || !input.fallbackProgramExists) {
    decision.target = input.overrideTarget;
    decision.overrideApplied = true;
    return decision;
  }

  decision.clearOverride = true;
  return decision;
}
