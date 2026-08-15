#include "SvcTemp.h"
#include "ClosedCube_HDC1080.h"
static ClosedCube_HDC1080 hdc;

int16_t g_zone_slope_qpm[4] = { 0, 0, 0, 0 };


static int16_t ema(int16_t p, int16_t s, uint8_t sh) {
  return p + ((s - p) >> sh);
}


void SvcI2C_init() {
  Wire.begin();
  hdc.begin(0x40);
  info("[I2C] init"); 
}
int16_t readBoard_Tq() {
  float t = hdc.readTemperature();
  if (!isnan(t)) return (int16_t)lroundf(t * 4.0f);
  return g_zone_Tq[0];
}


static int16_t readZone_raw(uint8_t z) {
  uint8_t s = g_zone_sensor[z];
  if (s < 3) {
    auto &r = g_rf[s];
    if (r.ok && (millis() - r.lastMs) < RF_TIMEOUT_MS) return r.Tq;
  }
  return readBoard_Tq();
}


void SvcTemp_poll() {
  static uint32_t last = 0;
  if (millis() - last < 1000) return;
  last = millis();
  for (uint8_t z = 0; z < 4; z++) {
    int16_t s = readZone_raw(z);
    int16_t prev = g_zone_Tq[z];
    g_zone_Tq[z] = ema(prev, s, 4);
    int16_t diff = g_zone_Tq[z] - prev;
    int16_t slope = diff * 60;
    g_zone_slope_qpm[z] = ema(g_zone_slope_qpm[z], slope, 5);
  }
}


int16_t currentDisplay_Tq() {
  extern UiState g_ui;
  if (g_ui == UiState::Adjust) {
    int16_t base = (int16_t)g_setpoint_idx + 32;
    if (g_vac.active) base -= 12;
    return clampTq(base);
  }
  if (g_display_sensor < 3) {
    auto &r = g_rf[g_display_sensor];
    if (r.ok && (millis() - r.lastMs) < RF_TIMEOUT_MS) return r.Tq;
  }
  return readBoard_Tq();
}