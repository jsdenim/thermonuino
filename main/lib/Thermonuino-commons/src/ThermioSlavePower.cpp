#include "ThermioSlavePower.h"

#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/wdt.h>

namespace {
volatile bool gPinWake = false;
volatile uint16_t gWatchdogTicks = 0;
ThermioSlavePower::PinChangeCallback gPinChangeCallback = nullptr;
}

ISR(PCINT1_vect) {
  gPinWake = true;
  if (gPinChangeCallback != nullptr) {
    gPinChangeCallback();
  }
}

ISR(PCINT2_vect) {
  gPinWake = true;
  if (gPinChangeCallback != nullptr) {
    gPinChangeCallback();
  }
}

ISR(WDT_vect) {
  gWatchdogTicks++;
}

namespace ThermioSlavePower {

void setupWatchdog8s() {
  MCUSR &= ~_BV(WDRF);
  noInterrupts();
  WDTCSR = _BV(WDCE) | _BV(WDE);
  WDTCSR = _BV(WDIE) | _BV(WDP3) | _BV(WDP0);
  interrupts();
}

void setupPortCPinChange(uint8_t pcintMask) {
  PCICR |= _BV(PCIE1);
  PCMSK1 |= pcintMask;
}

void setupPortDPinChange(uint8_t pcintMask) {
  PCICR |= _BV(PCIE2);
  PCMSK2 |= pcintMask;
}

void registerPinChangeCallback(PinChangeCallback callback) {
  noInterrupts();
  gPinChangeCallback = callback;
  interrupts();
}

uint16_t consumeWatchdogTicks() {
  noInterrupts();
  const uint16_t ticks = gWatchdogTicks;
  gWatchdogTicks = 0;
  interrupts();
  return ticks;
}

bool consumePinWake() {
  noInterrupts();
  const bool value = gPinWake;
  gPinWake = false;
  interrupts();
  return value;
}

void sleepPowerDown(bool keepAdc) {
  const bool adcWasEnabled = (ADCSRA & _BV(ADEN)) != 0;
  if (!keepAdc) {
    ADCSRA &= ~_BV(ADEN);
  }

  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  noInterrupts();
  sleep_enable();
#if defined(BODS) && defined(BODSE)
  sleep_bod_disable();
#endif
  interrupts();
  sleep_cpu();
  sleep_disable();

  if (!keepAdc && adcWasEnabled) {
    ADCSRA |= _BV(ADEN);
  }
}

} // namespace ThermioSlavePower
