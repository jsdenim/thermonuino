#include "ThermioByteStorage.h"

ThermioByteStorage::ThermioByteStorage()
    : context_(0), readByte_(0), updateByte_(0) {}

ThermioByteStorage::ThermioByteStorage(void *context,
                                       ReadByteFn readByte,
                                       UpdateByteFn updateByte)
    : context_(context), readByte_(readByte), updateByte_(updateByte) {}

void ThermioByteStorage::begin(void *context,
                               ReadByteFn readByte,
                               UpdateByteFn updateByte) {
  context_ = context;
  readByte_ = readByte;
  updateByte_ = updateByte;
}

bool ThermioByteStorage::ready() const {
  return readByte_ && updateByte_;
}

uint8_t ThermioByteStorage::read(uint16_t address) const {
  return readByte_ ? readByte_(context_, address) : 0xFF;
}

bool ThermioByteStorage::update(uint16_t address, uint8_t value) const {
  return updateByte_ && updateByte_(context_, address, value);
}
