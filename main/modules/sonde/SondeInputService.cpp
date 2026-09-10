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
  sens1BitMask_ = digitalPinToBitMask(pins_.cmdSens1);
  sens2BitMask_ = digitalPinToBitMask(pins_.cmdSens2);
  buttonBitMask_ = digitalPinToBitMask(pins_.cmdButton);
  sens1PressedLatch_ = (*sens1InputRegister_ & sens1BitMask_) == 0;
  sens2PressedLatch_ = (*sens2InputRegister_ & sens2BitMask_) == 0;
  buttonPressedLatch_ = (*buttonInputRegister_ & buttonBitMask_) == 0;
  activeInstance_ = this;
  ThermioSlavePower::registerPinChangeCallback(handlePinChangeInterrupt);
  configurePinChangeInterrupt(pins_.cmdSens1);
  configurePinChangeInterrupt(pins_.cmdSens2);
  configurePinChangeInterrupt(pins_.cmdButton);
  configurePinChangeInterrupt(pins_.bodyDetect);

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

  const SondeInputEvent queuedEvent = popQueuedEvent();
  if (queuedEvent != SONDE_INPUT_NONE) {
    suppressNextStablePress_ = true;
    return queuedEvent;
  }

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

  if (previous == SWITCH_NONE && stableSwitch_ != SWITCH_NONE &&
      stableSwitch_ != SWITCH_INVALID) {
    if (suppressNextStablePress_) {
      suppressNextStablePress_ = false;
      return SONDE_INPUT_NONE;
    }
    return eventForPress(stableSwitch_);
  }
  return SONDE_INPUT_NONE;
}

bool SondeInputService::motionDetected() const {
  return motionDetected_;
}

bool SondeInputService::centerPressed() const {
  return digitalRead(pins_.cmdButton) == LOW;
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

  if (sens1Pressed && !sens1PressedLatch_) {
    queueEventFromIsr(SONDE_INPUT_PLUS, lastSens1QueuedAt_);
  }
  if (sens2Pressed && !sens2PressedLatch_) {
    queueEventFromIsr(SONDE_INPUT_MINUS, lastSens2QueuedAt_);
  }
  if (buttonPressed && !buttonPressedLatch_) {
    queueEventFromIsr(SONDE_INPUT_CENTER, lastButtonQueuedAt_);
  }

  sens1PressedLatch_ = sens1Pressed;
  sens2PressedLatch_ = sens2Pressed;
  buttonPressedLatch_ = buttonPressed;
}

void SondeInputService::queueEventFromIsr(SondeInputEvent event, volatile uint32_t &lastQueuedAt) {
  const uint32_t now = millis();
  if ((uint32_t)(now - lastQueuedAt) < DebounceMs) {
    return;
  }

  const uint8_t nextHead = (queueHead_ + 1) % EventQueueSize;
  if (nextHead == queueTail_) {
    return;
  }

  queuedEvents_[queueHead_] = event;
  queueHead_ = nextHead;
  lastQueuedAt = now;
}

SondeInputEvent SondeInputService::popQueuedEvent() {
  noInterrupts();
  if (queueTail_ == queueHead_) {
    interrupts();
    return SONDE_INPUT_NONE;
  }

  const SondeInputEvent event = (SondeInputEvent)queuedEvents_[queueTail_];
  queueTail_ = (queueTail_ + 1) % EventQueueSize;
  interrupts();
  return event;
}

void SondeInputService::handlePinChangeInterrupt() {
  if (activeInstance_ != nullptr) {
    activeInstance_->captureSwitchesFromIsr();
  }
}
