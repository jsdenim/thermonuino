#include "greetings.h"
#include "thermostat_learning.h"

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
  configuredBaseTemp = baseTemp;
  learning.setup(baseTemp);
}

WASM_KEEPALIVE
void resetThermostat() {
  learning.reset();
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
    int temporaryOverride) {
  static std::string result;

  const int userTargetHalf = halfFromCelsius(configuredBaseTemp + userVariation);
  const thermonuino::LearningDecision decision = learning.evaluate(
      absoluteSlot,
      0,
      measuredTemp,
      userTargetHalf,
      explicitUserAction != 0,
      temporaryOverride != 0,
      presenceDetected != 0,
      replayOnly != 0);

  const double defaultTarget = thermonuino::celsiusFromHalf(decision.defaultTargetHalf);
  const double learnedTarget = thermonuino::celsiusFromHalf(decision.targetHalf);
  const int power = decision.heating
      ? static_cast<int>(std::min(100.0, (learnedTarget - measuredTemp) * 35.0))
      : 0;

  char buffer[1024];
  std::snprintf(
      buffer,
      sizeof(buffer),
      "{\"absoluteSlot\":%d,\"slotOfWeek\":%d,\"day\":%d,\"hour\":%d,\"minute\":%d,"
      "\"zone\":%d,\"mode\":\"%s\",\"baseTemp\":%.2f,\"userVariation\":%.2f,"
      "\"presenceDetected\":%s,\"previousPresenceDetected\":%s,"
      "\"target\":%.2f,\"learnedTarget\":%.2f,\"measured\":%.2f,"
      "\"heating\":%s,\"idle\":%s,\"power\":%d,\"replayOnly\":%s,"
      "\"sourceSlot\":%d,\"sourceDay\":%d,\"confidence\":%u,\"hasLearnedTarget\":%s,"
      "\"explicitUserAction\":%s,\"scheduleChanged\":%s,\"contradiction\":%s,"
      "\"candidateActive\":%s,\"candidateTarget\":%.2f,\"candidateCount\":%u}",
      absoluteSlot,
      decision.slotOfWeek,
      decision.day,
      decision.hour,
      decision.minute,
      decision.zone,
      decision.hasLearnedTarget ? "learned" : "default",
      configuredBaseTemp,
      userVariation,
      decision.presenceDetected ? "true" : "false",
      decision.previousPresenceDetected ? "true" : "false",
      defaultTarget,
      learnedTarget,
      measuredTemp,
      decision.heating ? "true" : "false",
      decision.idle ? "true" : "false",
      power,
      replayOnly ? "true" : "false",
      decision.sourceSlot,
      decision.sourceDay,
      decision.confidence,
      decision.hasLearnedTarget ? "true" : "false",
      decision.explicitUserAction ? "true" : "false",
      decision.scheduleChanged ? "true" : "false",
      decision.contradiction ? "true" : "false",
      decision.candidateActive ? "true" : "false",
      decision.candidateActive ? thermonuino::celsiusFromHalf(decision.candidateHalf) : 0.0,
      decision.candidateCount);

  result = buffer;
  return result.c_str();
}

}
