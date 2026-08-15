#include "greetings.h"

#include <algorithm>
#include <cstdio>
#include <string>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define WASM_KEEPALIVE EMSCRIPTEN_KEEPALIVE
#else
#define WASM_KEEPALIVE
#endif

static int trainedComfortSlots = 0;
static double configuredBaseTemp = 17.0;

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
  trainedComfortSlots = 0;
}

WASM_KEEPALIVE
void resetThermostat() {
  trainedComfortSlots = 0;
}

WASM_KEEPALIVE
const char* evaluateThermostatSlot(
    int absoluteSlot,
    double measuredTemp,
    double userVariation,
    int presenceDetected,
    int replayOnly) {
  static std::string result;

  const int slotsPerDay = 96;
  const int slotsPerWeek = slotsPerDay * 7;
  int slotOfWeek = absoluteSlot % slotsPerWeek;
  if (slotOfWeek < 0) {
    slotOfWeek += slotsPerWeek;
  }

  const int day = slotOfWeek / slotsPerDay;
  const int slotOfDay = slotOfWeek % slotsPerDay;
  const int minutes = slotOfDay * 15;
  const int hour = minutes / 60;
  const int minute = minutes % 60;
  const bool weekend = day >= 5;

  const bool morningComfort = !weekend && hour >= 6 && hour < 8;
  const bool eveningComfort = !weekend && hour >= 18 && hour < 22;
  const bool weekendComfort = weekend && hour >= 8 && hour < 23;
  const bool comfortMode = morningComfort || eveningComfort || weekendComfort;

  if (comfortMode && !replayOnly) {
    trainedComfortSlots += 1;
  }

  const double scheduledOffset = comfortMode ? 2.0 : -1.0;
  const double presenceOffset = presenceDetected ? 0.5 : -0.5;
  const double target = configuredBaseTemp + scheduledOffset + presenceOffset + userVariation;
  const double learnedBias = std::min(1.5, trainedComfortSlots / 160.0);
  const double learnedTarget = target + (comfortMode ? learnedBias : 0.0);
  const bool heating = measuredTemp < learnedTarget - 0.2;
  const bool idle = measuredTemp > learnedTarget + 0.2;
  const int power = heating ? static_cast<int>(std::min(100.0, (learnedTarget - measuredTemp) * 35.0)) : 0;

  char buffer[640];
  std::snprintf(
      buffer,
      sizeof(buffer),
      "{\"absoluteSlot\":%d,\"slotOfWeek\":%d,\"day\":%d,\"hour\":%d,\"minute\":%d,"
      "\"mode\":\"%s\",\"baseTemp\":%.2f,\"userVariation\":%.2f,\"presenceDetected\":%s,"
      "\"target\":%.2f,\"learnedTarget\":%.2f,\"measured\":%.2f,"
      "\"heating\":%s,\"idle\":%s,\"power\":%d,\"replayOnly\":%s}",
      absoluteSlot,
      slotOfWeek,
      day,
      hour,
      minute,
      comfortMode ? "comfort" : "eco",
      configuredBaseTemp,
      userVariation,
      presenceDetected ? "true" : "false",
      target,
      learnedTarget,
      measuredTemp,
      heating ? "true" : "false",
      idle ? "true" : "false",
      power,
      replayOnly ? "true" : "false");

  result = buffer;
  return result.c_str();
}

}
