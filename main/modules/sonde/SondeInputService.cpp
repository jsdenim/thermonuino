#include "SondeInputService.h"

#include <ThermioSlavePower.h>

SondeInputService *SondeInputService::activeInstance_ = nullptr;

void SondeInputService::begin() {
  pinMode(pins_.bodyDetect, INPUT);
  pinMode(pins_.cmdSens1, INPUT_PULLUP);
  pinMode(pins_.cmdSens2, INPUT_PULLUP);
  pinMode(pins_.cmdButton, INPUT_PULLUP);
  pinMode(pins_.wake, INPUT);

  sens1InputRegister_ = portInputRegister(digitalPinToPort(pins_.cmdSens1));
  sens2InputRegister_ = portInputRegister(digitalPinToPort(pins_.cmdSens2));
  buttonInputRegister_ = portInputRegister(digitalPinToPort(pins_.cmdButton));
  bodyInputRegister_ = portInputRegister(digitalPinToPort(pins_.bodyDetect));
  sens1BitMask_ = digitalPinToBitMask(pins_.cmdSens1);
  sens2BitMask_ = digitalPinToBitMask(pins_.cmdSens2);
  buttonBitMask_ = digitalPinToBitMask(pins_.cmdButton);
  bodyBitMask_ = digitalPinToBitMask(pins_.bodyDetect);
  sens1PressedLatch_ = (*sens1InputRegister_ & sens1BitMask_) == 0;
  sens2PressedLatch_ = (*sens2InputRegister_ & sens2BitMask_) == 0;
  buttonPressedLatch_ = (*buttonInputRegister_ & buttonBitMask_) == 0;
  bodyDetectedLatch_ = (*bodyInputRegister_ & bodyBitMask_) != 0;
  activeInstance_ = this;
  ThermioSlavePower::registerPinChangeCallback(handlePinChangeInterrupt);
  configurePinChangeInterrupt(pins_.cmdSens1);
  configurePinChangeInterrupt(pins_.cmdSens2);
  configurePinChangeInterrupt(pins_.cmdButton);
  configurePinChangeInterrupt(pins_.bodyDetect);

  if (bodyDetectedLatch_) {
    motionEventCount_ = 1;
    motionHoldWatchdogTicks_ = MotionHoldWatchdogTicks;
  }
  motionDetected_ = motionHoldWatchdogTicks_ > 0;
}

SondeInputEvent SondeInputService::update(uint32_t now, uint16_t watchdogTicks) {
  if (watchdogTicks >= motionHoldWatchdogTicks_) {
    motionHoldWatchdogTicks_ = 0;
  } else {
    motionHoldWatchdogTicks_ -= watchdogTicks;
  }
  if (digitalRead(pins_.bodyDetect) == HIGH) {
    motionHoldWatchdogTicks_ = MotionHoldWatchdogTicks;
  }
  motionDetected_ = motionHoldWatchdogTicks_ > 0;

  if (repeatCount_ > 0 && repeatEvent_ != SONDE_INPUT_NONE) {
    repeatCount_--;
    return repeatEvent_;
  }

  bool interruptSeen = false;
  noInterrupts();
  if (switchInterruptSeen_) {
    switchInterruptSeen_ = false;
    interruptSeen = true;
  }
  interrupts();

  if (interruptSeen && !arbitrationActive_) {
    startArbitration(now);
  }

  if (!arbitrationActive_ && (uint32_t)(now - lastSwitchReadAt_) >= DebounceMs) {
    lastSwitchReadAt_ = now;
    if (readSwitchMask() != 0) {
      startArbitration(now);
    }
  }

  if (!arbitrationActive_) {
    return SONDE_INPUT_NONE;
  }

  sampleArbitration(now);
  if ((uint32_t)(now - arbitrationStartedAt_) < ArbitrationWindowMs) {
    return SONDE_INPUT_NONE;
  }

  return finishArbitration();
}

bool SondeInputService::motionDetected() const {
  return motionDetected_;
}

uint8_t SondeInputService::consumeMotionEvents() {
  noInterrupts();
  const uint8_t count = motionEventCount_;
  motionEventCount_ = 0;
  interrupts();
  return count;
}

bool SondeInputService::centerPressed() const {
  return digitalRead(pins_.cmdButton) == LOW;
}

bool SondeInputService::hasPendingEvent() const {
  noInterrupts();
  const bool pendingInterrupt = switchInterruptSeen_;
  interrupts();
  return pendingInterrupt || arbitrationActive_ || repeatCount_ > 0;
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

uint8_t SondeInputService::readSwitchMask() {
  uint8_t mask = 0;
  if (digitalRead(pins_.cmdSens1) == LOW) {
    mask |= SwitchMaskSens1;
  }
  if (digitalRead(pins_.cmdSens2) == LOW) {
    mask |= SwitchMaskSens2;
  }
  if (digitalRead(pins_.cmdButton) == LOW) {
    mask |= SwitchMaskButton;
  }
  return mask;
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

void SondeInputService::consumeEdgeCounts(uint8_t &sens1Edges,
                                          uint8_t &sens2Edges,
                                          uint8_t &buttonEdges) {
  noInterrupts();
  sens1Edges = sens1EdgeCount_;
  sens2Edges = sens2EdgeCount_;
  buttonEdges = buttonEdgeCount_;
  sens1EdgeCount_ = 0;
  sens2EdgeCount_ = 0;
  buttonEdgeCount_ = 0;
  interrupts();
}

void SondeInputService::startArbitration(uint32_t now) {
  arbitrationActive_ = true;
  arbitrationStartedAt_ = now;
  lastArbitrationSampleAt_ = now - ArbitrationSampleMs;
  arbitrationSens1Score_ = 0;
  arbitrationSens2Score_ = 0;
  arbitrationButtonScore_ = 0;
  consumeEdgeCounts(arbitrationSens1Edges_,
                    arbitrationSens2Edges_,
                    arbitrationButtonEdges_);
  sampleArbitration(now);
}

void SondeInputService::sampleArbitration(uint32_t now) {
  uint8_t sens1Edges = 0;
  uint8_t sens2Edges = 0;
  uint8_t buttonEdges = 0;
  consumeEdgeCounts(sens1Edges, sens2Edges, buttonEdges);
  arbitrationSens1Edges_ += sens1Edges;
  arbitrationSens2Edges_ += sens2Edges;
  arbitrationButtonEdges_ += buttonEdges;

  while ((uint32_t)(now - lastArbitrationSampleAt_) >= ArbitrationSampleMs) {
    lastArbitrationSampleAt_ += ArbitrationSampleMs;
    const uint8_t mask = readSwitchMask();
    if ((mask & SwitchMaskSens1) != 0) {
      arbitrationSens1Score_++;
    }
    if ((mask & SwitchMaskSens2) != 0) {
      arbitrationSens2Score_++;
    }
    if ((mask & SwitchMaskButton) != 0) {
      arbitrationButtonScore_++;
    }
  }
}

SondeInputEvent SondeInputService::finishArbitration() {
  arbitrationActive_ = false;
  uint16_t bestScore = arbitrationButtonScore_;
  uint8_t repetitions = arbitrationButtonEdges_;
  SondeInputEvent event = bestScore > 0 || repetitions > 0 ? SONDE_INPUT_CENTER : SONDE_INPUT_NONE;

  if (arbitrationSens1Score_ > bestScore) {
    bestScore = arbitrationSens1Score_;
    repetitions = arbitrationSens1Edges_;
    event = SONDE_INPUT_PLUS;
  }
  if (arbitrationSens2Score_ > bestScore) {
    bestScore = arbitrationSens2Score_;
    repetitions = arbitrationSens2Edges_;
    event = SONDE_INPUT_MINUS;
  }

  if (event == SONDE_INPUT_NONE) {
    return SONDE_INPUT_NONE;
  }

  if (repetitions == 0) {
    repetitions = 1;
  }
  if (repetitions > MaxRepeatedEvents) {
    repetitions = MaxRepeatedEvents;
  }
  repeatEvent_ = event;
  repeatCount_ = repetitions - 1;
  return event;
}

void SondeInputService::configurePinChangeInterrupt(uint8_t pin) {
  volatile uint8_t *maskRegister = digitalPinToPCMSK(pin);
  if (maskRegister == nullptr) {
    return;
  }

  const uint8_t pcicrBit = digitalPinToPCICRbit(pin);
  noInterrupts();
  *maskRegister |= _BV(digitalPinToPCMSKbit(pin));
  PCIFR |= _BV(pcicrBit);
  PCICR |= _BV(pcicrBit);
  interrupts();
}

void SondeInputService::captureSwitchesFromIsr() {
  const bool sens1Pressed = (*sens1InputRegister_ & sens1BitMask_) == 0;
  const bool sens2Pressed = (*sens2InputRegister_ & sens2BitMask_) == 0;
  const bool buttonPressed = (*buttonInputRegister_ & buttonBitMask_) == 0;
  const bool bodyDetected = (*bodyInputRegister_ & bodyBitMask_) != 0;

  if (sens1Pressed && !sens1PressedLatch_) {
    countEdgeFromIsr(sens1EdgeCount_, lastSens1QueuedAt_);
  }
  if (sens2Pressed && !sens2PressedLatch_) {
    countEdgeFromIsr(sens2EdgeCount_, lastSens2QueuedAt_);
  }
  if (buttonPressed && !buttonPressedLatch_) {
    countEdgeFromIsr(buttonEdgeCount_, lastButtonQueuedAt_);
  }
  if (bodyDetected && !bodyDetectedLatch_ && motionEventCount_ < 255) {
    motionEventCount_++;
  }
  if (sens1Pressed || sens2Pressed || buttonPressed) {
    switchInterruptSeen_ = true;
  }

  sens1PressedLatch_ = sens1Pressed;
  sens2PressedLatch_ = sens2Pressed;
  buttonPressedLatch_ = buttonPressed;
  bodyDetectedLatch_ = bodyDetected;
}

void SondeInputService::countEdgeFromIsr(volatile uint8_t &edgeCount,
                                         volatile uint32_t &lastQueuedAt) {
  const uint32_t now = millis();
  if ((uint32_t)(now - lastQueuedAt) < DebounceMs) {
    return;
  }

  if (edgeCount < MaxRepeatedEvents) {
    edgeCount++;
  }
  lastQueuedAt = now;
}

void SondeInputService::handlePinChangeInterrupt() {
  if (activeInstance_ != nullptr) {
    activeInstance_->captureSwitchesFromIsr();
  }
}
