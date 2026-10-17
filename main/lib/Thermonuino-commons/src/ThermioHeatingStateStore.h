#pragma once

#include <stdint.h>

#include "ThermioByteStorage.h"
#include "ThermioHeatingRegulator.h"

class ThermioHeatingStateStore {
 public:
  static const uint8_t MagicValue = 0x48;
  static const uint8_t FormatVersion = 1;
  static const uint8_t StoredSize = 19;

  ThermioHeatingStateStore();
  void begin(ThermioByteStorage &storage, uint16_t firstAddress);
  bool load(ThermioHeatingRegulator &regulator) const;
  bool save(const ThermioHeatingRegulator &regulator) const;
  bool clear() const;

 private:
  ThermioByteStorage *storage_;
  uint16_t firstAddress_;

  static uint8_t checksum(const uint8_t *bytes, uint8_t length);
};
