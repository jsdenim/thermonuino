/*
  Demo Arduino Uno + Good Display DESPI-C02 + ePaper 0.97"

  Cible testee/prevue:
    - Good Display GDEM0097T61, 0.97", monochrome, 184 x 88, SSD1680
    - Adaptateur DESPI-C02, avec eventuel adaptateur 24 -> 18 pins pour 0.97"

  Cablage DESPI-C02 -> Arduino Uno:
    DESPI-C02 3.3V  -> Uno 3.3V
    DESPI-C02 GND   -> Uno GND
    DESPI-C02 SCK   -> Uno D13 / SCK
    DESPI-C02 SDI   -> Uno D11 / MOSI
    DESPI-C02 CS    -> Uno D10
    DESPI-C02 D/C   -> Uno D9
    DESPI-C02 RES   -> Uno D8
    DESPI-C02 BUSY  -> Uno D7

  Important:
    Les panneaux ePaper Good Display sont en logique 3.3V.
    Un Uno classique sort du 5V sur ses pins: utiliser un convertisseur de niveau
    ou au minimum des ponts diviseurs sur SCK, SDI, CS, D/C et RES.
    BUSY va de l'ePaper vers l'Uno, 3.3V est lu comme HIGH par l'ATmega.

  Ce sketch n'utilise pas de framebuffer complet: il genere les 184x88 pixels
  ligne par ligne pour tenir dans la RAM d'un Uno.
*/

#include <SPI.h>
#include <avr/pgmspace.h>

const uint8_t PIN_EPD_BUSY = 7;
const uint8_t PIN_EPD_RST  = 8;
const uint8_t PIN_EPD_DC   = 9;
const uint8_t PIN_EPD_CS   = 10;

const uint16_t EPD_WIDTH = 184;
const uint16_t EPD_HEIGHT = 88;
const uint8_t EPD_WIDTH_BYTES = EPD_WIDTH / 8;
const unsigned long EPD_BUSY_TIMEOUT_MS = 10000;

const SPISettings EPD_SPI_SETTINGS(2000000, MSBFIRST, SPI_MODE0);

const char LINE1[] PROGMEM = "GOOD DISPLAY";
const char LINE2[] PROGMEM = "0.97 EINK";
const char LINE3[] PROGMEM = "UNO + DESPI-C02";

// Glyphes 5x7, colonnes LSB en haut. Seulement les caracteres utilises ici.
const uint8_t GLYPH_SPACE[5] PROGMEM = {0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t GLYPH_PLUS[5]  PROGMEM = {0x08, 0x08, 0x3E, 0x08, 0x08};
const uint8_t GLYPH_DASH[5]  PROGMEM = {0x08, 0x08, 0x08, 0x08, 0x08};
const uint8_t GLYPH_DOT[5]   PROGMEM = {0x00, 0x60, 0x60, 0x00, 0x00};
const uint8_t GLYPH_0[5]     PROGMEM = {0x3E, 0x51, 0x49, 0x45, 0x3E};
const uint8_t GLYPH_2[5]     PROGMEM = {0x42, 0x61, 0x51, 0x49, 0x46};
const uint8_t GLYPH_7[5]     PROGMEM = {0x01, 0x71, 0x09, 0x05, 0x03};
const uint8_t GLYPH_9[5]     PROGMEM = {0x06, 0x49, 0x49, 0x29, 0x1E};
const uint8_t GLYPH_A[5]     PROGMEM = {0x7E, 0x11, 0x11, 0x11, 0x7E};
const uint8_t GLYPH_C[5]     PROGMEM = {0x3E, 0x41, 0x41, 0x41, 0x22};
const uint8_t GLYPH_D[5]     PROGMEM = {0x7F, 0x41, 0x41, 0x22, 0x1C};
const uint8_t GLYPH_E[5]     PROGMEM = {0x7F, 0x49, 0x49, 0x49, 0x41};
const uint8_t GLYPH_G[5]     PROGMEM = {0x3E, 0x41, 0x49, 0x49, 0x7A};
const uint8_t GLYPH_I[5]     PROGMEM = {0x00, 0x41, 0x7F, 0x41, 0x00};
const uint8_t GLYPH_K[5]     PROGMEM = {0x7F, 0x08, 0x14, 0x22, 0x41};
const uint8_t GLYPH_L[5]     PROGMEM = {0x7F, 0x40, 0x40, 0x40, 0x40};
const uint8_t GLYPH_N[5]     PROGMEM = {0x7F, 0x02, 0x0C, 0x10, 0x7F};
const uint8_t GLYPH_O[5]     PROGMEM = {0x3E, 0x41, 0x41, 0x41, 0x3E};
const uint8_t GLYPH_P[5]     PROGMEM = {0x7F, 0x09, 0x09, 0x09, 0x06};
const uint8_t GLYPH_S[5]     PROGMEM = {0x46, 0x49, 0x49, 0x49, 0x31};
const uint8_t GLYPH_U[5]     PROGMEM = {0x3F, 0x40, 0x40, 0x40, 0x3F};
const uint8_t GLYPH_Y[5]     PROGMEM = {0x07, 0x08, 0x70, 0x08, 0x07};

const uint8_t *glyphFor(char c) {
  switch (c) {
    case ' ': return GLYPH_SPACE;
    case '+': return GLYPH_PLUS;
    case '-': return GLYPH_DASH;
    case '.': return GLYPH_DOT;
    case '0': return GLYPH_0;
    case '2': return GLYPH_2;
    case '7': return GLYPH_7;
    case '9': return GLYPH_9;
    case 'A': return GLYPH_A;
    case 'C': return GLYPH_C;
    case 'D': return GLYPH_D;
    case 'E': return GLYPH_E;
    case 'G': return GLYPH_G;
    case 'I': return GLYPH_I;
    case 'K': return GLYPH_K;
    case 'L': return GLYPH_L;
    case 'N': return GLYPH_N;
    case 'O': return GLYPH_O;
    case 'P': return GLYPH_P;
    case 'S': return GLYPH_S;
    case 'U': return GLYPH_U;
    case 'Y': return GLYPH_Y;
    default: return GLYPH_SPACE;
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

void epdWaitBusy() {
  unsigned long startAt = millis();

  Serial.print(F("Attente BUSY"));
  while (digitalRead(PIN_EPD_BUSY) == HIGH) {
    if (millis() - startAt > EPD_BUSY_TIMEOUT_MS) {
      Serial.println(F(" TIMEOUT"));
      return;
    }
    delay(10);
    Serial.print('.');
  }
  Serial.println();
}

void epdReset() {
  digitalWrite(PIN_EPD_RST, LOW);
  delay(20);
  digitalWrite(PIN_EPD_RST, HIGH);
  delay(20);
}

void epdSetRamArea() {
  epdCommand(0x44); // SET_RAM_X_ADDRESS_START_END_POSITION
  epdData(0x00);
  epdData(EPD_WIDTH_BYTES - 1);

  epdCommand(0x45); // SET_RAM_Y_ADDRESS_START_END_POSITION
  // Sequence Good Display/SSD1680 courante pour ce 184x88: Y decroit.
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

void epdInit() {
  epdReset();

  epdCommand(0x12); // SWRESET
  epdWaitBusy();

  epdCommand(0x01); // DRIVER_OUTPUT_CONTROL
  epdData((EPD_HEIGHT - 1) & 0xFF);
  epdData((EPD_HEIGHT - 1) >> 8);
  epdData(0x00);

  epdCommand(0x11); // DATA_ENTRY_MODE_SETTING
  epdData(0x01);    // X+, Y-, comme les samples Good Display SSD1680 petits panneaux

  epdSetRamArea();
  epdSetRamPointer(0, EPD_HEIGHT - 1);

  epdCommand(0x3C); // BORDER_WAVEFORM_CONTROL
  epdData(0x05);

  epdCommand(0x18); // TEMPERATURE_SENSOR_CONTROL
  epdData(0x80);

  epdCommand(0x21); // DISPLAY_UPDATE_CONTROL_1
  epdData(0x00);
  epdData(0x80);
}

bool textPixel(const char *text, int16_t originX, int16_t originY, uint8_t scale, uint16_t x, uint16_t y) {
  if (x < originX || y < originY) {
    return false;
  }

  uint16_t localX = x - originX;
  uint16_t localY = y - originY;
  if (localY >= 7 * scale) {
    return false;
  }

  uint8_t charIndex = localX / (6 * scale);
  uint8_t charX = (localX % (6 * scale)) / scale;
  uint8_t charY = localY / scale;

  if (charX >= 5) {
    return false;
  }

  char c = pgm_read_byte(text + charIndex);
  if (c == '\0') {
    return false;
  }

  const uint8_t *glyph = glyphFor(c);
  uint8_t column = pgm_read_byte(glyph + charX);
  return (column & (1 << charY)) != 0;
}

bool imagePixelIsBlack(uint16_t x, uint16_t y) {
  if (x == 0 || y == 0 || x == EPD_WIDTH - 1 || y == EPD_HEIGHT - 1) {
    return true;
  }

  if ((x > 4 && x < EPD_WIDTH - 5 && (y == 18 || y == 68)) ||
      (y > 4 && y < EPD_HEIGHT - 5 && (x == 12 || x == EPD_WIDTH - 13))) {
    return true;
  }

  if (textPixel(LINE1, 28, 10, 1, x, y)) return true;
  if (textPixel(LINE2, 34, 34, 2, x, y)) return true;
  if (textPixel(LINE3, 44, 72, 1, x, y)) return true;

  // Petit motif de verification dans les coins.
  if (x > 154 && x < 176 && y > 28 && y < 52 && ((x + y) % 6 < 3)) {
    return true;
  }

  return false;
}

void epdWriteGeneratedImage() {
  epdSetRamPointer(0, EPD_HEIGHT - 1);

  // RAM "old image" tout blanc, comme dans les samples Good Display SSD1680.
  epdCommand(0x26); // WRITE_RAM old/previous image

  digitalWrite(PIN_EPD_DC, HIGH);
  digitalWrite(PIN_EPD_CS, LOW);

  for (uint16_t i = 0; i < (uint16_t)EPD_WIDTH_BYTES * EPD_HEIGHT; i++) {
    SPI.transfer(0xFF);
  }

  digitalWrite(PIN_EPD_CS, HIGH);

  epdSetRamPointer(0, EPD_HEIGHT - 1);
  epdCommand(0x24); // WRITE_RAM black/white

  digitalWrite(PIN_EPD_DC, HIGH);
  digitalWrite(PIN_EPD_CS, LOW);

  for (uint16_t y = 0; y < EPD_HEIGHT; y++) {
    for (uint8_t xb = 0; xb < EPD_WIDTH_BYTES; xb++) {
      uint8_t data = 0xFF; // 1 = blanc, 0 = noir
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

void epdRefresh() {
  epdCommand(0x22); // DISPLAY_UPDATE_CONTROL_2
  epdData(0xF7);
  epdCommand(0x20); // MASTER_ACTIVATION
  epdWaitBusy();
}

void epdSleep() {
  epdCommand(0x10); // DEEP_SLEEP_MODE
  epdData(0x01);
  delay(100);
}

void setup() {
  pinMode(PIN_EPD_BUSY, INPUT);
  pinMode(PIN_EPD_RST, OUTPUT);
  pinMode(PIN_EPD_DC, OUTPUT);
  pinMode(PIN_EPD_CS, OUTPUT);
  digitalWrite(PIN_EPD_CS, HIGH);
  digitalWrite(PIN_EPD_RST, HIGH);

  Serial.begin(9600);
  SPI.begin();
  SPI.beginTransaction(EPD_SPI_SETTINGS);

  Serial.println(F("Demo Good Display 0.97 ePaper / DESPI-C02 / Arduino Uno"));
  Serial.println(F("Si rien ne s'affiche, verifier niveau 3.3V, BUSY, et inversion du FPC."));

  epdInit();
  epdWriteGeneratedImage();
  epdRefresh();
  epdSleep();

  Serial.println(F("Affichage envoye, ePaper en deep sleep."));
}

void loop() {
  // Rien a faire: l'ePaper garde l'image sans alimentation active.
}
