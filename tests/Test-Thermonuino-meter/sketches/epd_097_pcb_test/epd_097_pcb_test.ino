/*
  Test PCB Thermonuino - ePaper Good Display 0.97"

  Inspire de demo-gooddisplay-097-despi-c02-uno.ino.

  Cible:
    - ATmega328P 3 V / 8 MHz
    - Good Display GDEM0097T61, 0.97", monochrome, 184 x 88, SSD1680

  D'apres pinout.txt:
    EPD_CS    PCINT22 / PD6 / D6
    EPD_RES   PCINT20 / PD4 / D4
    EPD_DC    PCINT19 / PD3 / D3
    EPD_BUSY  PCINT10 / PC2 / A2
    SPI_MOSI  PCINT3  / PB3 / D11
    SPI_MISO  PCINT4  / PB4 / D12
    SPI_SCK   PCINT5  / PB5 / D13
    LED       PCINT21 / PD5 / D5
    RF_CSN    PCINT2  / PB2 / D10, SPI partage: garder HIGH
    RF_EN     PCINT0  / PB0 / D8, garder RF coupe

  Le sketch ne garde pas de framebuffer complet en RAM: il genere l'image
  ligne par ligne.
*/

#include <SPI.h>
#include <avr/pgmspace.h>

const uint8_t PIN_EPD_BUSY = A2;
const uint8_t PIN_EPD_RST = 4;
const uint8_t PIN_EPD_DC = 3;
const uint8_t PIN_EPD_CS = 6;
const uint8_t PIN_LED = 5;
const uint8_t PIN_RF_CSN = 10;
const uint8_t PIN_RF_EN = 8;

// AO3401A P-MOS en high-side: gate LOW = RF ON, donc HIGH = RF OFF.
// Mettre LOW ici si ton RF_EN coupe l'alim RF avec un niveau bas.
const uint8_t RF_EN_OFF_LEVEL = HIGH;

const uint16_t EPD_WIDTH = 184;
const uint16_t EPD_HEIGHT = 88;
const uint8_t EPD_WIDTH_BYTES = EPD_WIDTH / 8;
const unsigned long EPD_BUSY_TIMEOUT_MS = 12000;

const SPISettings EPD_SPI_SETTINGS(2000000, MSBFIRST, SPI_MODE0);

const char LINE1[] PROGMEM = "THERMONUINO";
const char LINE2[] PROGMEM = "EPD 0.97";
const char LINE3[] PROGMEM = "PCB TEST";

const uint8_t GLYPH_SPACE[5] PROGMEM = {0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t GLYPH_DOT[5] PROGMEM = {0x00, 0x60, 0x60, 0x00, 0x00};
const uint8_t GLYPH_0[5] PROGMEM = {0x3E, 0x51, 0x49, 0x45, 0x3E};
const uint8_t GLYPH_7[5] PROGMEM = {0x01, 0x71, 0x09, 0x05, 0x03};
const uint8_t GLYPH_9[5] PROGMEM = {0x06, 0x49, 0x49, 0x29, 0x1E};
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

const uint8_t *glyphFor(char c) {
  switch (c) {
    case ' ': return GLYPH_SPACE;
    case '.': return GLYPH_DOT;
    case '0': return GLYPH_0;
    case '7': return GLYPH_7;
    case '9': return GLYPH_9;
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

void blinkLed(uint8_t count) {
  for (uint8_t i = 0; i < count; i++) {
    digitalWrite(PIN_LED, HIGH);
    delay(90);
    digitalWrite(PIN_LED, LOW);
    delay(130);
  }
}

void epdCommand(uint8_t command) {
  digitalWrite(PIN_EPD_DC, LOW);
  digitalWrite(PIN_EPD_CS, LOW);
  SPI.transfer(command);
  digitalWrite(PIN_EPD_CS, HIGH);
}

void epdData(uint8_t data) {
  digitalWrite(PIN_EPD_DC, HIGH);
  digitalWrite(PIN_EPD_CS, LOW);
  SPI.transfer(data);
  digitalWrite(PIN_EPD_CS, HIGH);
}

bool epdWaitBusy() {
  unsigned long startAt = millis();

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

  uint16_t localX = x - originX;
  uint16_t localY = y - originY;
  if (localY >= 7 * scale) return false;

  uint8_t charIndex = localX / (6 * scale);
  uint8_t charX = (localX % (6 * scale)) / scale;
  uint8_t charY = localY / scale;
  if (charX >= 5) return false;

  char c = pgm_read_byte(text + charIndex);
  if (c == '\0') return false;

  const uint8_t *glyph = glyphFor(c);
  uint8_t column = pgm_read_byte(glyph + charX);
  return (column & (1 << charY)) != 0;
}

bool imagePixelIsBlack(uint16_t x, uint16_t y) {
  if (x == 0 || y == 0 || x == EPD_WIDTH - 1 || y == EPD_HEIGHT - 1) {
    return true;
  }

  if ((x < 14 && y < 14) ||
      (x > EPD_WIDTH - 15 && y < 14) ||
      (x < 14 && y > EPD_HEIGHT - 15) ||
      (x > EPD_WIDTH - 15 && y > EPD_HEIGHT - 15)) {
    return ((x + y) & 0x02) == 0;
  }

  if ((x > 18 && x < EPD_WIDTH - 19 && (y == 20 || y == 67)) ||
      (y > 8 && y < EPD_HEIGHT - 9 && (x == 18 || x == EPD_WIDTH - 19))) {
    return true;
  }

  if (textPixel(LINE1, 58, 10, 1, x, y)) return true;
  if (textPixel(LINE2, 44, 34, 2, x, y)) return true;
  if (textPixel(LINE3, 70, 73, 1, x, y)) return true;

  if (x > 24 && x < 52 && y > 30 && y < 56) {
    return ((x / 4) + (y / 4)) % 2 == 0;
  }

  if (x > 134 && x < 160 && y > 30 && y < 56) {
    int16_t dx = x - 147;
    int16_t dy = y - 43;
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
        uint16_t x = xb * 8 + bit;
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

void setup() {
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  pinMode(PIN_RF_EN, OUTPUT);
  digitalWrite(PIN_RF_EN, RF_EN_OFF_LEVEL);

  pinMode(PIN_RF_CSN, OUTPUT);
  digitalWrite(PIN_RF_CSN, HIGH);

  pinMode(PIN_EPD_BUSY, INPUT);
  pinMode(PIN_EPD_RST, OUTPUT);
  pinMode(PIN_EPD_DC, OUTPUT);
  pinMode(PIN_EPD_CS, OUTPUT);
  digitalWrite(PIN_EPD_CS, HIGH);
  digitalWrite(PIN_EPD_RST, HIGH);

  blinkLed(1);

  SPI.begin();
  SPI.beginTransaction(EPD_SPI_SETTINGS);

  bool ok = epdInit();
  if (ok) {
    epdWriteGeneratedImage();
    ok = epdRefresh();
    epdSleep();
  }

  SPI.endTransaction();
  SPI.end();

  blinkLed(ok ? 5 : 2);
}

void loop() {
}
