#pragma once
#include "ConfigPins.h"
#include "Types.h"
#include "Logging.h"
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <ClickEncoder.h>
#include <TimerOne.h>
#include "protocole_statut.h"
#include "string.h"


// Globals – déclarations (définies dans SvcState.cpp)
extern uint8_t g_setpoint_idx; // 0..80 (8 → 28 °C)
extern int8_t g_zone_offset_q[4]; // −12..+12 (±3,00 °C)
extern uint8_t g_zone_sensor[4]; // 0..2 RF, 3 boîtier
extern uint8_t g_display_sensor; // 0..2 RF, 3 boîtier
extern Vacation g_vac;
extern ZoneCtl g_ctl[4];
extern int16_t g_zone_Tq[4];
extern int16_t g_zone_slope_qpm[4];
extern UiState g_ui;
extern RampProfile g_ramp;
extern int32_t g_mech_zero_steps;
extern int32_t g_current_steps;
extern RfReading g_rf[3];


// Services API
void SvcPins_init();
void SvcI2C_init();
void SvcLED_init();
void SvcRF_init();
void SvcUI_init();
void SvcStepper_init();
void SvcProto_init();
void SvcClock_init();
uint32_t SvcClock_now();
uint8_t SvcClock_hourLocal();
uint8_t SvcClock_minuteLocal();
void SvcState_init();
void SvcDisplay_boot();


void SvcTemp_poll();
void SvcRegul_poll();
void SvcProto_poll();
void SvcLED_poll();
void SvcVacances_poll();
void SvcUI_poll();
void SvcRF_poll();
void SvcRF_enable(bool on);                              // ON/OFF réception
void SvcRF_onReading(uint8_t slot, int16_t Tq, bool lowBatt);  // callback décodeur → service
void SvcEEPROM_poll();


// Helpers transverses
int16_t readBoard_Tq();
int16_t currentDisplay_Tq();
int16_t setpointZone_Tq(uint8_t z);
void Stepper_displayTq(int16_t Tq);
void Stepper_displayStar();
void Stepper_displayStop();