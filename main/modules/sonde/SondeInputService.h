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

  static void handlePinChangeInterrupt();

private:
  static const uint32_t DebounceMs = 35;
  static const uint32_t MotionHoldMs = 900000;
  static const uint8_t EventQueueSize = 8;

  enum SwitchState : uint8_t {
    SWITCH_NONE,
    SWITCH_SENS1,
    SWITCH_SENS2,
    SWITCH_BUTTON,
    SWITCH_INVALID
  };

  Pins pins_;
  volatile uint8_t *sens1InputRegister_ = nullptr;
  volatile uint8_t *sens2InputRegister_ = nullptr;
  volatile uint8_t *buttonInputRegister_ = nullptr;
  uint8_t sens1BitMask_ = 0;
  uint8_t sens2BitMask_ = 0;
  uint8_t buttonBitMask_ = 0;
  volatile uint8_t queuedEvents_[EventQueueSize];
  volatile uint8_t queueHead_ = 0;
  volatile uint8_t queueTail_ = 0;
  volatile bool sens1PressedLatch_ = false;
  volatile bool sens2PressedLatch_ = false;
  volatile bool buttonPressedLatch_ = false;
  volatile uint32_t lastSens1QueuedAt_ = 0;
  volatile uint32_t lastSens2QueuedAt_ = 0;
  volatile uint32_t lastButtonQueuedAt_ = 0;
  uint32_t lastSwitchReadAt_ = 0;
  uint32_t motionDetectedUntilAt_ = 0;
  SwitchState lastRawSwitch_ = SWITCH_NONE;
  SwitchState stableSwitch_ = SWITCH_NONE;
  uint8_t stableCount_ = 0;
  bool motionDetected_ = false;
  bool suppressNextStablePress_ = false;

  SwitchState readRawSwitch();
  SondeInputEvent eventForPress(SwitchState state);
  void configurePinChangeInterrupt(uint8_t pin);
  void captureSwitchesFromIsr();
  void queueEventFromIsr(SondeInputEvent event, volatile uint32_t &lastQueuedAt);
  SondeInputEvent popQueuedEvent();

  static SondeInputService *activeInstance_;
};

#endif
