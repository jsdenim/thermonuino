/*
  Diagnostic avance PCB Thermonuino Sonde / Mesure

  Objectif: comprendre pourquoi le PCB ne repond pas en isolant les blocs:
    1. LED locale
    2. entrees switch / WAKE / PIR
    3. alimentation RF_EN
    4. bus SPI + CC1101
    5. ligne BUSY ePaper + RESET
    6. I2C AHT20/AHT30

  Cible: ATmega328P 3.3 V / 8 MHz.
  Pinout d'apres Specifications Thermonuio.md / PCB Sonde.

  Codes LED de demarrage:
    1 blink  = boot du diagnostic
    2 blinks = debut test RF/CC1101
    3 blinks = CC1101 detecte
    4 blinks = debut test ePaper BUSY/RESET
    5 blinks = BUSY ePaper bouge pendant RESET
    6 blinks = AHT20/AHT30 detecte sur I2C
    8 blinks longs = anomalie detectee sur l'etape precedente

  En boucle:
    - PIR BODYDETECT HIGH: LED fixe pendant 10 s
    - CMD_BTN LOW: relance test RF/CC1101
    - CMD_SENS1 LOW: clignotement simple
    - CMD_SENS2 LOW: clignotement double
    - WAKE change: flash court

  Debug serie:
    - TX debug est sur PCINT17 / D1. Le sketch emet a 9600 bauds sur Serial.
    - Ne pas relier RX/D0 si tu veux tester CMD_SENS1, car CMD_SENS1 est sur D0.
*/

#include <SPI.h>
#include <Wire.h>

const uint8_t PIN_LED = 5;          // PCINT21 / PD5 / D5
const uint8_t PIN_BODYDETECT = 7;   // PCINT23 / PD7 / D7
const uint8_t PIN_WAKE = 2;         // PCINT18 / PD2 / D2

const uint8_t PIN_CMD_SENS1 = 0;    // PCINT16 / PD0 / D0, actif LOW
const uint8_t PIN_CMD_SENS2 = A1;   // PCINT9  / PC1 / A1, actif LOW
const uint8_t PIN_CMD_BTN = A3;     // PCINT11 / PC3 / A3, actif LOW

const uint8_t PIN_RF_EN = 8;        // PCINT0 / PB0 / D8
const uint8_t PIN_RF_GDO0 = 9;      // PCINT1 / PB1 / D9
const uint8_t PIN_RF_CSN = 10;      // PCINT2 / PB2 / D10
const uint8_t PIN_RF_MOSI = 11;     // PCINT3 / PB3 / D11
const uint8_t PIN_RF_MISO = 12;     // PCINT4 / PB4 / D12
const uint8_t PIN_RF_SCK = 13;      // PCINT5 / PB5 / D13

const uint8_t PIN_EPD_BUSY = A2;    // PCINT10 / PC2 / A2
const uint8_t PIN_EPD_RST = 4;      // PCINT20 / PD4 / D4
const uint8_t PIN_EPD_DC = 3;       // PCINT19 / PD3 / D3
const uint8_t PIN_EPD_CS = 6;       // PCINT22 / PD6 / D6

const uint8_t RF_EN_ON_LEVEL = LOW;
const uint8_t RF_EN_OFF_LEVEL = (RF_EN_ON_LEVEL == LOW) ? HIGH : LOW;

const uint8_t AHT_ADDR = 0x38;
const uint8_t CC1101_READ_BURST = 0xC0;
const uint8_t CC1101_PARTNUM = 0x30;
const uint8_t CC1101_VERSION = 0x31;

const unsigned long PIR_HOLD_MS = 10000;
const unsigned int RF_POWER_UP_MS = 25;
const unsigned int CC1101_READY_TIMEOUT_MS = 15;
const unsigned int EPD_BUSY_SAMPLE_MS = 250;

const SPISettings RF_SPI_SETTINGS(1000000, MSBFIRST, SPI_MODE0);

unsigned long pirLedUntil = 0;
unsigned long lastPatternAt = 0;
uint8_t patternPhase = 0;
uint8_t lastWakeLevel = LOW;
bool lastButtonPressed = false;

void ledOn() {
  digitalWrite(PIN_LED, HIGH);
}

void ledOff() {
  digitalWrite(PIN_LED, LOW);
}

void blinkLed(uint8_t count, unsigned int onMs = 120, unsigned int offMs = 150) {
  delay(250);
  for (uint8_t i = 0; i < count; i++) {
    ledOn();
    delay(onMs);
    ledOff();
    delay(offMs);
  }
  delay(250);
}

void blinkError() {
  blinkLed(8, 260, 180);
}

void printByteHex(uint8_t value) {
  if (value < 0x10) Serial.print('0');
  Serial.print(value, HEX);
}

void rfPowerOn() {
  digitalWrite(PIN_EPD_CS, HIGH);
  digitalWrite(PIN_RF_CSN, HIGH);
  digitalWrite(PIN_RF_EN, RF_EN_ON_LEVEL);
  delay(RF_POWER_UP_MS);
}

void rfPowerOff() {
  digitalWrite(PIN_RF_CSN, HIGH);
  digitalWrite(PIN_RF_MOSI, LOW);
  digitalWrite(PIN_RF_SCK, LOW);
  digitalWrite(PIN_RF_EN, RF_EN_OFF_LEVEL);
}

bool waitCc1101Ready(unsigned int timeoutMs) {
  const unsigned long startedAt = millis();
  while (digitalRead(PIN_RF_MISO) == HIGH) {
    if ((uint32_t)(millis() - startedAt) >= timeoutMs) {
      return false;
    }
  }
  return true;
}

bool cc1101ReadStatusRegister(uint8_t address, uint8_t &value) {
  digitalWrite(PIN_RF_CSN, LOW);
  if (!waitCc1101Ready(CC1101_READY_TIMEOUT_MS)) {
    digitalWrite(PIN_RF_CSN, HIGH);
    return false;
  }

  SPI.transfer(CC1101_READ_BURST | address);
  value = SPI.transfer(0x00);
  digitalWrite(PIN_RF_CSN, HIGH);
  return true;
}

bool testCc1101() {
  Serial.println(F("[RF] power on"));
  rfPowerOn();
  SPI.beginTransaction(RF_SPI_SETTINGS);

  uint8_t partnum = 0xFF;
  uint8_t version1 = 0xFF;
  uint8_t version2 = 0xFF;
  const bool readOk =
      cc1101ReadStatusRegister(CC1101_PARTNUM, partnum) &&
      cc1101ReadStatusRegister(CC1101_VERSION, version1) &&
      cc1101ReadStatusRegister(CC1101_VERSION, version2);

  SPI.endTransaction();
  rfPowerOff();

  Serial.print(F("[RF] readOk="));
  Serial.print(readOk ? F("yes") : F("no"));
  Serial.print(F(" PARTNUM=0x"));
  printByteHex(partnum);
  Serial.print(F(" VERSION=0x"));
  printByteHex(version1);
  Serial.print('/');
  printByteHex(version2);
  Serial.println();

  const bool busIsNotFloating = version1 != 0x00 && version1 != 0xFF;
  const bool responseIsStable = version1 == version2;
  const bool partnumLooksValid = partnum == 0x00;
  return readOk && busIsNotFloating && responseIsStable && partnumLooksValid;
}

uint8_t stableDigitalRead(uint8_t pin, uint8_t mode) {
  pinMode(pin, mode);
  delay(60);
  uint8_t highs = 0;
  for (uint8_t i = 0; i < 21; i++) {
    if (digitalRead(pin) == HIGH) highs++;
    delay(3);
  }
  return highs >= 11 ? HIGH : LOW;
}

bool pulseEpdResetAndDetectBusyMove() {
  pinMode(PIN_EPD_BUSY, INPUT);
  delay(30);
  uint8_t last = digitalRead(PIN_EPD_BUSY);
  bool changed = false;

  for (uint8_t pulse = 0; pulse < 8; pulse++) {
    digitalWrite(PIN_EPD_RST, LOW);
    delay(35);
    digitalWrite(PIN_EPD_RST, HIGH);

    const unsigned long startedAt = millis();
    while ((uint32_t)(millis() - startedAt) < EPD_BUSY_SAMPLE_MS) {
      const uint8_t now = digitalRead(PIN_EPD_BUSY);
      if (now != last) changed = true;
      last = now;
      delay(2);
    }
  }

  return changed;
}

bool testEpdBusyLine() {
  digitalWrite(PIN_RF_CSN, HIGH);
  digitalWrite(PIN_RF_EN, RF_EN_OFF_LEVEL);
  digitalWrite(PIN_EPD_CS, HIGH);
  digitalWrite(PIN_EPD_DC, LOW);
  digitalWrite(PIN_EPD_RST, HIGH);

  const uint8_t busyInput = stableDigitalRead(PIN_EPD_BUSY, INPUT);
  const uint8_t busyPullup = stableDigitalRead(PIN_EPD_BUSY, INPUT_PULLUP);
  const bool moved = pulseEpdResetAndDetectBusyMove();

  Serial.print(F("[EPD] BUSY input="));
  Serial.print(busyInput == HIGH ? F("HIGH") : F("LOW"));
  Serial.print(F(" pullup="));
  Serial.print(busyPullup == HIGH ? F("HIGH") : F("LOW"));
  Serial.print(F(" resetMoved="));
  Serial.println(moved ? F("yes") : F("no"));

  return moved;
}

bool testAhtI2c() {
  Wire.beginTransmission(AHT_ADDR);
  const uint8_t err = Wire.endTransmission();
  Serial.print(F("[I2C] AHT 0x38 err="));
  Serial.println(err);
  return err == 0;
}

void printInputs() {
  Serial.print(F("[IN] SENS1="));
  Serial.print(digitalRead(PIN_CMD_SENS1));
  Serial.print(F(" SENS2="));
  Serial.print(digitalRead(PIN_CMD_SENS2));
  Serial.print(F(" BTN="));
  Serial.print(digitalRead(PIN_CMD_BTN));
  Serial.print(F(" WAKE="));
  Serial.print(digitalRead(PIN_WAKE));
  Serial.print(F(" PIR="));
  Serial.println(digitalRead(PIN_BODYDETECT));
}

void runStartupDiagnostics() {
  blinkLed(1);
  Serial.println(F("[BOOT] Thermonuino mesure advanced diag"));
  printInputs();

  blinkLed(2);
  if (testCc1101()) {
    blinkLed(3);
  } else {
    blinkError();
  }

  blinkLed(4);
  if (testEpdBusyLine()) {
    blinkLed(5);
  } else {
    blinkError();
  }

  if (testAhtI2c()) {
    blinkLed(6);
  } else {
    blinkError();
  }

  Serial.println(F("[BOOT] diagnostics done"));
}

void updatePirHold() {
  if (digitalRead(PIN_BODYDETECT) == HIGH) {
    pirLedUntil = millis() + PIR_HOLD_MS;
  }
}

void updateWakeFlash() {
  const uint8_t wakeLevel = digitalRead(PIN_WAKE);
  if (wakeLevel != lastWakeLevel) {
    lastWakeLevel = wakeLevel;
    blinkLed(1, 35, 45);
    printInputs();
  }
}

void updateSwitchLed() {
  if ((int32_t)(pirLedUntil - millis()) > 0) {
    ledOn();
    return;
  }

  const bool sens1 = digitalRead(PIN_CMD_SENS1) == LOW;
  const bool sens2 = digitalRead(PIN_CMD_SENS2) == LOW;

  uint8_t pulses = 0;
  if (sens1 && !sens2) pulses = 1;
  if (sens2 && !sens1) pulses = 2;
  if (sens1 && sens2) pulses = 3;

  if (pulses == 0) {
    patternPhase = 0;
    ledOff();
    return;
  }

  if ((int32_t)(millis() - lastPatternAt) < 0) {
    return;
  }

  const bool onPhase = (patternPhase % 2) == 0 && patternPhase < pulses * 2;
  digitalWrite(PIN_LED, onPhase ? HIGH : LOW);
  patternPhase++;
  if (patternPhase >= pulses * 2 + 2) {
    patternPhase = 0;
    lastPatternAt = millis() + 700;
  } else {
    lastPatternAt = millis() + (onPhase ? 120 : 150);
  }
}

void handleButtonRfRetest() {
  const bool buttonPressed = digitalRead(PIN_CMD_BTN) == LOW;
  if (buttonPressed && !lastButtonPressed) {
    Serial.println(F("[BTN] RF retest"));
    const bool ok = testCc1101();
    blinkLed(ok ? 3 : 8, ok ? 90 : 260, ok ? 110 : 180);
    printInputs();
  }
  lastButtonPressed = buttonPressed;
}

void setup() {
  pinMode(PIN_LED, OUTPUT);
  ledOff();

  pinMode(PIN_BODYDETECT, INPUT);
  pinMode(PIN_WAKE, INPUT);

  pinMode(PIN_CMD_SENS1, INPUT_PULLUP);
  pinMode(PIN_CMD_SENS2, INPUT_PULLUP);
  pinMode(PIN_CMD_BTN, INPUT_PULLUP);

  pinMode(PIN_RF_EN, OUTPUT);
  pinMode(PIN_RF_GDO0, INPUT);
  pinMode(PIN_RF_CSN, OUTPUT);
  pinMode(PIN_RF_MOSI, OUTPUT);
  pinMode(PIN_RF_MISO, INPUT);
  pinMode(PIN_RF_SCK, OUTPUT);
  rfPowerOff();

  pinMode(PIN_EPD_BUSY, INPUT);
  pinMode(PIN_EPD_RST, OUTPUT);
  pinMode(PIN_EPD_DC, OUTPUT);
  pinMode(PIN_EPD_CS, OUTPUT);
  digitalWrite(PIN_EPD_CS, HIGH);
  digitalWrite(PIN_EPD_RST, HIGH);

  Serial.begin(9600);
  Wire.begin();
  SPI.begin();

  lastWakeLevel = digitalRead(PIN_WAKE);
  runStartupDiagnostics();
}

void loop() {
  updatePirHold();
  updateWakeFlash();
  handleButtonRfRetest();
  updateSwitchLed();
  delay(5);
}
