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
  bool centerPressed() const;
  bool hasPendingEvent() const;

  static void handlePinChangeInterrupt();

private:
  static const uint32_t DebounceMs = 35;
  static const uint32_t ArbitrationWindowMs = 800;
  static const uint32_t ArbitrationSampleMs = 20;
  static const uint32_t MotionHoldMs = 900000;
  static const uint8_t MaxRepeatedEvents = 12;

  static const uint8_t SwitchMaskSens1 = 0x01;
  static const uint8_t SwitchMaskSens2 = 0x02;
  static const uint8_t SwitchMaskButton = 0x04;

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
  volatile bool switchInterruptSeen_ = false;
  volatile uint8_t sens1EdgeCount_ = 0;
  volatile uint8_t sens2EdgeCount_ = 0;
  volatile uint8_t buttonEdgeCount_ = 0;
  volatile bool sens1PressedLatch_ = false;
  volatile bool sens2PressedLatch_ = false;
  volatile bool buttonPressedLatch_ = false;
  volatile uint32_t lastSens1QueuedAt_ = 0;
  volatile uint32_t lastSens2QueuedAt_ = 0;
  volatile uint32_t lastButtonQueuedAt_ = 0;
  uint32_t lastSwitchReadAt_ = 0;
  uint32_t motionDetectedUntilAt_ = 0;
  uint32_t arbitrationStartedAt_ = 0;
  uint32_t lastArbitrationSampleAt_ = 0;
  uint16_t arbitrationSens1Score_ = 0;
  uint16_t arbitrationSens2Score_ = 0;
  uint16_t arbitrationButtonScore_ = 0;
  uint8_t arbitrationSens1Edges_ = 0;
  uint8_t arbitrationSens2Edges_ = 0;
  uint8_t arbitrationButtonEdges_ = 0;
  uint8_t repeatCount_ = 0;
  bool arbitrationActive_ = false;
  bool motionDetected_ = false;
  SondeInputEvent repeatEvent_ = SONDE_INPUT_NONE;

  SwitchState readRawSwitch();
  uint8_t readSwitchMask();
  SondeInputEvent eventForPress(SwitchState state);
  void consumeEdgeCounts(uint8_t &sens1Edges, uint8_t &sens2Edges, uint8_t &buttonEdges);
  void startArbitration(uint32_t now);
  void sampleArbitration(uint32_t now);
  SondeInputEvent finishArbitration();
  void configurePinChangeInterrupt(uint8_t pin);
  void captureSwitchesFromIsr();
  void countEdgeFromIsr(volatile uint8_t &edgeCount, volatile uint32_t &lastQueuedAt);

  static SondeInputService *activeInstance_;
};

#endif
