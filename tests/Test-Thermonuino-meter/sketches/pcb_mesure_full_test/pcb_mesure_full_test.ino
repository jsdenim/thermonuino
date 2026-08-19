/*
  Test complet PCB Thermonuino Sonde / Mesure

  Cible: ATmega328P 3.3 V / 8 MHz.
  Pinout d'apres Specifications Thermonuio.md / PCB Sonde.

  Fonctions testees:
    - affiche une mire sur eInk GoodDisplay 0.97" ;
    - LED PCB sur D5 ;
    - switch SLLB510100: sens 1 / sens 2 / bouton central, actifs a GND ;
    - bouton central: lance un test SPI CC1101 ;
    - test RF OK: clignotement LED 5 fois ;
    - PIR BODYDETECT: LED allumee 10 s.

  Notes:
    - CMD_SENS1 est sur D0/RX, donc ce sketch n'utilise pas Serial.
    - RF_EN partage PCINT0 avec POT_ALIM dans les specs. Ici il est utilise
      comme RF_EN, gate LOW = alimentation RF ON.
*/

#include <SPI.h>
#include <avr/pgmspace.h>

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

const uint16_t EPD_WIDTH = 184;
const uint16_t EPD_HEIGHT = 88;
const uint8_t EPD_WIDTH_BYTES = EPD_WIDTH / 8;
const unsigned long EPD_BUSY_TIMEOUT_MS = 12000;
const unsigned long PIR_HOLD_MS = 10000;
const unsigned long SWITCH_DEBOUNCE_MS = 35;
const unsigned int RF_POWER_UP_MS = 20;

const SPISettings EPD_SPI_SETTINGS(2000000, MSBFIRST, SPI_MODE0);
const SPISettings RF_SPI_SETTINGS(1000000, MSBFIRST, SPI_MODE0);

const uint8_t CC1101_READ_BURST = 0xC0;
const uint8_t CC1101_PARTNUM = 0x30;
const uint8_t CC1101_VERSION = 0x31;

const char LINE1[] PROGMEM = "THERMONUINO";
const char LINE2[] PROGMEM = "PCB SONDE";
const char LINE3[] PROGMEM = "EPD TEST";

const uint8_t GLYPH_SPACE[5] PROGMEM = {0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t GLYPH_DOT[5] PROGMEM = {0x00, 0x60, 0x60, 0x00, 0x00};
const uint8_t GLYPH_B[5] PROGMEM = {0x7F, 0x49, 0x49, 0x49, 0x36};
const uint8_t GLYPH_C[5] PROGMEM = {0x3E, 0x41, 0x41, 0x41, 0x22};
const uint8_t GLYPH_D[5] PROGMEM = {0x7F, 0x41, 0x41, 0x22, 0x1C};
const uint8_t GLYPH_E[5] PROGMEM = {0x7F, 0x49, 0x49, 0x49, 0x41};
const uint8_t GLYPH_H[5] PROGMEM = {0x7F, 0x08, 0x08, 0x08, 0x7F};
const uint8_t GLYPH_I[5] PROGMEM = {0x00, 0x41, 0x7F, 0x41, 0x00};
const uint8_t GLYPH_M[5] PROGMEM = {0x7F, 0x02, 0x0C, 0x02, 0x7F};
const uint8_t GLYPH_N[5] PROGMEM = {0x7F, 0x02, 0x0C, 0x10, 0x7F};
const uint8_t GLYPH_O[5] PROGMEM = {0x3E, 0x41, 0x41, 0x41, 0x3E};
const uint8_t GLYPH_P[5] PROGMEM = {0x7F, 0x09, 0x09, 0x09, 0x06};
const uint8_t GLYPH_R[5] PROGMEM = {0x7F, 0x09, 0x19, 0x29, 0x46};
const uint8_t GLYPH_S[5] PROGMEM = {0x46, 0x49, 0x49, 0x49, 0x31};
const uint8_t GLYPH_T[5] PROGMEM = {0x01, 0x01, 0x7F, 0x01, 0x01};
const uint8_t GLYPH_U[5] PROGMEM = {0x3F, 0x40, 0x40, 0x40, 0x3F};

enum SwitchState : uint8_t {
  SWITCH_NONE,
  SWITCH_SENS1,
  SWITCH_SENS2,
  SWITCH_BUTTON,
  SWITCH_INVALID
};

unsigned long pirLedUntil = 0;
unsigned long lastSwitchReadAt = 0;
SwitchState stableSwitch = SWITCH_NONE;
SwitchState lastRawSwitch = SWITCH_NONE;
uint8_t stableSwitchCount = 0;
bool previousButtonPressed = false;
uint8_t switchBlinkPhase = 0;
unsigned long nextSwitchBlinkAt = 0;

const uint8_t *glyphFor(char c) {
  switch (c) {
    case ' ': return GLYPH_SPACE;
    case '.': return GLYPH_DOT;
    case 'B': return GLYPH_B;
    case 'C': return GLYPH_C;
    case 'D': return GLYPH_D;
    case 'E': return GLYPH_E;
    case 'H': return GLYPH_H;
    case 'I': return GLYPH_I;
    case 'M': return GLYPH_M;
    case 'N': return GLYPH_N;
    case 'O': return GLYPH_O;
    case 'P': return GLYPH_P;
    case 'R': return GLYPH_R;
    case 'S': return GLYPH_S;
    case 'T': return GLYPH_T;
    case 'U': return GLYPH_U;
    default: return GLYPH_SPACE;
  }
}

void ledOn() {
  digitalWrite(PIN_LED, HIGH);
}

void ledOff() {
  digitalWrite(PIN_LED, LOW);
}

void blinkLed(uint8_t count, unsigned int onMs, unsigned int offMs) {
  for (uint8_t i = 0; i < count; i++) {
    ledOn();
    delay(onMs);
    ledOff();
    delay(offMs);
  }
}

void rfPowerOff() {
  digitalWrite(PIN_RF_CSN, HIGH);
  digitalWrite(PIN_RF_EN, RF_EN_OFF_LEVEL);
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

bool epdWaitBusy() {
  const unsigned long startAt = millis();
  while (digitalRead(PIN_EPD_BUSY) == HIGH) {
    if (millis() - startAt > EPD_BUSY_TIMEOUT_MS) {
      return false;
    }
    delay(10);
  }
  return true;
}

void epdReset() {
  digitalWrite(PIN_EPD_RST, LOW);
  delay(20);
  digitalWrite(PIN_EPD_RST, HIGH);
  delay(20);
}

void epdSetRamArea() {
  epdCommand(0x44);
  epdData(0x00);
  epdData(EPD_WIDTH_BYTES - 1);

  epdCommand(0x45);
  epdData((EPD_HEIGHT - 1) & 0xFF);
  epdData((EPD_HEIGHT - 1) >> 8);
  epdData(0x00);
  epdData(0x00);
}

void epdSetRamPointer(uint16_t xByte, uint16_t y) {
  epdCommand(0x4E);
  epdData(xByte);
  epdCommand(0x4F);
  epdData(y & 0xFF);
  epdData(y >> 8);
}

bool epdInit() {
  epdReset();

  epdCommand(0x12);
  if (!epdWaitBusy()) return false;

  epdCommand(0x01);
  epdData((EPD_HEIGHT - 1) & 0xFF);
  epdData((EPD_HEIGHT - 1) >> 8);
  epdData(0x00);

  epdCommand(0x11);
  epdData(0x01);

  epdSetRamArea();
  epdSetRamPointer(0, EPD_HEIGHT - 1);

  epdCommand(0x3C);
  epdData(0x05);

  epdCommand(0x18);
  epdData(0x80);

  epdCommand(0x21);
  epdData(0x00);
  epdData(0x80);

  return true;
}

bool textPixel(const char *text, int16_t originX, int16_t originY,
               uint8_t scale, uint16_t x, uint16_t y) {
  if (x < originX || y < originY) return false;

  const uint16_t localX = x - originX;
  const uint16_t localY = y - originY;
  if (localY >= 7 * scale) return false;

  const uint8_t charIndex = localX / (6 * scale);
  const uint8_t charX = (localX % (6 * scale)) / scale;
  const uint8_t charY = localY / scale;
  if (charX >= 5) return false;

  const char c = pgm_read_byte(text + charIndex);
  if (c == '\0') return false;

  const uint8_t *glyph = glyphFor(c);
  const uint8_t column = pgm_read_byte(glyph + charX);
  return (column & (1 << charY)) != 0;
}

bool imagePixelIsBlack(uint16_t x, uint16_t y) {
  if (x == 0 || y == 0 || x == EPD_WIDTH - 1 || y == EPD_HEIGHT - 1) {
    return true;
  }

  if ((x > 16 && x < EPD_WIDTH - 17 && (y == 20 || y == 67)) ||
      (y > 8 && y < EPD_HEIGHT - 9 && (x == 16 || x == EPD_WIDTH - 17))) {
    return true;
  }

  if (textPixel(LINE1, 58, 10, 1, x, y)) return true;
  if (textPixel(LINE2, 38, 34, 2, x, y)) return true;
  if (textPixel(LINE3, 68, 73, 1, x, y)) return true;

  if (x > 24 && x < 50 && y > 30 && y < 56) {
    return ((x / 4) + (y / 4)) % 2 == 0;
  }

  if (x > 136 && x < 162 && y > 30 && y < 56) {
    const int16_t dx = x - 149;
    const int16_t dy = y - 43;
    return (dx * dx + dy * dy) < 120;
  }

  return false;
}

void epdWriteGeneratedImage() {
  epdSetRamPointer(0, EPD_HEIGHT - 1);

  epdCommand(0x26);
  digitalWrite(PIN_EPD_DC, HIGH);
  digitalWrite(PIN_EPD_CS, LOW);
  for (uint16_t i = 0; i < (uint16_t)EPD_WIDTH_BYTES * EPD_HEIGHT; i++) {
    SPI.transfer(0xFF);
  }
  digitalWrite(PIN_EPD_CS, HIGH);

  epdSetRamPointer(0, EPD_HEIGHT - 1);

  epdCommand(0x24);
  digitalWrite(PIN_EPD_DC, HIGH);
  digitalWrite(PIN_EPD_CS, LOW);
  for (uint16_t y = 0; y < EPD_HEIGHT; y++) {
    for (uint8_t xb = 0; xb < EPD_WIDTH_BYTES; xb++) {
      uint8_t data = 0xFF;
      for (uint8_t bit = 0; bit < 8; bit++) {
        const uint16_t x = xb * 8 + bit;
        if (imagePixelIsBlack(x, y)) {
          data &= ~(0x80 >> bit);
        }
      }
      SPI.transfer(data);
    }
  }
  digitalWrite(PIN_EPD_CS, HIGH);
}

bool epdRefresh() {
  epdCommand(0x22);
  epdData(0xF7);
  epdCommand(0x20);
  return epdWaitBusy();
}

void epdSleep() {
  epdCommand(0x10);
  epdData(0x01);
  delay(100);
}

bool drawStartupScreen() {
  SPI.beginTransaction(EPD_SPI_SETTINGS);
  bool ok = epdInit();
  if (ok) {
    epdWriteGeneratedImage();
    ok = epdRefresh();
    epdSleep();
  }
  SPI.endTransaction();
  return ok;
}

uint8_t cc1101ReadStatusRegister(uint8_t address) {
  digitalWrite(PIN_EPD_CS, HIGH);
  digitalWrite(PIN_RF_CSN, LOW);

  const unsigned long startedAt = millis();
  while (digitalRead(PIN_RF_MISO) == HIGH && (millis() - startedAt) < 10) {
    // Le CC1101 force SO/MISO a 0 quand son interface SPI est prete.
  }

  SPI.transfer(CC1101_READ_BURST | address);
  const uint8_t value = SPI.transfer(0x00);
  digitalWrite(PIN_RF_CSN, HIGH);
  return value;
}

bool testCc1101Spi() {
  digitalWrite(PIN_EPD_CS, HIGH);
  digitalWrite(PIN_RF_EN, RF_EN_ON_LEVEL);
  delay(RF_POWER_UP_MS);

  SPI.beginTransaction(RF_SPI_SETTINGS);
  const uint8_t partnum = cc1101ReadStatusRegister(CC1101_PARTNUM);
  const uint8_t version1 = cc1101ReadStatusRegister(CC1101_VERSION);
  const uint8_t version2 = cc1101ReadStatusRegister(CC1101_VERSION);
  SPI.endTransaction();

  rfPowerOff();

  const bool busIsNotFloating = version1 != 0x00 && version1 != 0xFF;
  const bool responseIsStable = version1 == version2;
  const bool partnumLooksValid = partnum == 0x00;
  return busIsNotFloating && responseIsStable && partnumLooksValid;
}

SwitchState readRawSwitch() {
  uint8_t activeCount = 0;
  SwitchState active = SWITCH_NONE;

  if (digitalRead(PIN_CMD_BTN) == LOW) {
    activeCount++;
    active = SWITCH_BUTTON;
  }
  if (digitalRead(PIN_CMD_SENS1) == LOW) {
    activeCount++;
    active = SWITCH_SENS1;
  }
  if (digitalRead(PIN_CMD_SENS2) == LOW) {
    activeCount++;
    active = SWITCH_SENS2;
  }

  if (activeCount == 0) return SWITCH_NONE;
  if (activeCount > 1) return SWITCH_INVALID;
  return active;
}

void updateSwitchState() {
  if ((uint32_t)(millis() - lastSwitchReadAt) < SWITCH_DEBOUNCE_MS) {
    return;
  }

  lastSwitchReadAt = millis();
  const SwitchState raw = readRawSwitch();
  if (raw == lastRawSwitch) {
    if (stableSwitchCount < 4) stableSwitchCount++;
  } else {
    lastRawSwitch = raw;
    stableSwitchCount = 1;
  }

  if (stableSwitchCount >= 3) {
    stableSwitch = raw;
  }
}

void runRfTestFeedback(bool ok) {
  blinkLed(ok ? 5 : 2, ok ? 120 : 300, ok ? 120 : 300);
  switchBlinkPhase = 0;
  nextSwitchBlinkAt = millis() + 400;
}

void handleButtonRfTest() {
  const bool buttonPressed = stableSwitch == SWITCH_BUTTON;
  if (buttonPressed && !previousButtonPressed) {
    runRfTestFeedback(testCc1101Spi());
  }
  previousButtonPressed = buttonPressed;
}

void updatePirHold() {
  if (digitalRead(PIN_BODYDETECT) == HIGH) {
    pirLedUntil = millis() + PIR_HOLD_MS;
  }
}

void updateLedFromInputs() {
  if ((int32_t)(pirLedUntil - millis()) > 0) {
    ledOn();
    return;
  }

  if (stableSwitch == SWITCH_SENS1 || stableSwitch == SWITCH_SENS2) {
    const uint8_t pulses = stableSwitch == SWITCH_SENS1 ? 1 : 2;
    if ((int32_t)(millis() - nextSwitchBlinkAt) >= 0) {
      const bool onPhase = (switchBlinkPhase % 2) == 0 && switchBlinkPhase < pulses * 2;
      digitalWrite(PIN_LED, onPhase ? HIGH : LOW);
      switchBlinkPhase++;
      if (switchBlinkPhase >= pulses * 2 + 2) {
        switchBlinkPhase = 0;
        nextSwitchBlinkAt = millis() + 700;
      } else {
        nextSwitchBlinkAt = millis() + (onPhase ? 120 : 160);
      }
    }
    return;
  }

  switchBlinkPhase = 0;
  ledOff();
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
  digitalWrite(PIN_RF_CSN, HIGH);
  rfPowerOff();

  pinMode(PIN_EPD_BUSY, INPUT);
  pinMode(PIN_EPD_RST, OUTPUT);
  pinMode(PIN_EPD_DC, OUTPUT);
  pinMode(PIN_EPD_CS, OUTPUT);
  digitalWrite(PIN_EPD_CS, HIGH);
  digitalWrite(PIN_EPD_RST, HIGH);

  SPI.begin();

  blinkLed(1, 70, 120);
  const bool epdOk = drawStartupScreen();
  blinkLed(epdOk ? 3 : 2, epdOk ? 90 : 250, epdOk ? 120 : 250);
}

void loop() {
  updateSwitchState();
  updatePirHold();
  handleButtonRfTest();
  updateLedFromInputs();
  delay(5);
}
