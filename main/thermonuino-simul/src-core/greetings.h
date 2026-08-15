#pragma once

extern "C" const char* buildGreetings(const char* name);
extern "C" void setupThermostat(double baseTemp);
extern "C" void resetThermostat();
extern "C" const char* evaluateThermostatSlot(
    int absoluteSlot,
    double measuredTemp,
    double userVariation,
    int presenceDetected,
    int replayOnly);
