#include "SondeInputService.h"

void SondeInputService::begin() {
  pinMode(pins_.bodyDetect, INPUT);
  pinMode(pins_.cmdSens1, INPUT_PULLUP);
  pinMode(pins_.cmdSens2, INPUT_PULLUP);
  pinMode(pins_.cmdButton, INPUT_PULLUP);
  pinMode(pins_.wake, INPUT);

  lastRawSwitch_ = readRawSwitch();
  stableSwitch_ = lastRawSwitch_;
  stableCount_ = 0;
  if (digitalRead(pins_.bodyDetect) == HIGH) {
    motionDetectedUntilAt_ = millis() + MotionHoldMs;
  }
  motionDetected_ = (int32_t)(motionDetectedUntilAt_ - millis()) > 0;
}

SondeInputEvent SondeInputService::update(uint32_t now) {
  if (digitalRead(pins_.bodyDetect) == HIGH) {
    motionDetectedUntilAt_ = now + MotionHoldMs;
  }
  motionDetected_ = (int32_t)(motionDetectedUntilAt_ - now) > 0;

  if ((uint32_t)(now - lastSwitchReadAt_) < DebounceMs) {
    return SONDE_INPUT_NONE;
  }
  lastSwitchReadAt_ = now;

  const SwitchState raw = readRawSwitch();
  if (raw == lastRawSwitch_) {
    if (stableCount_ < 3) {
      stableCount_++;
    }
  } else {
    lastRawSwitch_ = raw;
    stableCount_ = 0;
  }

  if (stableCount_ < 2 || raw == stableSwitch_) {
    return SONDE_INPUT_NONE;
  }

  const SwitchState previous = stableSwitch_;
  stableSwitch_ = raw;

  if (previous == SWITCH_NONE && stableSwitch_ != SWITCH_NONE) {
    return eventForPress(stableSwitch_);
  }
  return SONDE_INPUT_NONE;
}

bool SondeInputService::motionDetected() const {
  return motionDetected_;
}

SondeInputService::SwitchState SondeInputService::readRawSwitch() {
  const bool sens1 = digitalRead(pins_.cmdSens1) == LOW;
  const bool sens2 = digitalRead(pins_.cmdSens2) == LOW;
  const bool button = digitalRead(pins_.cmdButton) == LOW;
  const uint8_t activeCount = (sens1 ? 1 : 0) + (sens2 ? 1 : 0) + (button ? 1 : 0);

  if (activeCount == 0) {
    return SWITCH_NONE;
  }
  if (activeCount > 1) {
    return SWITCH_INVALID;
  }
  if (sens1) {
    return SWITCH_SENS1;
  }
  if (sens2) {
    return SWITCH_SENS2;
  }
  return SWITCH_BUTTON;
}

SondeInputEvent SondeInputService::eventForPress(SwitchState state) {
  switch (state) {
    case SWITCH_SENS1:
      return SONDE_INPUT_PLUS;
    case SWITCH_SENS2:
      return SONDE_INPUT_MINUS;
    case SWITCH_BUTTON:
      return SONDE_INPUT_CENTER;
    default:
      return SONDE_INPUT_NONE;
  }
}
