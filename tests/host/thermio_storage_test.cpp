#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include <algorithm>

#include <ThermioByteStorage.h>
#include <ThermioHeatingRegulator.h>
#include <ThermioSetpointPolicy.h>
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
  assert(program.referenceDay(0) == 0);

  ThermioWeeklyProgram::Selection selected = program.current(0, 0, 28, 34, false);
  assert(selected.found && selected.targetHalfC == 39);

  selected = program.current(0, 1, 20, 34, true);
  assert(selected.found && selected.sourceDay == 0 && selected.targetHalfC == 35);
  selected = program.current(0, 1, 28, 34, true);
  assert(selected.found && selected.targetHalfC == 39);
  selected = program.current(0, 1, 70, 34, true);
  assert(selected.found && selected.targetHalfC == 41);

  assert(program.save(0, 3, 40, 36));
  assert(program.referenceDay(0) == 0);
  selected = program.current(0, 3, 20, 34, true);
  assert(selected.found && selected.sourceDay == 3 && selected.targetHalfC == 36);
  selected = program.current(0, 3, 70, 34, true);
  assert(selected.found && selected.sourceDay == 3 && selected.targetHalfC == 36);
  selected = program.current(0, 4, 28, 34, true);
  assert(selected.found && selected.sourceDay == 0 && selected.targetHalfC == 39);
  selected = program.current(0, 4, 70, 34, true);
  assert(selected.found && selected.sourceDay == 0 && selected.targetHalfC == 41);

  assert(program.save(1, 2, 32, 40));
  assert(program.referenceDay(1) == 2);
  selected = program.current(1, 6, 34, 34, true);
  assert(selected.found && selected.sourceDay == 2 && selected.targetHalfC == 40);

  uint16_t delaySlots = 0;
  assert(program.next(0, 0, 88, 34, selected, delaySlots));
  assert(selected.targetHalfC == 39);
  assert(delaySlots == 34);
  assert(program.hasExplicitPointBetween(0, 25, 26));
  assert(!program.hasExplicitPointBetween(0, 26, 65));

  assert(program.clearZone(0));
  selected = program.current(0, 0, 28, 34, true);
  assert(!selected.found && selected.targetHalfC == 34);
  selected = program.current(1, 2, 34, 34, true);
  assert(selected.found && selected.targetHalfC == 40);

  assert(program.clearAll());
  selected = program.current(1, 2, 34, 34, true);
  assert(!selected.found && selected.targetHalfC == 34);
}

void testOneSlotPreheat() {
  ThermioHeatingRegulator regulator;
  regulator.reset();

  ThermioHeatingRegulator::PreheatPlan plan =
      regulator.planPreheat(180, 190, 205, 30);
  assert(!plan.active && plan.workTargetDeciC == 190);

  plan = regulator.planPreheat(180, 190, 205, 15);
  assert(plan.active && plan.workTargetDeciC == 205);

  plan = regulator.planPreheat(210, 190, 205, 15);
  assert(!plan.active && plan.workTargetDeciC == 190);

  plan = regulator.planPreheat(200, 200, 180, 15);
  assert(!plan.active && plan.workTargetDeciC == 200);
}

void testSharedSetpointPolicy() {
  ThermioSetpointPolicy::Decision decision = ThermioSetpointPolicy::evaluate({
      190, 205, 100, 150, 120, true, true, true, false});
  assert(decision.overrideApplied && !decision.clearOverride && decision.target == 205);

  decision = ThermioSetpointPolicy::evaluate({
      190, 205, 100, 220, 120, true, true, true, false});
  assert(!decision.overrideApplied && decision.clearOverride && decision.target == 190);

  decision = ThermioSetpointPolicy::evaluate({
      190, 205, 100, 220, 120, true, true, false, false});
  assert(decision.overrideApplied && !decision.clearOverride && decision.target == 205);

  decision = ThermioSetpointPolicy::evaluate({
      190, 205, 100, 110, 120, true, true, true, true});
  assert(!decision.overrideApplied && decision.clearOverride && decision.target == 190);

  decision = ThermioSetpointPolicy::evaluate({
      190, 205, 100, 10000, 120, true, false, true, true});
  assert(decision.overrideApplied && !decision.clearOverride && decision.target == 205);

  decision = ThermioSetpointPolicy::evaluate({
      190, 205, 0xFFFFFFF0UL, 0x10UL, 64, true, true, true, false});
  assert(decision.overrideApplied && !decision.clearOverride && decision.target == 205);
}

}  // namespace

int main() {
  std::fill(eeprom, eeprom + sizeof(eeprom), 0xFF);
  ThermioByteStorage storage(nullptr, readByte, updateByte);
  testWeeklyProgram(storage);
  testOneSlotPreheat();
  testSharedSetpointPolicy();
  puts("thermio_storage_test: OK");
  return 0;
}
