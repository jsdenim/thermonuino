#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include <algorithm>

#include <ThermioByteStorage.h>

#include "thermostat_learning.h"

namespace {

uint8_t eeprom[1024];

uint8_t readByte(void *, uint16_t address) {
  return address < sizeof(eeprom) ? eeprom[address] : 0xFF;
}

bool updateByte(void *, uint16_t address, uint8_t value) {
  if (address >= sizeof(eeprom)) {
    return false;
  }
  eeprom[address] = value;
  return true;
}

void testTemporarySetpointCanBeCommittedWithoutAnotherDelta() {
  std::fill(eeprom, eeprom + sizeof(eeprom), 0xFF);
  ThermioByteStorage storage(nullptr, readByte, updateByte);
  thermonuino::ThermostatLearning learning;
  learning.setup(17.0, storage);

  const int thursday0715 = 3 * thermonuino::kSlotsPerDay + 29;
  thermonuino::LearningDecision temporary = learning.evaluate(
      thursday0715, 0, 18.0, 4, true, false, true, true, false, false);
  assert(temporary.targetHalf == 38);
  assert(temporary.temporaryOverrideActive);
  assert(!temporary.scheduleChanged);

  thermonuino::LearningDecision committed = learning.evaluate(
      thursday0715, 0, 18.0, 0, true, true, true, true, false, false);
  assert(committed.targetHalf == 38);
  assert(committed.explicitUserAction);
  assert(committed.committedUserAction);
  assert(committed.scheduleChanged);

  const thermonuino::LearningDecision replay = learning.evaluate(
      thursday0715 + 20, 0, 18.0, 0, false, false, true, false, false, true);
  assert(replay.targetHalf == 38);
  assert(replay.hasProgramTarget);
}

}  // namespace

int main() {
  testTemporarySetpointCanBeCommittedWithoutAnotherDelta();
  puts("thermostat_learning_test: OK");
  return 0;
}
