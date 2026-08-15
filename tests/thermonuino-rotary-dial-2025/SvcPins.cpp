#include "SvcPins.h"
void SvcPins_init() {
  pinMode(PIN_BTN_A, INPUT_PULLUP);
  pinMode(PIN_BTN_B, INPUT_PULLUP);
  pinMode(PIN_BTN_C, INPUT_PULLUP);
  pinMode(PIN_STP_IB1, OUTPUT);
  pinMode(PIN_STP_IA1, OUTPUT);
  pinMode(PIN_STP_IB2, OUTPUT);
  pinMode(PIN_STP_IA2, OUTPUT);
  coilsOff();
}