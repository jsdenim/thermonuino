#include "greetings.h"
#include "thermostat_learning.h"

#include <ThermioHeatingRegulator.h>
#include <ThermioHeatingStateStore.h>
#include <ThermioStorageLayout.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define WASM_KEEPALIVE EMSCRIPTEN_KEEPALIVE
#else
#define WASM_KEEPALIVE
#endif

static double configuredBaseTemp = 17.0;
static thermonuino::ThermostatLearning learning;
static ThermioHeatingRegulator regulator;
static ThermioHeatingStateStore regulatorStore;
static ThermioByteStorage virtualStorage;
static uint8_t virtualEeprom[1024];
static bool virtualEepromReady = false;

static uint8_t readVirtualEeprom(void *, uint16_t address) {
  return address < sizeof(virtualEeprom) ? virtualEeprom[address] : 0xFF;
}

static bool updateVirtualEeprom(void *, uint16_t address, uint8_t value) {
  if (address >= sizeof(virtualEeprom)) {
    return false;
  }
  if (virtualEeprom[address] != value) {
    virtualEeprom[address] = value;
  }
  return true;
}

static void ensureVirtualEeprom() {
  if (virtualEepromReady) {
    return;
  }
  std::fill(virtualEeprom, virtualEeprom + sizeof(virtualEeprom), 0xFF);
  virtualStorage.begin(0, readVirtualEeprom, updateVirtualEeprom);
  virtualStorage.update(ThermioStorageLayout::ScheduleMagicAddress,
                        ThermioStorageLayout::ScheduleMagicValue);
  regulatorStore.begin(virtualStorage, ThermioStorageLayout::HeatingStateFirstAddress);
  virtualEepromReady = true;
}

static int halfFromCelsius(double tempC) {
  return static_cast<int>(std::lround(tempC * 2.0));
}

extern "C" {

WASM_KEEPALIVE
const char* buildGreetings(const char* name) {
  static std::string result;

  const char* safeName = name && name[0] != '\0' ? name : "inconnu";
  result = "Bonjour ";
  result += safeName;

  return result.c_str();
}

WASM_KEEPALIVE
void setupThermostat(double baseTemp) {
  ensureVirtualEeprom();
  configuredBaseTemp = baseTemp;
  learning.setup(baseTemp, virtualStorage);
  regulator.reset();
  if (!regulatorStore.load(regulator)) {
    regulatorStore.save(regulator);
  }
}

WASM_KEEPALIVE
void resetThermostat() {
  learning.reset();
}

WASM_KEEPALIVE
void loadTypicalWeekThermostat() {
  learning.loadTypicalWeek();
}

WASM_KEEPALIVE
const char* inspectVirtualEeprom() {
  static std::string result;
  ensureVirtualEeprom();
  result = "{\"size\":" + std::to_string(sizeof(virtualEeprom));
  result += ",\"schedule\":{\"magicAddress\":";
  result += std::to_string(ThermioStorageLayout::ScheduleMagicAddress);
  result += ",\"magic\":";
  result += std::to_string(virtualStorage.read(ThermioStorageLayout::ScheduleMagicAddress));
  result += ",\"valid\":";
  result += virtualStorage.read(ThermioStorageLayout::ScheduleMagicAddress) ==
      ThermioStorageLayout::ScheduleMagicValue ? "true" : "false";
  result += ",\"points\":[";

  bool first = true;
  for (uint8_t zone = 0; zone < thermonuino::kZones; zone++) {
    for (uint8_t day = 0; day < ThermioWeeklyProgram::DaysPerWeek; day++) {
      for (uint8_t point = 0; point < ThermioStorageLayout::SchedulePointsPerDay; point++) {
        const uint16_t address = ThermioStorageLayout::ScheduleFirstPointAddress +
            (((uint16_t)zone * ThermioWeeklyProgram::DaysPerWeek + day) *
                 ThermioStorageLayout::SchedulePointsPerDay + point) * 2U;
        const uint8_t slot = virtualStorage.read(address);
        if (slot == ThermioWeeklyProgram::UnsetSlot) {
          continue;
        }
        const int8_t targetHalfC = (int8_t)virtualStorage.read(address + 1);
        char pointJson[150];
        std::snprintf(pointJson,
                      sizeof(pointJson),
                      "%s{\"address\":%u,\"zone\":%u,\"day\":%u,\"slot\":%u,"
                      "\"targetHalfC\":%d}",
                      first ? "" : ",",
                      address,
                      zone + 1,
                      day,
                      slot,
                      targetHalfC);
        result += pointJson;
        first = false;
      }
    }
  }
  result += "]},\"heating\":{\"address\":";
  result += std::to_string(ThermioStorageLayout::HeatingStateFirstAddress);
  result += ",\"magic\":";
  result += std::to_string(virtualStorage.read(ThermioStorageLayout::HeatingStateFirstAddress));
  result += ",\"version\":";
  result += std::to_string(virtualStorage.read(ThermioStorageLayout::HeatingStateFirstAddress + 1));

  ThermioHeatingRegulator storedRegulator;
  storedRegulator.reset();
  const bool heatingValid = regulatorStore.load(storedRegulator);
  ThermioHeatingRegulator::State state;
  storedRegulator.readState(state);
  result += ",\"valid\":";
  result += heatingValid ? "true" : "false";
  result += ",\"responseBtuPerC\":";
  result += std::to_string(state.responseBtuPerC);
  result += ",\"zones\":[";
  for (uint8_t zone = 0; zone < ThermioHeatingRegulator::ZoneCount; zone++) {
    char zoneJson[100];
    std::snprintf(zoneJson,
                  sizeof(zoneJson),
                  "%s{\"zone\":%u,\"holdBtuPerHour\":%u,\"confidence\":%u}",
                  zone == 0 ? "" : ",",
                  zone + 1,
                  state.holdBtuPerHour[zone],
                  state.holdConfidence[zone]);
    result += zoneJson;
  }
  result += "],\"raw\":[";
  for (uint8_t i = 0; i < ThermioHeatingStateStore::StoredSize; i++) {
    if (i > 0) {
      result += ',';
    }
    result += std::to_string(
        virtualStorage.read(ThermioStorageLayout::HeatingStateFirstAddress + i));
  }
  result += "]}}";
  return result.c_str();
}

WASM_KEEPALIVE
void resetHeatingRegulator() {
  ensureVirtualEeprom();
  regulator.reset();
  regulatorStore.save(regulator);
}

WASM_KEEPALIVE
const char* evaluateHeatingRegulator(
    int zone,
    int measuredDeciC,
    int targetDeciC,
    int installedPowerW,
    int doorOpen,
    int stopMode) {
  static std::string result;
  const ThermioHeatingRegulator::Decision decision = regulator.decide(
      zone,
      measuredDeciC,
      targetDeciC,
      installedPowerW,
      doorOpen != 0,
      stopMode != 0);

  char buffer[520];
  std::snprintf(
      buffer,
      sizeof(buffer),
      "{\"installedPowerW\":%u,\"requestedPowerW\":%u,\"workload\":%u,"
      "\"learnedHoldBtuPerHour\":%u,\"learnedResponseBtuPerC\":%lu,"
      "\"maintenanceBtuPerHour\":%u,\"catchupBtuPerHour\":%u,"
      "\"requestedBtuPerHour\":%u,\"holdConfidence\":%u,\"heating\":%s}",
      decision.installedPowerW,
      decision.requestedPowerW,
      decision.workload,
      decision.learnedHoldBtuPerHour,
      (unsigned long)decision.learnedResponseBtuPerC,
      decision.maintenanceBtuPerHour,
      decision.catchupBtuPerHour,
      decision.requestedBtuPerHour,
      decision.holdConfidence,
      decision.heating ? "true" : "false");
  result = buffer;
  return result.c_str();
}

WASM_KEEPALIVE
void observeHeatingRegulator(
    int zone,
    int measuredBeforeDeciC,
    int measuredAfterDeciC,
    int installedPowerW,
    int heatBtuPerHour,
    int maintenanceBtuPerHour,
    int learningEnabled) {
  regulator.observe(
      zone,
      measuredBeforeDeciC,
      measuredAfterDeciC,
      installedPowerW <= 0 ? ThermioHeatingRegulator::DefaultInstalledPowerW : installedPowerW,
      heatBtuPerHour < 0 ? 0 : heatBtuPerHour,
      maintenanceBtuPerHour < 0 ? 0 : maintenanceBtuPerHour,
      learningEnabled != 0);
  regulatorStore.save(regulator);
}

WASM_KEEPALIVE
const char* evaluateThermostatSlot(
    int absoluteSlot,
    double measuredTemp,
    double userVariation,
    int presenceDetected,
    int replayOnly) {
  return evaluateThermostatSlotEx(
      absoluteSlot,
      measuredTemp,
      userVariation,
      presenceDetected,
      replayOnly,
      std::abs(userVariation) >= 0.001 ? 1 : 0,
      0,
      1,
      0);
}

WASM_KEEPALIVE
const char* evaluateThermostatSlotEx(
    int absoluteSlot,
    double measuredTemp,
    double userVariation,
    int presenceDetected,
    int replayOnly,
    int explicitUserAction,
    int commitSetpoint,
    int learningEnabled,
    int doorOpened) {
  static std::string result;

  const int userVariationHalf = halfFromCelsius(userVariation);
  const thermonuino::LearningDecision decision = learning.evaluate(
      absoluteSlot,
      0,
      measuredTemp,
      userVariationHalf,
      explicitUserAction != 0,
      commitSetpoint != 0,
      learningEnabled != 0,
      presenceDetected != 0,
      doorOpened != 0,
      replayOnly != 0);

  const double defaultTarget = thermonuino::celsiusFromHalf(decision.defaultTargetHalf);
  const double learnedTarget = thermonuino::celsiusFromHalf(decision.targetHalf);
  const int power = decision.workload * 100 / 255;

  char buffer[1400];
  std::snprintf(
      buffer,
      sizeof(buffer),
      "{\"absoluteSlot\":%d,\"slotOfWeek\":%d,\"day\":%d,\"hour\":%d,\"minute\":%d,"
      "\"zone\":%d,\"mode\":\"%s\",\"baseTemp\":%.2f,\"userVariation\":%.2f,"
      "\"learningEnabled\":%s,"
      "\"presenceDetected\":%s,\"previousPresenceDetected\":%s,"
      "\"doorOpened\":%s,\"doorOpenHabit\":%u,"
      "\"target\":%.2f,\"learnedTarget\":%.2f,\"measured\":%.2f,"
      "\"heating\":%s,\"idle\":%s,\"power\":%d,\"installedPowerW\":%d,"
      "\"requestedPowerW\":%d,\"workload\":%u,\"replayOnly\":%s,"
      "\"sourceSlot\":%d,\"sourceDay\":%d,\"hasProgramTarget\":%s,"
      "\"explicitUserAction\":%s,\"scheduleChanged\":%s,"
      "\"committedUserAction\":%s,\"temporaryOverrideActive\":%s}",
      absoluteSlot,
      decision.slotOfWeek,
      decision.day,
      decision.hour,
      decision.minute,
      decision.zone,
      decision.hasProgramTarget ? "program" : "base",
      configuredBaseTemp,
      userVariation,
      learningEnabled ? "true" : "false",
      decision.presenceDetected ? "true" : "false",
      decision.previousPresenceDetected ? "true" : "false",
      decision.doorOpened ? "true" : "false",
      decision.doorOpenHabit,
      defaultTarget,
      learnedTarget,
      measuredTemp,
      decision.heating ? "true" : "false",
      decision.idle ? "true" : "false",
      power,
      decision.installedPowerW,
      decision.requestedPowerW,
      decision.workload,
      replayOnly ? "true" : "false",
      decision.sourceSlot,
      decision.sourceDay,
      decision.hasProgramTarget ? "true" : "false",
      decision.explicitUserAction ? "true" : "false",
      decision.scheduleChanged ? "true" : "false",
      decision.committedUserAction ? "true" : "false",
      decision.temporaryOverrideActive ? "true" : "false");

  result = buffer;
  return result.c_str();
}

}
