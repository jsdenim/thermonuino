#include "SvcStepper.h"
static const uint8_t seq[4][4] = { { 1, 0, 1, 0 }, { 0, 1, 1, 0 }, { 0, 1, 0, 1 }, { 1, 0, 0, 1 } };
static int idx = 0;

inline void setCoils(uint8_t a1, uint8_t a2, uint8_t b1, uint8_t b2) {
  digitalWrite(PIN_STP_IB1, b1);
  digitalWrite(PIN_STP_IA1, a1);
  digitalWrite(PIN_STP_IB2, b2);
  digitalWrite(PIN_STP_IA2, a2);
}
inline void coilsOff() {
  setCoils(0, 0, 0, 0);
}
inline void setStep(int i) {
  setCoils(seq[i][1], seq[i][3], seq[i][0], seq[i][2]);
}

static void moveSteps(long steps, bool fwd) {
  int start_us, end_us, accel;
  switch (g_ramp) {
    case RampProfile::Soft:
      start_us = 8000;
      end_us = 2000;
      accel = 200;
      break;
    case RampProfile::Fast:
      start_us = 4000;
      end_us = 700;
      accel = 70;
      break;
    default:
      start_us = 5000;
      end_us = 1000;
      accel = 100;
      break;
  }
  int d = start_us;
  for (long i = 0; i < steps; i++) {
    setStep(idx);
    idx = (idx + (fwd ? 1 : 3)) & 3;
    if (i < accel && d > end_us) d -= (start_us - end_us) / accel;
    if (i > steps - accel && d < start_us) d += (start_us - end_us) / accel;
    delayMicroseconds(d);
  }
  delay(200);
  coilsOff();
}

static int32_t stepsForTq(int16_t Tq) {
  if (Tq < TQ_MIN) Tq = TQ_MIN;
  if (Tq > TQ_MAX) Tq = TQ_MAX;
  int32_t ticks = Tq - 16 * 4;
  return g_mech_zero_steps + ticks * STEPS_PER_TICK;
}

void Stepper_displayTq(int16_t Tq) {
  int32_t target = stepsForTq(Tq);
  long d = target - g_current_steps;
  if (!d) return;
  info("[STEPPER] move "+String(d)); 
  moveSteps(labs(d), d > 0);
  g_current_steps = target;
}
void Stepper_displayStar() {
  long d = g_mech_zero_steps - g_current_steps;
  if (d) {
    moveSteps(labs(d), d > 0);
    g_current_steps = g_mech_zero_steps;
  }
}
void Stepper_displayStop() {
  Stepper_displayTq(8 * 4);
}

// Initialise le driver du moteur pas-à-pas.
// - Configure les 4 bobines en sortie
// - Coupe le courant (moteur OFF)
// - Ne fait AUCUN mouvement (l’affichage initial est géré par SvcDisplay_boot)
void SvcStepper_init() {
  // Broches bobines en sortie
  pinMode(PIN_STP_IB1, OUTPUT);
  pinMode(PIN_STP_IA1, OUTPUT);
  pinMode(PIN_STP_IB2, OUTPUT);
  pinMode(PIN_STP_IA2, OUTPUT);

  // Moteur OFF au repos (ULN2003 : mettre les 4 bobines à LOW)
  digitalWrite(PIN_STP_IB1, LOW);
  digitalWrite(PIN_STP_IA1, LOW);
  digitalWrite(PIN_STP_IB2, LOW);
  digitalWrite(PIN_STP_IA2, LOW);

  // (optionnel) réinitialiser l’index séquence si tu l’utilises en statique ici
  // idx = 0;  // décommente si tu as un `static int idx` dans ce fichier
  info("[STEPPER] init"); 
}