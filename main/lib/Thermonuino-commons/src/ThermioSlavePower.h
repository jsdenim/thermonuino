#pragma once

#include <Arduino.h>

namespace ThermioSlavePower {

typedef void (*PinChangeCallback)();

void setupWatchdog8s();
void setupPortCPinChange(uint8_t pcintMask);
void setupPortDPinChange(uint8_t pcintMask);
void registerPinChangeCallback(PinChangeCallback callback);
uint16_t consumeWatchdogTicks();
bool consumePinWake();
void sleepPowerDown(bool keepAdc = false);

} // namespace ThermioSlavePower
