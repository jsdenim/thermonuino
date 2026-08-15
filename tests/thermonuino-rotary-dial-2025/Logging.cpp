#include "Logging.h"

static Stream* s_log = nullptr;

void logBegin(Stream& s) { s_log = &s; }

static inline void logPrefix(const __FlashStringHelper* p) {
  if (!s_log) return;
  s_log->print(p);
}

void debug(const String& msg)                        { if(!s_log) return; logPrefix(F("DEBG ")); s_log->println(msg); }
void debug(const char* msg)                          { if(!s_log) return; logPrefix(F("DEBG ")); s_log->println(msg); }
void debug(const __FlashStringHelper* msg)           { if(!s_log) return; logPrefix(F("DEBG ")); s_log->println(msg); }

void info (const String& msg)                        { if(!s_log) return; logPrefix(F("INFO ")); s_log->println(msg); }
void info (const char* msg)                          { if(!s_log) return; logPrefix(F("INFO ")); s_log->println(msg); }
void info (const __FlashStringHelper* msg)           { if(!s_log) return; logPrefix(F("INFO ")); s_log->println(msg); }

static String hhmm(){ 
  char b[6]; 
  uint8_t h = SvcClock_hourLocal(); 
  uint8_t m = SvcClock_minuteLocal(); 
  snprintf(b, sizeof(b), "%02u:%02u", h, m); 
  return String(b); 
}