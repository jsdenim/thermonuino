#pragma once

#include <Arduino.h>

namespace ThermioSlavePower {

void setupWatchdog8s();
void setupPortDPinChange(uint8_t pcintMask);
uint16_t consumeWatchdogTicks();
bool consumePinWake();
void sleepPowerDown(bool keepAdc = false);

} // namespace ThermioSlavePower
