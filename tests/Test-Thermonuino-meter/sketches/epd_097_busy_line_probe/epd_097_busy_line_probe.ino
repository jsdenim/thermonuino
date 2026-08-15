/*
  Probe bas niveau de la ligne BUSY ePaper.

  Objectif:
    Distinguer une ligne BUSY flottante / non connectee d'une ligne pilotee
    par le controleur ePaper.

  D'apres pinout.txt:
    EPD_BUSY  PCINT10 / PC2 / A2
    EPD_RES   PCINT20 / PD4 / D4
    EPD_CS    PCINT22 / PD6 / D6
    EPD_DC    PCINT19 / PD3 / D3
    LED       PCINT21 / PD5 / D5
    RF_CSN    PCINT2  / PB2 / D10, garder HIGH
    RF_EN     PCINT0  / PB0 / D8, garder RF coupe

  Lecture des codes:
    Sequence A: niveau BUSY en INPUT normal
      1 blink = LOW
      2 blinks = HIGH

    Sequence B: niveau BUSY avec pull-up interne activee
      1 blink = LOW
      2 blinks = HIGH

    Sequence C: reaction pendant 10 pulses RESET
      1 blink = aucune transition detectee
      3 blinks = transition detectee

  Interpretation rapide:
    A=1 puis B=2: BUSY probablement flottant ou piste/FPC ouvert.
    A=1 puis B=1: BUSY force a LOW, court-circuit ou ecran bloque.
    A=2 puis B=2: BUSY force a HIGH, court-circuit/pull-up externe/ecran bloque.
    C=3: le reset fait bouger BUSY, donc la ligne repond.
*/

const byte PIN_EPD_BUSY = A2;
const byte PIN_EPD_RST = 4;
const byte PIN_EPD_DC = 3;
const byte PIN_EPD_CS = 6;
const byte PIN_LED = 5;
const byte PIN_RF_CSN = 10;
const byte PIN_RF_EN = 8;

// AO3401A P-MOS en high-side: gate HIGH = RF OFF. Inverser si ton PCB differe.
const byte RF_EN_OFF_LEVEL = HIGH;

void blinkLed(byte count, unsigned int onMs = 140, unsigned int offMs = 180) {
  delay(700);
  for (byte i = 0; i < count; i++) {
    digitalWrite(PIN_LED, HIGH);
    delay(onMs);
    digitalWrite(PIN_LED, LOW);
    delay(offMs);
  }
}

byte stableBusyRead(byte mode) {
  pinMode(PIN_EPD_BUSY, mode);
  delay(100);

  byte highs = 0;
  for (byte i = 0; i < 25; i++) {
    if (digitalRead(PIN_EPD_BUSY) == HIGH) {
      highs++;
    }
    delay(4);
  }

  return (highs >= 13) ? HIGH : LOW;
}

bool pulseResetAndLookForBusyTransition() {
  pinMode(PIN_EPD_BUSY, INPUT);
  delay(50);

  byte last = digitalRead(PIN_EPD_BUSY);
  bool changed = false;

  for (byte pulse = 0; pulse < 10; pulse++) {
    digitalWrite(PIN_EPD_RST, LOW);
    delay(40);
    digitalWrite(PIN_EPD_RST, HIGH);

    unsigned long startedAt = millis();
    while (millis() - startedAt < 250) {
      byte now = digitalRead(PIN_EPD_BUSY);
      if (now != last) {
        changed = true;
      }
      last = now;
      delay(2);
    }

    delay(100);
  }

  return changed;
}

void setup() {
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  pinMode(PIN_RF_EN, OUTPUT);
  digitalWrite(PIN_RF_EN, RF_EN_OFF_LEVEL);

  pinMode(PIN_RF_CSN, OUTPUT);
  digitalWrite(PIN_RF_CSN, HIGH);

  pinMode(PIN_EPD_CS, OUTPUT);
  digitalWrite(PIN_EPD_CS, HIGH);

  pinMode(PIN_EPD_DC, OUTPUT);
  digitalWrite(PIN_EPD_DC, LOW);

  pinMode(PIN_EPD_RST, OUTPUT);
  digitalWrite(PIN_EPD_RST, HIGH);

  blinkLed(1, 60, 80);

  byte busyInput = stableBusyRead(INPUT);
  blinkLed(busyInput == HIGH ? 2 : 1);

  byte busyPullup = stableBusyRead(INPUT_PULLUP);
  blinkLed(busyPullup == HIGH ? 2 : 1);

  bool resetMovedBusy = pulseResetAndLookForBusyTransition();
  blinkLed(resetMovedBusy ? 3 : 1);
}

void loop() {
}
