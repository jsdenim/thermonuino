#pragma once
#include <Arduino.h>  // <- nécessaire pour String
#include "SvcClock.h"

// Initialise le flux de log (SoftwareSerial, Serial, etc.)
void logBegin(Stream& s);

// Overloads légères (pas de copies inutiles)
void debug(const String& msg);
void debug(const char* msg);
void debug(const __FlashStringHelper* msg);

void info(const String& msg);
void info(const char* msg);
void info(const __FlashStringHelper* msg);

String hhmm();