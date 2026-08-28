/*
  Diagnostic direct eInk GoodDisplay 0.97" / SSD1680
  PCB Thermonuino Sonde / Mesure

  Objectif:
    Tester uniquement l'affichage eInk, sans sequence de diagnostic par LED.
    L'ecran est pilote en repere RAM 88x184, comme l'exemple GoodDisplay
    officiel. Sur le PCB, cela correspond au test avec rotation 90 degres.

  Pinout PCB Sonde:
    LED       PCINT21 / PD5 / D5, active HIGH
    RF_CSN    PCINT2  / PB2 / D10, garde HIGH pour isoler le CC1101
    EPD_CS    PCINT22 / PD6 / D6
    EPD_RES   PCINT20 / PD4 / D4
    EPD_DC    PCINT19 / PD3 / D3
    EPD_BUSY  PCINT10 / PC2 / A2
    SPI MOSI  PCINT3  / PB3 / D11
    SPI MISO  PCINT4  / PB4 / D12
    SPI SCK   PCINT5  / PB5 / D13
*/

#include <SPI.h>

const uint8_t PIN_LED = 5;
const uint8_t PIN_RF_CSN = 10;

const uint8_t PIN_EPD_BUSY = A2;
const uint8_t PIN_EPD_RST = 4;
const uint8_t PIN_EPD_DC = 3;
const uint8_t PIN_EPD_CS = 6;

const uint16_t EPD_WIDTH = 88;
const uint16_t EPD_HEIGHT = 184;
const uint8_t EPD_WIDTH_BYTES = EPD_WIDTH / 8;
const unsigned long EPD_BUSY_TIMEOUT_MS = 12000;
const bool DISPLAY_MIRROR_X = true;

const SPISettings EPD_SPI_SETTINGS(500000, MSBFIRST, SPI_MODE0);

bool g_displayOk = false;

void ledOn() {
  digitalWrite(PIN_LED, HIGH);
}

void ledOff() {
  digitalWrite(PIN_LED, LOW);
}

void isolateRfSpi() {
  digitalWrite(PIN_RF_CSN, HIGH);
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

bool epdWaitBusyLow(unsigned long timeoutMs) {
  const unsigned long startedAt = millis();
  while (digitalRead(PIN_EPD_BUSY) == HIGH) {
    if ((uint32_t)(millis() - startedAt) >= timeoutMs) {
      return false;
    }
    delay(10);
  }
  return true;
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

void epdReset() {
  digitalWrite(PIN_EPD_RST, HIGH);
  delay(20);
  digitalWrite(PIN_EPD_RST, LOW);
  delay(20);
  digitalWrite(PIN_EPD_RST, HIGH);
  delay(200);
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

void epdSetRamPointer(uint8_t xByte, uint16_t y) {
  epdCommand(0x4E);
  epdData(xByte);
  epdCommand(0x4F);
  epdData(y & 0xFF);
  epdData(y >> 8);
}

bool epdInit() {
  epdReset();

  epdCommand(0x12);
  const bool swResetOk = epdWaitBusyLow(EPD_BUSY_TIMEOUT_MS);

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

  return swResetOk;
}

uint8_t glyphColumn(char c, uint8_t x) {
  switch (c) {
    case '0': {
      const uint8_t glyph[5] = {0x3E, 0x51, 0x49, 0x45, 0x3E};
      return glyph[x];
    }
    case '1': {
      const uint8_t glyph[5] = {0x00, 0x42, 0x7F, 0x40, 0x00};
      return glyph[x];
    }
    case '4': {
      const uint8_t glyph[5] = {0x18, 0x14, 0x12, 0x7F, 0x10};
      return glyph[x];
    }
    case '8': {
      const uint8_t glyph[5] = {0x36, 0x49, 0x49, 0x49, 0x36};
      return glyph[x];
    }
    case '9': {
      const uint8_t glyph[5] = {0x26, 0x49, 0x49, 0x49, 0x3E};
      return glyph[x];
    }
    case '.': {
      const uint8_t glyph[5] = {0x00, 0x60, 0x60, 0x00, 0x00};
      return glyph[x];
    }
    case 'A': {
      const uint8_t glyph[5] = {0x7E, 0x11, 0x11, 0x11, 0x7E};
      return glyph[x];
    }
    case 'B': {
      const uint8_t glyph[5] = {0x7F, 0x49, 0x49, 0x49, 0x36};
      return glyph[x];
    }
    case 'D': {
      const uint8_t glyph[5] = {0x7F, 0x41, 0x41, 0x22, 0x1C};
      return glyph[x];
    }
    case 'E': {
      const uint8_t glyph[5] = {0x7F, 0x49, 0x49, 0x49, 0x41};
      return glyph[x];
    }
    case 'F': {
      const uint8_t glyph[5] = {0x7F, 0x09, 0x09, 0x09, 0x01};
      return glyph[x];
    }
    case 'G': {
      const uint8_t glyph[5] = {0x3E, 0x41, 0x49, 0x49, 0x7A};
      return glyph[x];
    }
    case 'H': {
      const uint8_t glyph[5] = {0x7F, 0x08, 0x08, 0x08, 0x7F};
      return glyph[x];
    }
    case 'I': {
      const uint8_t glyph[5] = {0x00, 0x41, 0x7F, 0x41, 0x00};
      return glyph[x];
    }
    case 'K': {
      const uint8_t glyph[5] = {0x7F, 0x08, 0x14, 0x22, 0x41};
      return glyph[x];
    }
    case 'L': {
      const uint8_t glyph[5] = {0x7F, 0x40, 0x40, 0x40, 0x40};
      return glyph[x];
    }
    case 'M': {
      const uint8_t glyph[5] = {0x7F, 0x02, 0x0C, 0x02, 0x7F};
      return glyph[x];
    }
    case 'N': {
      const uint8_t glyph[5] = {0x7F, 0x04, 0x08, 0x10, 0x7F};
      return glyph[x];
    }
    case 'O': {
      const uint8_t glyph[5] = {0x3E, 0x41, 0x41, 0x41, 0x3E};
      return glyph[x];
    }
    case 'P': {
      const uint8_t glyph[5] = {0x7F, 0x09, 0x09, 0x09, 0x06};
      return glyph[x];
    }
    case 'R': {
      const uint8_t glyph[5] = {0x7F, 0x09, 0x19, 0x29, 0x46};
      return glyph[x];
    }
    case 'S': {
      const uint8_t glyph[5] = {0x46, 0x49, 0x49, 0x49, 0x31};
      return glyph[x];
    }
    case 'T': {
      const uint8_t glyph[5] = {0x01, 0x01, 0x7F, 0x01, 0x01};
      return glyph[x];
    }
    case 'U': {
      const uint8_t glyph[5] = {0x3F, 0x40, 0x40, 0x40, 0x3F};
      return glyph[x];
    }
    case 'Y': {
      const uint8_t glyph[5] = {0x07, 0x08, 0x70, 0x08, 0x07};
      return glyph[x];
    }
    case 'X': {
      const uint8_t glyph[5] = {0x63, 0x14, 0x08, 0x14, 0x63};
      return glyph[x];
    }
    default:
      return 0x00;
  }
}

bool textPixel(const char *text, uint8_t x0, uint8_t y0, uint16_t x, uint16_t y) {
  if (x < x0 || y < y0 || y >= y0 + 7) {
    return false;
  }

  const uint8_t relX = x - x0;
  const uint8_t charIndex = relX / 6;
  const uint8_t col = relX % 6;
  if (col >= 5) {
    return false;
  }

  char c = text[charIndex];
  if (c == '\0') {
    return false;
  }

  return (glyphColumn(c, col) & (1 << (y - y0))) != 0;
}

bool imagePixelIsBlack(uint16_t x,
                       uint16_t y,
                       bool initOk,
                       bool refreshOk,
                       uint8_t busyInput,
                       uint8_t busyPullup) {
  if (x == 0 || y == 0 || x == EPD_WIDTH - 1 || y == EPD_HEIGHT - 1) {
    return true;
  }

  if ((x < 12 && y < 12) ||
      (x > EPD_WIDTH - 13 && y < 12) ||
      (x < 12 && y > EPD_HEIGHT - 13) ||
      (x > EPD_WIDTH - 13 && y > EPD_HEIGHT - 13)) {
    return ((x + y) & 1) == 0;
  }

  if ((x > 4 && x < EPD_WIDTH - 5 && (y == 22 || y == 145)) ||
      (y > 4 && y < EPD_HEIGHT - 5 && (x == 18 || x == 69))) {
    return true;
  }

  if (textPixel("THERMIO", 23, 8, x, y) ||
      textPixel("EINK 0.97", 17, 32, x, y) ||
      textPixel("ROT 90 DEG", 14, 46, x, y) ||
      textPixel("MIRROR X", 20, 60, x, y) ||
      textPixel("RAM 88X184", 14, 74, x, y) ||
      textPixel(initOk ? "INIT OK" : "INIT TO", 20, 96, x, y) ||
      textPixel(refreshOk ? "REF OK" : "REF TO", 23, 110, x, y) ||
      textPixel(busyInput == HIGH ? "BUSY H" : "BUSY L", 23, 124, x, y) ||
      textPixel(busyPullup == HIGH ? "PULL H" : "PULL L", 23, 138, x, y)) {
    return true;
  }

  if (y > 153 && y < 176) {
    return ((x / 4) + (y / 4)) & 1;
  }

  return false;
}

void epdWriteImage(bool initOk, bool refreshOk, uint8_t busyInput, uint8_t busyPullup) {
  epdSetRamPointer(0, EPD_HEIGHT - 1);
  epdCommand(0x26);
  for (uint16_t i = 0; i < (uint16_t)EPD_WIDTH_BYTES * EPD_HEIGHT; i++) {
    epdData(0xFF);
  }

  epdSetRamPointer(0, EPD_HEIGHT - 1);
  epdCommand(0x24);
  for (uint16_t y = 0; y < EPD_HEIGHT; y++) {
    for (uint8_t xb = 0; xb < EPD_WIDTH_BYTES; xb++) {
      uint8_t data = 0xFF;
      for (uint8_t bit = 0; bit < 8; bit++) {
        const uint16_t x = (uint16_t)xb * 8 + bit;
        const uint16_t sourceX = DISPLAY_MIRROR_X ? (EPD_WIDTH - 1 - x) : x;
        if (imagePixelIsBlack(sourceX, y, initOk, refreshOk, busyInput, busyPullup)) {
          data &= ~(0x80 >> bit);
        }
      }
      epdData(data);
    }
  }
}

bool epdRefresh() {
  epdCommand(0x22);
  epdData(0xF7);
  epdCommand(0x20);
  return epdWaitBusyLow(EPD_BUSY_TIMEOUT_MS);
}

void epdSleep() {
  epdCommand(0x10);
  epdData(0x01);
}

void setup() {
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_RF_CSN, OUTPUT);
  pinMode(PIN_EPD_CS, OUTPUT);
  pinMode(PIN_EPD_DC, OUTPUT);
  pinMode(PIN_EPD_RST, OUTPUT);
  pinMode(PIN_EPD_BUSY, INPUT);

  ledOn();
  isolateRfSpi();

  digitalWrite(PIN_EPD_CS, HIGH);
  digitalWrite(PIN_EPD_DC, LOW);
  digitalWrite(PIN_EPD_RST, HIGH);

  const uint8_t busyInput = stableRead(PIN_EPD_BUSY, INPUT);
  const uint8_t busyPullup = stableRead(PIN_EPD_BUSY, INPUT_PULLUP);
  pinMode(PIN_EPD_BUSY, INPUT);

  SPI.begin();
  SPI.beginTransaction(EPD_SPI_SETTINGS);

  const bool initOk = epdInit();

  epdWriteImage(initOk, false, busyInput, busyPullup);
  epdRefresh();

  epdWriteImage(initOk, true, busyInput, busyPullup);
  const bool refreshOk = epdRefresh();

  epdSleep();
  SPI.endTransaction();

  g_displayOk = initOk && refreshOk;
  if (g_displayOk) {
    ledOff();
  }
}

void loop() {
  if (g_displayOk) {
    return;
  }

  ledOn();
  delay(900);
  ledOff();
  delay(900);
}
