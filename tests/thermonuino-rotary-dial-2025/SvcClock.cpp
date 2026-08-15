#include "SvcClock.h"


// ---------- État interne ----------

// Epoch : "à s_epochRef_ms (millis), l'heure Unix était s_epochBase_s (sec)"
static uint32_t s_epochBase_s  = 0;   // secondes Unix à la référence
static uint32_t s_epochRef_ms  = 0;   // millis() à la référence
static bool     s_epochValid   = false;

// Pseudo-horloge locale : "à s_pseudoRef_ms (millis), on était à s_pseudoRef_min (min du jour)"
static uint32_t s_pseudoRef_ms = 0;   // millis() à la référence
static uint16_t s_pseudoRef_min= 0;   // minute du jour [0..1439]

// Seuils de "rebase" pour éviter tout souci de wrap millis()
static const uint32_t REBASE_EPOCH_S   = 86400UL;  // 1 jour
static const uint32_t REBASE_PSEUDO_MIN= 1440UL;   // 1 jour

// ---------- Helpers ----------

static inline uint32_t ms_now() { return millis(); }

// Différences wrap-safe (unsigned)
static inline uint32_t seconds_since(uint32_t since_ms) {
  return (uint32_t)((ms_now() - since_ms) / 1000UL);
}
static inline uint32_t minutes_since(uint32_t since_ms) {
  return (uint32_t)((ms_now() - since_ms) / 60000UL);
}

// Modulo positif
static inline int32_t mod_pos(int32_t x, int32_t m) {
  int32_t r = x % m;
  return (r < 0) ? (r + m) : r;
}

// ---------- API ----------

void SvcClock_init() {
  s_epochBase_s   = 0;
  s_epochRef_ms   = ms_now();
  s_epochValid    = false;

  s_pseudoRef_ms  = ms_now();
  s_pseudoRef_min = 0; // 00:00

  info(String("[CLOCK] init"));
}

bool SvcClock_hasEpoch() {
  return s_epochValid;
}

void SvcClock_setEpoch(uint32_t epochUtc) {
  s_epochBase_s = epochUtc;
  s_epochRef_ms = ms_now();
  s_epochValid  = true;
  info(String("[CLOCK] epoch set → ") + String(epochUtc));
}

uint32_t SvcClock_now() {
  uint32_t now_ms   = ms_now();
  uint32_t elapsedS = (now_ms - s_epochRef_ms) / 1000UL; // wrap-safe

  if (s_epochValid) {
    // Rebase périodique pour rester loin du wrap millis()
    if (elapsedS > REBASE_EPOCH_S) {
      s_epochBase_s += elapsedS;
      s_epochRef_ms  = now_ms;
      elapsedS       = 0;
      debug(String("[CLOCK] rebase epoch"));
    }
    return s_epochBase_s + elapsedS;
  }

  // Sans epoch, on renvoie un compteur relatif (depuis init)
  return elapsedS;
}

void SvcClock_setTimeHM(uint8_t hour, uint8_t minute) {
  if (hour > 23)   hour   = hour % 24;
  if (minute > 59) minute = minute % 60;
  s_pseudoRef_ms  = ms_now();
  s_pseudoRef_min = (uint16_t)hour * 60u + (uint16_t)minute;
  info(String("[CLOCK] set HM → ") + String(hour) + ":" + (minute<10?"0":"") + String(minute));
}

uint8_t SvcClock_hourLocal(int8_t tzOffsetHours) {
  if (s_epochValid) {
    int32_t t_local = (int32_t)SvcClock_now() + (int32_t)tzOffsetHours * 3600;
    t_local = mod_pos(t_local, 86400);               // 0..86399
    return (uint8_t)((t_local / 3600) % 24);         // 0..23
  }
  // Pseudo-horloge : rebase quotidien si besoin
  uint32_t mins = (uint32_t)s_pseudoRef_min + minutes_since(s_pseudoRef_ms);
  if (mins > REBASE_PSEUDO_MIN) {
    s_pseudoRef_min = (uint16_t)(mins % 1440UL);
    s_pseudoRef_ms  = ms_now();
    debug(String("[CLOCK] rebase pseudo"));
  }
  return (uint8_t)((mins % 1440UL) / 60UL);
}

uint8_t SvcClock_minuteLocal(int8_t tzOffsetHours) {
  if (s_epochValid) {
    int32_t t_local = (int32_t)SvcClock_now() + (int32_t)tzOffsetHours * 3600;
    t_local = mod_pos(t_local, 86400);               // 0..86399
    return (uint8_t)((t_local / 60) % 60);           // 0..59
  }
  uint32_t mins = (uint32_t)s_pseudoRef_min + minutes_since(s_pseudoRef_ms);
  if (mins > REBASE_PSEUDO_MIN) {
    s_pseudoRef_min = (uint16_t)(mins % 1440UL);
    s_pseudoRef_ms  = ms_now();
    debug(String("[CLOCK] rebase pseudo"));
  }
  return (uint8_t)(mins % 60UL);
}
