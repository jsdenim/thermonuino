#pragma once

#include <Arduino.h>

namespace ThermioRfIds {

struct EepromSlot {
  int magic0;
  int magic1;
  int idLow;
  int idHigh;
  uint8_t magicValue0;
  uint8_t magicValue1;
};

bool isValid(uint16_t id, uint16_t forbiddenId = 0);
bool load(const EepromSlot &slot, uint16_t &id, uint16_t forbiddenId = 0);
void save(const EepromSlot &slot, uint16_t id);
void clear(const EepromSlot &slot);
uint16_t generate(uint8_t entropyPin, uint16_t fallbackId, uint16_t forbiddenId = 0);

} // namespace ThermioRfIds
