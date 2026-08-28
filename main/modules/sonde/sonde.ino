/*
  Thermonuino - module sonde

  Base de production :
    - affichage eInk GoodDisplay GDEM0097T61 ;
    - eInk remis en deep sleep apres chaque mise a jour ;
    - ATmega328P reveille par watchdog 8 s ;
    - affichage temporaire du nombre de secondes depuis le boot.

  ATmega328P 3.3 V / 8 MHz.
*/

#include <SPI.h>

#include <ThermioSlavePower.h>

#include "graphics/ThermioEink097.h"

constexpr uint8_t PIN_LED = 5;       // PCINT21 / PD5 / D5
constexpr uint8_t PIN_RF_CSN = 10;   // PCINT2 / PB2 / D10
constexpr uint8_t PIN_EPD_BUSY = A2; // PCINT10 / PC2 / A2
constexpr uint8_t PIN_EPD_RST = 4;   // PCINT20 / PD4 / D4
constexpr uint8_t PIN_EPD_DC = 3;    // PCINT19 / PD3 / D3
constexpr uint8_t PIN_EPD_CS = 6;    // PCINT22 / PD6 / D6

constexpr uint8_t WATCHDOG_SECONDS = 8;

const ThermioEink097::Pins einkPins = {
  PIN_EPD_CS,
  PIN_EPD_DC,
  PIN_EPD_RST,
  PIN_EPD_BUSY,
  PIN_RF_CSN
};

ThermioEink097 eink(einkPins);

uint32_t bootSeconds = 0;
bool lastDisplayOk = false;

struct ScreenContext {
  uint32_t seconds;
  bool displayOk;
};

void ledOn() {
  digitalWrite(PIN_LED, HIGH);
}

void ledOff() {
  digitalWrite(PIN_LED, LOW);
}

void isolateRfSpi() {
  digitalWrite(PIN_RF_CSN, HIGH);
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
    case '2': {
      const uint8_t glyph[5] = {0x62, 0x51, 0x49, 0x49, 0x46};
      return glyph[x];
    }
    case '3': {
      const uint8_t glyph[5] = {0x22, 0x41, 0x49, 0x49, 0x36};
      return glyph[x];
    }
    case '4': {
      const uint8_t glyph[5] = {0x18, 0x14, 0x12, 0x7F, 0x10};
      return glyph[x];
    }
    case '5': {
      const uint8_t glyph[5] = {0x27, 0x45, 0x45, 0x45, 0x39};
      return glyph[x];
    }
    case '6': {
      const uint8_t glyph[5] = {0x3C, 0x4A, 0x49, 0x49, 0x30};
      return glyph[x];
    }
    case '7': {
      const uint8_t glyph[5] = {0x01, 0x71, 0x09, 0x05, 0x03};
      return glyph[x];
    }
    case '8': {
      const uint8_t glyph[5] = {0x36, 0x49, 0x49, 0x49, 0x36};
      return glyph[x];
    }
    case '9': {
      const uint8_t glyph[5] = {0x06, 0x49, 0x49, 0x29, 0x1E};
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
    case 'I': {
      const uint8_t glyph[5] = {0x00, 0x41, 0x7F, 0x41, 0x00};
      return glyph[x];
    }
    case 'K': {
      const uint8_t glyph[5] = {0x7F, 0x08, 0x14, 0x22, 0x41};
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
    case 'S': {
      const uint8_t glyph[5] = {0x46, 0x49, 0x49, 0x49, 0x31};
      return glyph[x];
    }
    case 'T': {
      const uint8_t glyph[5] = {0x01, 0x01, 0x7F, 0x01, 0x01};
      return glyph[x];
    }
    default:
      return 0x00;
  }
}

bool textPixel(const char *text,
               uint8_t x0,
               uint8_t y0,
               uint16_t x,
               uint16_t y,
               uint8_t scale = 1) {
  if (x < x0 || y < y0) {
    return false;
  }

  const uint16_t relX = x - x0;
  const uint16_t relY = y - y0;
  if (relY >= (uint16_t)7 * scale) {
    return false;
  }

  const uint8_t charPitch = 6 * scale;
  const uint8_t charIndex = relX / charPitch;
  const uint8_t col = (relX % charPitch) / scale;
  if (col >= 5) {
    return false;
  }

  const char c = text[charIndex];
  if (c == '\0') {
    return false;
  }

  const uint8_t row = relY / scale;
  return (glyphColumn(c, col) & (1 << row)) != 0;
}

uint8_t appendUint32(char *buffer, uint8_t pos, uint32_t value) {
  char digits[10];
  uint8_t count = 0;
  do {
    digits[count++] = '0' + (value % 10);
    value /= 10;
  } while (value > 0 && count < sizeof(digits));

  while (count > 0) {
    buffer[pos++] = digits[--count];
  }
  buffer[pos] = '\0';
  return pos;
}

void buildSecondsText(uint32_t seconds, char *buffer, uint8_t bufferLen) {
  if (bufferLen == 0) {
    return;
  }

  uint8_t pos = 0;
  const char prefix[] = "BOOT ";
  for (uint8_t i = 0; prefix[i] != '\0' && pos + 1 < bufferLen; i++) {
    buffer[pos++] = prefix[i];
  }

  pos = appendUint32(buffer, pos, seconds);
  if (pos + 2 < bufferLen) {
    buffer[pos++] = 'S';
    buffer[pos] = '\0';
  }
}

bool screenPixel(uint16_t x, uint16_t y, void *context) {
  ScreenContext *screen = (ScreenContext *)context;

  if (x == 0 || y == 0 ||
      x == ThermioEink097::FrontWidth - 1 ||
      y == ThermioEink097::FrontHeight - 1) {
    return true;
  }

  char secondsText[18];
  buildSecondsText(screen->seconds, secondsText, sizeof(secondsText));

  if (textPixel("THERMIO", 23, 10, x, y) ||
      textPixel("SONDE", 29, 27, x, y) ||
      textPixel(secondsText, 8, 58, x, y, 2) ||
      textPixel(screen->displayOk ? "EINK OK" : "EINK BOOT", 20, 104, x, y) ||
      textPixel("WDT 8S", 23, 120, x, y)) {
    return true;
  }

  if (y > 150 && y < 174) {
    return ((x / 4) + (y / 4)) & 1;
  }

  return false;
}

void updateDisplay() {
  ledOn();
  isolateRfSpi();

  const bool initOk = eink.init();
  ScreenContext screen = {bootSeconds, initOk && lastDisplayOk};
  const bool refreshOk = eink.writeFrontImage(screenPixel, &screen);
  eink.sleep();

  lastDisplayOk = initOk && refreshOk;
  if (lastDisplayOk) {
    ledOff();
  }
}

void setup() {
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_RF_CSN, OUTPUT);
  ledOff();
  isolateRfSpi();

  SPI.begin();
  eink.begin();

  updateDisplay();
  ThermioSlavePower::setupWatchdog8s();
}

void loop() {
  const uint16_t watchdogTicks = ThermioSlavePower::consumeWatchdogTicks();
  if (watchdogTicks > 0) {
    bootSeconds += (uint32_t)watchdogTicks * WATCHDOG_SECONDS;
    updateDisplay();
  }

  if (lastDisplayOk) {
    ledOff();
  }
  isolateRfSpi();
  ThermioSlavePower::sleepPowerDown();
}
