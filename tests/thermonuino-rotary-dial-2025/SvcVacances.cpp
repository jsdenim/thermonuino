#include "SvcVacances.h"

void SvcVacances_poll() {
  if (!g_vac.active) return;
  uint32_t now = millis() / 1000UL;
  if (now >= g_vac.endEpoch) g_vac.active = false;
}