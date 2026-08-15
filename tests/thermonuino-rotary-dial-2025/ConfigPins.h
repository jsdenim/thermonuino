#pragma once
#include <Arduino.h>


// Repris du sketch de test (ajuste si nécessaire)
#define PIN_WS2812 PCINT21
#define PIN_BTN_A 9
#define PIN_BTN_B 7
#define PIN_BTN_C 2
#define PIN_RF_RX PCINT20
#define PIN_ROT_CLK 8
#define PIN_ROT_DT 3
#define PIN_ROT_SW 10
#define PIN_STP_IB1 14 // PCINT8  (Arduino D14 = A0)
#define PIN_STP_IA1 15 // PCINT9  (Arduino D15 = A1)
#define PIN_STP_IB2 16 // PCINT10 (Arduino D16 = A2)
#define PIN_STP_IA2 17 // PCINT11 (Arduino D17 = A3)


// Série vers actionneur
static const unsigned long BAUD_ACTIONNEUR = 1200;
static const uint32_t TX_PERIOD_MS = 30000; // 30 s


// Disque : 0,25 °C → « tick » mécanique
static const int32_t STEPS_PER_TICK = 34; // à affiner mécaniquement (≈136 pas/°C)


// RF
static const uint32_t RF_TIMEOUT_MS = 10UL*60UL*1000UL; // 10 minutes


// LED jour/nuit
static const uint8_t LED_DAY_START = 7; // 07:00
static const uint8_t LED_DAY_END = 18; // 18:00


// UI
static const uint32_t UI_IDLE_TIMEOUT_MS = 15000; // 15 s


// Temp. bornes (quarts de degré)
static const int16_t TQ_MIN = 8*4; // 8,00 °C
static const int16_t TQ_MAX = 28*4; // 28,00 °C