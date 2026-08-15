#include "SvcRegul.h"



struct Gains {
  float Kp, Ki_per_s;
};
static Gains G[3] = { { 32, 0.005 }, { 48, 0.010 }, { 64, 0.015 } };
static uint8_t g_gainProfile = 1;


int16_t setpointZone_Tq(uint8_t z) {
  int16_t base = (int16_t)g_setpoint_idx + 32;
  int16_t tz = base + g_zone_offset_q[z];
  if (g_vac.active) tz -= 12;
  return clampTq(tz);
}


static void adapt(uint8_t z, int16_t e_q, int16_t slope_qpm) {
  static uint32_t stable[4] = { 0 };
  bool eSmall = abs(e_q) <= 1;
  bool sSmall = abs(slope_qpm) <= (int16_t)(0.02f * 4 * 1);
  if (eSmall && sSmall) {
    if (!stable[z]) stable[z] = millis();
  } else stable[z] = 0;
  if (stable[z] && millis() - stable[z] > 600000UL) {
    uint8_t Ub = g_ctl[z].U_base;
    uint8_t Uavg = (uint8_t)g_ctl[z].Uavg;
    int16_t n = Ub + ((int16_t)Uavg - Ub) / 20;
    g_ctl[z].U_base = (uint8_t)constrain(n, 0, 255);
  }
  if (eSmall && !sSmall) {
    if (slope_qpm < 0) {
      if (g_ctl[z].U_base < 254) g_ctl[z].U_base++;
    } else {
      if (g_ctl[z].U_base > 0) g_ctl[z].U_base--;
    }
  }
}


void SvcRegul_poll() {
  static uint32_t last = 0;
  if (millis() - last < 1000) return;
  last = millis();
  for (uint8_t z = 0; z < 4; z++) {
    int16_t Tsp = setpointZone_Tq(z);
    int16_t Tm = g_zone_Tq[z];
    int16_t e_q = Tsp - Tm;
    float e = e_q / 4.0f;
    uint8_t U;
    if (e >= 2.0f) U = 255;
    else if (e <= -0.5f) U = 0;
    else {
      auto g = G[g_gainProfile];
      g_ctl[z].I += (int32_t)(e * 1024.0f * g.Ki_per_s);
      if (g_ctl[z].I > 255 * 1024) g_ctl[z].I = 255 * 1024;
      if (g_ctl[z].I < -255 * 1024) g_ctl[z].I = -255 * 1024;
      float Ufb = g.Kp * e + (g_ctl[z].I / 1024.0f);
      int16_t Ui = (int16_t)lroundf((float)g_ctl[z].U_base + Ufb);
      U = (uint8_t)constrain(Ui, 0, 255);
    }
    g_ctl[z].duty = U;
    g_ctl[z].Uavg = ((uint16_t)g_ctl[z].Uavg * 31 + U) / 32;
    adapt(z, e_q, g_zone_slope_qpm[z]);
  }
}