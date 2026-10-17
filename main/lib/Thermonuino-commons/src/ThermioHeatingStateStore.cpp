#include "ThermioHeatingStateStore.h"

ThermioHeatingStateStore::ThermioHeatingStateStore()
    : storage_(0), firstAddress_(0) {}

void ThermioHeatingStateStore::begin(ThermioByteStorage &storage, uint16_t firstAddress) {
  storage_ = &storage;
  firstAddress_ = firstAddress;
}

bool ThermioHeatingStateStore::load(ThermioHeatingRegulator &regulator) const {
  if (!storage_ || !storage_->ready() || storage_->read(firstAddress_) != MagicValue ||
      storage_->read(firstAddress_ + 1) != FormatVersion) {
    return false;
  }

  uint8_t bytes[StoredSize - 1];
  for (uint8_t i = 0; i < sizeof(bytes); i++) {
    bytes[i] = storage_->read(firstAddress_ + 1 + i);
  }
  if (checksum(bytes, sizeof(bytes) - 1) != bytes[sizeof(bytes) - 1]) {
    return false;
  }

  ThermioHeatingRegulator::State state = {};
  uint8_t offset = 1;
  for (uint8_t zone = 0; zone < ThermioHeatingRegulator::ZoneCount; zone++) {
    state.holdBtuPerHour[zone] =
        (uint16_t)bytes[offset] | ((uint16_t)bytes[offset + 1] << 8);
    state.holdConfidence[zone] = bytes[offset + 2];
    offset += 3;
  }
  state.responseBtuPerC =
      (uint32_t)bytes[offset] |
      ((uint32_t)bytes[offset + 1] << 8) |
      ((uint32_t)bytes[offset + 2] << 16) |
      ((uint32_t)bytes[offset + 3] << 24);
  return regulator.restoreState(state);
}

bool ThermioHeatingStateStore::save(const ThermioHeatingRegulator &regulator) const {
  if (!storage_ || !storage_->ready()) {
    return false;
  }
  ThermioHeatingRegulator::State state;
  regulator.readState(state);

  uint8_t bytes[StoredSize - 1] = {};
  bytes[0] = FormatVersion;
  uint8_t offset = 1;
  for (uint8_t zone = 0; zone < ThermioHeatingRegulator::ZoneCount; zone++) {
    bytes[offset] = (uint8_t)state.holdBtuPerHour[zone];
    bytes[offset + 1] = (uint8_t)(state.holdBtuPerHour[zone] >> 8);
    bytes[offset + 2] = state.holdConfidence[zone];
    offset += 3;
  }
  bytes[offset] = (uint8_t)state.responseBtuPerC;
  bytes[offset + 1] = (uint8_t)(state.responseBtuPerC >> 8);
  bytes[offset + 2] = (uint8_t)(state.responseBtuPerC >> 16);
  bytes[offset + 3] = (uint8_t)(state.responseBtuPerC >> 24);
  bytes[sizeof(bytes) - 1] = checksum(bytes, sizeof(bytes) - 1);

  bool ok = true;
  for (uint8_t i = 0; i < sizeof(bytes); i++) {
    ok = storage_->update(firstAddress_ + 1 + i, bytes[i]) && ok;
  }
  ok = storage_->update(firstAddress_, MagicValue) && ok;
  return ok;
}

bool ThermioHeatingStateStore::clear() const {
  return storage_ && storage_->ready() && storage_->update(firstAddress_, 0xFF);
}

uint8_t ThermioHeatingStateStore::checksum(const uint8_t *bytes, uint8_t length) {
  uint8_t value = 0xA7;
  for (uint8_t i = 0; i < length; i++) {
    value = (uint8_t)((value << 1) | (value >> 7));
    value ^= bytes[i];
  }
  return value;
}
