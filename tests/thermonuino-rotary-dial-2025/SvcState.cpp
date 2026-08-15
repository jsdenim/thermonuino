#include "SvcState.h"

int8_t g_zone_offset_q[4] = { 0, -6, -6, 0 };  // chambres −1,5 °C
uint8_t g_setpoint_idx = 44;                   // ~19,00 °C
uint8_t g_zone_sensor[4] = { 3, 3, 3, 3 };     // 3 = boîtier par défaut
uint8_t g_display_sensor = 3;                  // boîtier affichage
Vacation g_vac{};
ZoneCtl g_ctl[4];
int16_t g_zone_Tq[4] = { 20 * 4, 20 * 4, 20 * 4, 20 * 4 };
UiState g_ui = UiState::Idle;
RampProfile g_ramp = RampProfile::Normal;

int32_t g_mech_zero_steps = 0;  // ★
int32_t g_current_steps = 0;

RfReading g_rf[3];   // {ok=false, lowBatt=false, Tq=..., lastMs=0} par défaut




void SvcState_init() { /* Placeholder si besoin d’inits supplémentaires */
  info("[STATE] init"); 
}