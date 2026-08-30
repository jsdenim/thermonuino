#ifndef SONDE_INPUT_SERVICE_H
#define SONDE_INPUT_SERVICE_H

#include <Arduino.h>

enum SondeInputEvent : uint8_t {
  SONDE_INPUT_NONE,
  SONDE_INPUT_PLUS,
  SONDE_INPUT_MINUS,
  SONDE_INPUT_CENTER
};

class SondeInputService {
public:
  struct Pins {
    uint8_t bodyDetect;
    uint8_t cmdSens1;
    uint8_t cmdSens2;
    uint8_t cmdButton;
    uint8_t wake;
  };

  explicit SondeInputService(const Pins &pins) : pins_(pins) {
  }

  void begin();
  SondeInputEvent update(uint32_t now);

  bool motionDetected() const;

private:
  static const uint32_t DebounceMs = 35;
  static const uint32_t MotionHoldMs = 900000;

  enum SwitchState : uint8_t {
    SWITCH_NONE,
    SWITCH_SENS1,
    SWITCH_SENS2,
    SWITCH_BUTTON,
    SWITCH_INVALID
  };

  Pins pins_;
  uint32_t lastSwitchReadAt_ = 0;
  uint32_t motionDetectedUntilAt_ = 0;
  SwitchState lastRawSwitch_ = SWITCH_NONE;
  SwitchState stableSwitch_ = SWITCH_NONE;
  uint8_t stableCount_ = 0;
  bool motionDetected_ = false;

  SwitchState readRawSwitch();
  SondeInputEvent eventForPress(SwitchState state);
};

#endif
