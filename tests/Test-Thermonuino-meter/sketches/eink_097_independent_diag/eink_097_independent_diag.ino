/*
  Diagnostic independant eInk GoodDisplay 0.97" / SSD1680
  PCB Thermonuino Sonde / Mesure

  Objectif:
    Tester uniquement la partie eInk, sans CC1101, sans AHT, sans switch.
    Le SSD1680 ne fournit pas d'ID SPI lisible dans ce cablage: la seule
    "reponse" observable est la ligne BUSY. Ce sketch cherche donc a savoir:
      - si BUSY est force, flottant, ou pilote ;
      - si RESET fait bouger BUSY ;
      - si une commande SPI SWRESET fait bouger BUSY ;
      - si un refresh minimal fait bouger BUSY.

  Pinout PCB Sonde:
    LED       PCINT21 / PD5 / D5, active HIGH
    RF_ALIM   PCINT0  / PB0 / D8, aussi appele RF_EN
    RF_CSN    PCINT2  / PB2 / D10, garde HIGH pour isoler le CC1101
    EPD_CS    PCINT22 / PD6 / D6
    EPD_RES   PCINT20 / PD4 / D4
    EPD_DC    PCINT19 / PD3 / D3
    EPD_BUSY  PCINT10 / PC2 / A2
    SPI MOSI  PCINT3  / PB3 / D11
    SPI MISO  PCINT4  / PB4 / D12
    SPI SCK   PCINT5  / PB5 / D13

  Protocole de lecture:
    - RF_ALIM indique le numero du test qui commence, par pulses courts.
    - La LED normale indique ensuite uniquement le resultat du test.

  Tests:
    RF 1 pulse: test du temoin RF_ALIM
      LED normale = suit RF_ALIM ON/OFF pour confirmer la sequence.

    RF 2 pulses: BUSY en INPUT
      LED 1 blink = LOW
      LED 2 blinks = HIGH

    RF 3 pulses: BUSY en INPUT_PULLUP
      LED 1 blink = LOW
      LED 2 blinks = HIGH
      Interpretation: test 2 LOW puis test 3 HIGH suggere BUSY flottant.

    RF 4 pulses: RESET materiel observe sur BUSY
      LED 1 blink = aucune transition BUSY
      LED 3 blinks = BUSY a bouge

    RF 5 pulses: SWRESET SPI observe sur BUSY
      LED 1 blink = aucune transition BUSY apres commande SPI
      LED 4 blinks = BUSY a bouge apres commande SPI

    RF 6 pulses: refresh minimal observe sur BUSY
      LED 1 blink = aucune transition pendant refresh
      LED 5 blinks = BUSY a bouge pendant refresh

    RF 7 pulses: pulse direct de EPD_RST
      LED normale suit les pulses envoyes sur RST.

    RF 8 pulses: pulse direct de EPD_CS
      LED normale suit les pulses envoyes sur CS.

    RF 9 pulses: pulse direct de EPD_DC
      LED normale suit les pulses envoyes sur DC.

    RF 10 pulses: burst SPI visible sur SCK/MOSI
      LED normale reste allumee pendant le burst.

    RF 11 pulses: fin
      LED 10 blinks rapides = diagnostic termine, boucle inactive.

  Resultats notes le 2026-08-18:
    - test 2 = 1 blink: BUSY LOW en INPUT
    - test 3 = 2 blinks: BUSY HIGH avec pull-up interne
    - tests 4, 5, 6 = 1 blink: aucune transition BUSY au reset/SWRESET/refresh

  Interpretation probable:
    BUSY se comporte comme une entree flottante ou non pilotee par l'ecran.
    Le controleur eInk ne semble pas reagir au reset ni aux commandes SPI.
    Verifier en priorite FPC/orientation, alim eInk, masse commune, RST, CS,
    DC, SCK et MOSI.
*/

#include <SPI.h>

const uint8_t PIN_LED = 5;
const uint8_t PIN_RF_ALIM = 8;
const uint8_t PIN_RF_CSN = 10;

const uint8_t PIN_EPD_BUSY = A2;
const uint8_t PIN_EPD_RST = 4;
const uint8_t PIN_EPD_DC = 3;
const uint8_t PIN_EPD_CS = 6;

// AO3401A P-MOS high-side: gate LOW = alimentation RF ON.
const uint8_t RF_ALIM_ON_LEVEL = LOW;
const uint8_t RF_ALIM_OFF_LEVEL = (RF_ALIM_ON_LEVEL == LOW) ? HIGH : LOW;

const uint16_t EPD_WIDTH = 184;
const uint16_t EPD_HEIGHT = 88;
const uint8_t EPD_WIDTH_BYTES = EPD_WIDTH / 8;

const SPISettings EPD_SPI_SETTINGS(500000, MSBFIRST, SPI_MODE0);

void ledOn() {
  digitalWrite(PIN_LED, HIGH);
}

void ledOff() {
  digitalWrite(PIN_LED, LOW);
}

void pauseBetweenCodes() {
  delay(900);
}

void blinkResult(uint8_t count, unsigned int onMs = 400, unsigned int offMs = 480) {
  pauseBetweenCodes();
  for (uint8_t i = 0; i < count; i++) {
    ledOn();
    delay(onMs);
    ledOff();
    delay(offMs);
  }
}

void pulseRfAlim(uint8_t count) {
  pauseBetweenCodes();
  for (uint8_t i = 0; i < count; i++) {
    digitalWrite(PIN_RF_ALIM, RF_ALIM_ON_LEVEL);
    delay(180);
    digitalWrite(PIN_RF_ALIM, RF_ALIM_OFF_LEVEL);
    delay(220);
  }
  delay(450);
}

void rfAlimOff() {
  digitalWrite(PIN_RF_CSN, HIGH);
  digitalWrite(PIN_RF_ALIM, RF_ALIM_OFF_LEVEL);
}

void rfAlimVisualCheck() {
  digitalWrite(PIN_RF_ALIM, RF_ALIM_ON_LEVEL);
  ledOn();
  delay(1200);

  digitalWrite(PIN_RF_ALIM, RF_ALIM_OFF_LEVEL);
  ledOff();
  delay(1200);

  digitalWrite(PIN_RF_ALIM, RF_ALIM_ON_LEVEL);
  ledOn();
  delay(1200);

  rfAlimOff();
  ledOff();
}

uint8_t stableRead(uint8_t pin, uint8_t mode) {
  pinMode(pin, mode);
  delay(80);

  uint8_t highs = 0;
  for (uint8_t i = 0; i < 25; i++) {
    if (digitalRead(pin) == HIGH) {
      highs++;
    }
    delay(4);
  }

  return highs >= 13 ? HIGH : LOW;
}

bool waitBusyChange(uint8_t initialLevel, unsigned long timeoutMs) {
  const unsigned long startedAt = millis();
  while ((uint32_t)(millis() - startedAt) < timeoutMs) {
    if (digitalRead(PIN_EPD_BUSY) != initialLevel) {
      return true;
    }
    delay(2);
  }
  return false;
}

void waitBusyLowOrTimeout(unsigned long timeoutMs) {
  const unsigned long startedAt = millis();
  while (digitalRead(PIN_EPD_BUSY) == HIGH &&
         (uint32_t)(millis() - startedAt) < timeoutMs) {
    delay(5);
  }
}

void epdCommand(uint8_t command) {
  digitalWrite(PIN_RF_CSN, HIGH);
  digitalWrite(PIN_EPD_DC, LOW);
  digitalWrite(PIN_EPD_CS, LOW);
  SPI.transfer(command);
  digitalWrite(PIN_EPD_CS, HIGH);
}

void epdData(uint8_t data) {
  digitalWrite(PIN_RF_CSN, HIGH);
  digitalWrite(PIN_EPD_DC, HIGH);
  digitalWrite(PIN_EPD_CS, LOW);
  SPI.transfer(data);
  digitalWrite(PIN_EPD_CS, HIGH);
}

void pulseOutputLine(uint8_t pin, uint8_t idleLevel, uint8_t activeLevel) {
  pinMode(pin, OUTPUT);
  digitalWrite(pin, idleLevel);
  ledOff();
  delay(300);

  for (uint8_t i = 0; i < 8; i++) {
    digitalWrite(pin, activeLevel);
    ledOn();
    delay(180);
    digitalWrite(pin, idleLevel);
    ledOff();
    delay(220);
  }
}

void spiVisibleBurst() {
  digitalWrite(PIN_EPD_CS, LOW);
  digitalWrite(PIN_EPD_DC, HIGH);
  ledOn();

  SPI.beginTransaction(EPD_SPI_SETTINGS);
  for (uint16_t i = 0; i < 800; i++) {
    SPI.transfer((i & 1) ? 0xAA : 0x55);
  }
  SPI.endTransaction();

  ledOff();
  digitalWrite(PIN_EPD_CS, HIGH);
  digitalWrite(PIN_EPD_DC, LOW);
}

bool pulseResetAndWatchBusy() {
  pinMode(PIN_EPD_BUSY, INPUT);
  const uint8_t before = digitalRead(PIN_EPD_BUSY);

  digitalWrite(PIN_EPD_RST, LOW);
  delay(40);
  digitalWrite(PIN_EPD_RST, HIGH);

  const bool changed = waitBusyChange(before, 700);
  delay(250);
  return changed;
}

bool sendSwResetAndWatchBusy() {
  pinMode(PIN_EPD_BUSY, INPUT);
  const uint8_t before = digitalRead(PIN_EPD_BUSY);

  SPI.beginTransaction(EPD_SPI_SETTINGS);
  epdCommand(0x12);
  const bool changed = waitBusyChange(before, 1200);
  waitBusyLowOrTimeout(5000);
  SPI.endTransaction();

  return changed;
}

void epdMinimalInit() {
  epdCommand(0x01);
  epdData((EPD_HEIGHT - 1) & 0xFF);
  epdData((EPD_HEIGHT - 1) >> 8);
  epdData(0x00);

  epdCommand(0x11);
  epdData(0x01);

  epdCommand(0x44);
  epdData(0x00);
  epdData(EPD_WIDTH_BYTES - 1);

  epdCommand(0x45);
  epdData((EPD_HEIGHT - 1) & 0xFF);
  epdData((EPD_HEIGHT - 1) >> 8);
  epdData(0x00);
  epdData(0x00);

  epdCommand(0x4E);
  epdData(0x00);
  epdCommand(0x4F);
  epdData((EPD_HEIGHT - 1) & 0xFF);
  epdData((EPD_HEIGHT - 1) >> 8);

  epdCommand(0x3C);
  epdData(0x05);
  epdCommand(0x18);
  epdData(0x80);
  epdCommand(0x21);
  epdData(0x00);
  epdData(0x80);
}

void epdWriteCheckerboard() {
  epdCommand(0x26);
  digitalWrite(PIN_EPD_DC, HIGH);
  digitalWrite(PIN_EPD_CS, LOW);
  for (uint16_t i = 0; i < (uint16_t)EPD_WIDTH_BYTES * EPD_HEIGHT; i++) {
    SPI.transfer(0xFF);
  }
  digitalWrite(PIN_EPD_CS, HIGH);

  epdCommand(0x4E);
  epdData(0x00);
  epdCommand(0x4F);
  epdData((EPD_HEIGHT - 1) & 0xFF);
  epdData((EPD_HEIGHT - 1) >> 8);

  epdCommand(0x24);
  digitalWrite(PIN_EPD_DC, HIGH);
  digitalWrite(PIN_EPD_CS, LOW);
  for (uint16_t y = 0; y < EPD_HEIGHT; y++) {
    for (uint8_t xb = 0; xb < EPD_WIDTH_BYTES; xb++) {
      SPI.transfer(((xb + (y / 8)) & 1) ? 0xAA : 0x55);
    }
  }
  digitalWrite(PIN_EPD_CS, HIGH);
}

bool refreshAndWatchBusy() {
  pinMode(PIN_EPD_BUSY, INPUT);
  const uint8_t before = digitalRead(PIN_EPD_BUSY);

  SPI.beginTransaction(EPD_SPI_SETTINGS);
  epdMinimalInit();
  epdWriteCheckerboard();
  epdCommand(0x22);
  epdData(0xF7);
  epdCommand(0x20);
  const bool changed = waitBusyChange(before, 1500);
  waitBusyLowOrTimeout(12000);
  SPI.endTransaction();

  return changed;
}

void setup() {
  pinMode(PIN_LED, OUTPUT);
  ledOff();

  pinMode(PIN_RF_ALIM, OUTPUT);
  pinMode(PIN_RF_CSN, OUTPUT);
  rfAlimOff();

  pinMode(PIN_EPD_BUSY, INPUT);
  pinMode(PIN_EPD_RST, OUTPUT);
  pinMode(PIN_EPD_DC, OUTPUT);
  pinMode(PIN_EPD_CS, OUTPUT);

  digitalWrite(PIN_EPD_CS, HIGH);
  digitalWrite(PIN_EPD_DC, LOW);
  digitalWrite(PIN_EPD_RST, HIGH);

  SPI.begin();

  blinkResult(1, 800, 850);

  pulseRfAlim(1);
  rfAlimVisualCheck();

  pulseRfAlim(2);
  const uint8_t busyInput = stableRead(PIN_EPD_BUSY, INPUT);
  blinkResult(busyInput == HIGH ? 2 : 1);
  //1

  pulseRfAlim(3);
  const uint8_t busyPullup = stableRead(PIN_EPD_BUSY, INPUT_PULLUP);
  blinkResult(busyPullup == HIGH ? 2 : 1);
  //2

  pulseRfAlim(4);
  const bool resetMoved = pulseResetAndWatchBusy();
  blinkResult(resetMoved ? 3 : 1);
  //1

  pulseRfAlim(5);
  const bool swResetMoved = sendSwResetAndWatchBusy();
  blinkResult(swResetMoved ? 4 : 1);
  //4

  pulseRfAlim(6);
  const bool refreshMoved = refreshAndWatchBusy();
  blinkResult(refreshMoved ? 5 : 1);
  //1

  pulseRfAlim(7);
  pulseOutputLine(PIN_EPD_RST, HIGH, LOW);

  pulseRfAlim(8);
  pulseOutputLine(PIN_EPD_CS, HIGH, LOW);

  pulseRfAlim(9);
  pulseOutputLine(PIN_EPD_DC, LOW, HIGH);

  pulseRfAlim(10);
  spiVisibleBurst();

  pulseRfAlim(11);
  blinkResult(10, 800, 850);
  //10
  rfAlimOff();
}

void loop() {
}
