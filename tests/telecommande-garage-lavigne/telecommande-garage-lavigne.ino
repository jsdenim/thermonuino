/*
  Thermonuino "Pivot" : télécommande porte de garage (WL4456 + ATmega328P / MiniCore)

  Objectif :
  - Conso minimale au repos : sleep profond (POWER-DOWN)
  - Réveil uniquement sur bouton BOOST (BTA1 sur PD7 / D7)
  - Quand on appuie sur BOOST : envoi RF pendant 15 secondes
  - Après le cycle d’émission : mesure batterie 2 fois (1s d’écart), moyenne
    -> si batterie faible : clignotement LED RF (via RF_EN) avec un pattern distinct,
       uniquement APRÈS usage (“peut-être la dernière fois…”)

  Hypothèses PCB :
  - RFOUT1 (DIN WL4456) : PD4 / Arduino D4
  - RF_EN (P-MOS high-side) : PD5 / Arduino D5
      *IMPORTANT* : P-MOS => gate LOW = ON ; gate HIGH = OFF
  - BOOST (BTA1) : PD7 / Arduino D7, bouton vers GND, INPUT_PULLUP
  - BAT_SENSE : PC1 / A1 (pont 1M/1M => Vadc = Vbat/2)
  - Alim MCU : 3.3V

  NOTE :
  - La librairie RF433send.h doit être disponible (celle que tu utilises déjà).
*/

#include <Arduino.h>
#include <avr/sleep.h>
#include <avr/power.h>
#include <avr/interrupt.h>
#include "RF433send.h"

// -------------------- Pins (Arduino numbering) --------------------
static const uint8_t PIN_RFOUT    = 4;   // PD4 -> DIN WL4456
static const uint8_t PIN_RFEN     = 5;   // PD5 -> gate P-MOS (LOW=ON)
static const uint8_t PIN_BOOSTBTN = 7;   // PD7 -> bouton BOOST
static const uint8_t PIN_BATSENSE = A1;  // PC1 ADC1

// -------------------- RF power control --------------------
#define RF_ON()   digitalWrite(PIN_RFEN, LOW)   // P-MOS: LOW=ON
#define RF_OFF()  digitalWrite(PIN_RFEN, HIGH)  // P-MOS: HIGH=OFF

// -------------------- Battery measurement --------------------
// Pont diviseur 1M/1M => Vadc = Vbat/2 ; Vbat = 2*Vadc
static const float ADC_REF_V = 3.3f;   // AVcc
static const float DIV_RATIO = 0.5f;   // validé par ton schéma

// Seuils alcaline AA (sous charge / après usage)
static const uint16_t BAT_WARN_MV = 1150;  // "fatiguée"
static const uint16_t BAT_CRIT_MV = 1050;  // "critique"

// -------------------- RF send parameters --------------------
RfSend *tx = nullptr;

// Code RF (ton code validé)
static uint8_t data_code[] = { 0x11, 0x82, 0x08, 0x2f, 0x5f };

// -------------------- Timing --------------------
static const uint32_t SEND_WINDOW_MS = 15000; // 15s d'émission
static const uint16_t SEND_GAP_MS    = 500;    // intervalle entre envois (à ajuster)

// Pattern warning (distinct de l’émission RF)
static const uint16_t WARN_FLASH_ON_MS  = 80;
static const uint16_t WARN_FLASH_OFF_MS = 520;

// -------------------- Wake on button (PCINT) --------------------
volatile bool g_boostIrq = false;

// ISR PCINT port D (PD0..PD7)
ISR(PCINT2_vect) {
  g_boostIrq = true;
}

static void setupBoostWake() {
  pinMode(PIN_BOOSTBTN, INPUT_PULLUP);

  // Enable pin-change interrupt for port D
  PCICR |= (1 << PCIE2);
  // Enable PCINT23 (PD7)
  PCMSK2 |= (1 << PCINT23);

  // Clear any pending
  PCIFR |= (1 << PCIF2);
}

// -------------------- Low power sleep --------------------
static void goToSleep() {
  // (optionnel) couper des périphériques comme tu fais déjà
  /*
  power_spi_disable();
  power_timer1_disable();
  power_timer2_disable();

  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();

  noInterrupts();

  // 1) Efface tout PCINT pending (sinon réveil immédiat)
  PCIFR |= (1 << PCIF2);

  // 2) Coupe le BOD pendant le sleep (BOD reste actif quand le CPU tourne)
  MCUCR |= (1 << BODS) | (1 << BODSE);
  MCUCR = (MCUCR & ~(1 << BODSE)) | (1 << BODS);

  interrupts();

  sleep_cpu();      // BOD OFF pendant le sommeil
  sleep_disable();

  power_timer2_enable();
  power_timer1_enable();
  power_spi_enable();
  */
}

// -------------------- Battery read --------------------
static uint16_t readBattery_mV_once() {
  // active ADC uniquement pendant la mesure
  ADCSRA |= (1 << ADEN);
  delay(2);

  uint32_t acc = 0;
  for (uint8_t i = 0; i < 8; i++) {
    acc += analogRead(PIN_BATSENSE);
    delay(2);
  }

  ADCSRA &= ~(1 << ADEN);

  float adc = acc / 8.0f;
  float vadc = (adc / 1023.0f) * ADC_REF_V;
  float vbat = vadc / DIV_RATIO; // *2
  return (uint16_t)(vbat * 1000.0f + 0.5f);
}

static uint16_t readBattery_mV_avg_2x_1s() {
  uint16_t a = readBattery_mV_once();
  delay(1000);
  uint16_t b = readBattery_mV_once();
  return (uint16_t)(((uint32_t)a + (uint32_t)b) / 2U);
}

// -------------------- Post-use warning (via RF_EN LED) --------------------
static void postUseBatteryWarning(uint16_t mv_avg) {
  // IMPORTANT : ne pas émettre de radio pendant le warning
  digitalWrite(PIN_RFOUT, LOW);

  if (mv_avg > BAT_WARN_MV) return; // OK

  // 3 flashes si warn, 6 flashes si critique
  uint8_t flashes = (mv_avg <= BAT_CRIT_MV) ? 6 : 3;

  for (uint8_t i = 0; i < flashes; i++) {
    RF_ON();  delay(WARN_FLASH_ON_MS);
    RF_OFF(); delay(WARN_FLASH_OFF_MS);
  }
}

// -------------------- Send window --------------------
static void sendGarageFor15s_thenWarn() {
  // Alim RF ON
  RF_ON();
  delay(50);

  const uint32_t tEnd = millis() + SEND_WINDOW_MS;

  // Émission continue pendant 15s
  while ((int32_t)(millis() - tEnd) < 0) {
    tx->send(sizeof(data_code), data_code);
    delay(SEND_GAP_MS);
  }

  // Stop RF
  digitalWrite(PIN_RFOUT, LOW);
  RF_OFF();

  // Mesure post-usage : moyenne de 2 mesures à 1s d'écart
  uint16_t mv_avg = readBattery_mV_avg_2x_1s();

  // Warning uniquement après usage
  postUseBatteryWarning(mv_avg);

  // attend relâche bouton (anti-rebond)
  while (digitalRead(PIN_BOOSTBTN) == LOW) delay(5);
}

// -------------------- Setup --------------------
void setup() {
  pinMode(PIN_RFOUT, OUTPUT);
  pinMode(PIN_RFEN, OUTPUT);
  digitalWrite(PIN_RFOUT, LOW);
  RF_OFF();

  setupBoostWake();

  // Init émetteur RF (paramètres que tu avais validés)
  tx = rfsend_builder(
    RfSendEncoding::TRIBIT_INVERTED,
    PIN_RFOUT,
    RFSEND_DEFAULT_CONVENTION,
    4,
    nullptr,
    53316,  // initseq
    0, 0,   // prefixes
    1166,   // first_lo_ign
    394,    // lo_short
    1166,   // lo_long
    0, 0,   // hi_short/hi_long -> mêmes que lo_*
    364,    // lo_last
    53300,  // sep
    37      // nb_bits
  );
}

// -------------------- Main loop --------------------
void loop() {
  // Réveil par changement sur PD7 (PCINT23)
  if (g_boostIrq) {
    g_boostIrq = false;

    // Debounce + validation appui réel
    delay(20);
    if (digitalRead(PIN_BOOSTBTN) == LOW) {
      sendGarageFor15s_thenWarn();
    }
  }

  // Dodo :)
  goToSleep();
}
