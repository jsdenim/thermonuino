#pragma once

#include <stdint.h>

class ThermioByteStorage {
 public:
  typedef uint8_t (*ReadByteFn)(void *context, uint16_t address);
  typedef bool (*UpdateByteFn)(void *context, uint16_t address, uint8_t value);

  ThermioByteStorage();
  ThermioByteStorage(void *context, ReadByteFn readByte, UpdateByteFn updateByte);

  void begin(void *context, ReadByteFn readByte, UpdateByteFn updateByte);
  bool ready() const;
  uint8_t read(uint16_t address) const;
  bool update(uint16_t address, uint8_t value) const;

 private:
  void *context_;
  ReadByteFn readByte_;
  UpdateByteFn updateByte_;
};
