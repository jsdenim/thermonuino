#include "ThermioRfIds.h"

#include <EEPROM.h>

namespace ThermioRfIds {

bool isValid(uint16_t id, uint16_t forbiddenId) {
  return id != 0x0000 && id != 0xFFFF && id != forbiddenId;
}

bool load(const EepromSlot &slot, uint16_t &id, uint16_t forbiddenId) {
  const bool magicOk =
      EEPROM.read(slot.magic0) == slot.magicValue0 &&
      EEPROM.read(slot.magic1) == slot.magicValue1;
  if (!magicOk) {
    return false;
  }

  const uint16_t loaded =
      (uint16_t)EEPROM.read(slot.idLow) |
      ((uint16_t)EEPROM.read(slot.idHigh) << 8);
  if (!isValid(loaded, forbiddenId)) {
    return false;
  }

  id = loaded;
  return true;
}

void save(const EepromSlot &slot, uint16_t id) {
  EEPROM.update(slot.magic0, slot.magicValue0);
  EEPROM.update(slot.magic1, slot.magicValue1);
  EEPROM.update(slot.idLow, id & 0xFF);
  EEPROM.update(slot.idHigh, id >> 8);
}

void clear(const EepromSlot &slot) {
  EEPROM.update(slot.magic0, 0xFF);
  EEPROM.update(slot.magic1, 0xFF);
  EEPROM.update(slot.idLow, 0xFF);
  EEPROM.update(slot.idHigh, 0xFF);
}

uint16_t generate(uint8_t entropyPin, uint16_t fallbackId, uint16_t forbiddenId) {
  uint32_t seed = micros() ^ ((uint32_t)millis() << 16);
  for (uint8_t i = 0; i < 24; i++) {
    seed ^= (uint32_t)analogRead(entropyPin) << (i % 10);
    seed = (seed << 5) | (seed >> 27);
    delay(2);
  }

  const uint16_t generated = (uint16_t)(seed & 0x7FFF);
  return isValid(generated, forbiddenId) ? generated : fallbackId;
}

} // namespace ThermioRfIds
