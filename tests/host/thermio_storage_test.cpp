#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include <algorithm>

#include <ThermioByteStorage.h>
#include <ThermioHeatingRegulator.h>
#include <ThermioHeatingStateStore.h>
#include <ThermioStorageLayout.h>
#include <ThermioWeeklyProgram.h>

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

void testWeeklyProgram(ThermioByteStorage &storage) {
  ThermioWeeklyProgram program;
  program.begin(storage,
                ThermioStorageLayout::ScheduleFirstPointAddress,
                4,
                ThermioStorageLayout::SchedulePointsPerDay);
  assert(program.clearAll());
  assert(program.save(0, 0, 26, 39));
  assert(program.save(0, 0, 66, 41));
  assert(program.save(0, 0, 84, 35));

  ThermioWeeklyProgram::Selection selected = program.current(0, 0, 28, 34, false);
  assert(selected.found && selected.targetHalfC == 39);

  selected = program.current(0, 1, 20, 34, true);
  assert(selected.found && selected.sourceDay == 0 && selected.targetHalfC == 35);
  selected = program.current(0, 1, 28, 34, true);
  assert(selected.found && selected.targetHalfC == 39);
  selected = program.current(0, 1, 70, 34, true);
  assert(selected.found && selected.targetHalfC == 41);

  uint16_t delaySlots = 0;
  assert(program.next(0, 0, 88, 34, selected, delaySlots));
  assert(selected.targetHalfC == 39);
  assert(delaySlots == 34);
  assert(program.hasExplicitPointBetween(0, 25, 26));
  assert(!program.hasExplicitPointBetween(0, 26, 65));
}

void testHeatingPersistence(ThermioByteStorage &storage) {
  ThermioHeatingRegulator regulator;
  ThermioHeatingStateStore stateStore;
  stateStore.begin(storage, ThermioStorageLayout::HeatingStateFirstAddress);
  regulator.reset();
  regulator.observe(0, 190, 190, 1000, 1700, 500, true);
  assert(stateStore.save(regulator));

  ThermioHeatingRegulator restored;
  restored.reset();
  assert(stateStore.load(restored));
  assert(restored.holdConfidence(0) == regulator.holdConfidence(0));
  assert(restored.learnedHoldBtuPerHour(0) == regulator.learnedHoldBtuPerHour(0));
  assert(restored.learnedResponseBtuPerC() == regulator.learnedResponseBtuPerC());
}

}  // namespace

int main() {
  std::fill(eeprom, eeprom + sizeof(eeprom), 0xFF);
  ThermioByteStorage storage(nullptr, readByte, updateByte);
  testWeeklyProgram(storage);
  testHeatingPersistence(storage);
  puts("thermio_storage_test: OK");
  return 0;
}
