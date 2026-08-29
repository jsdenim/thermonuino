/*
  Thermonuino - module sonde

  Base de production :
    - affichage eInk GoodDisplay GDEM0097T61 ;
    - page HOME de l'interface sonde ;
    - eInk remis en deep sleep apres mise a jour.

  ATmega328P 3.3 V / 8 MHz.
*/

#include <SPI.h>
#include <Wire.h>

#include "graphics/ThermioEink097.h"

constexpr uint8_t PIN_LED = 5;       // PCINT21 / PD5 / D5
constexpr uint8_t PIN_RF_CSN = 10;   // PCINT2 / PB2 / D10
constexpr uint8_t PIN_EPD_BUSY = A2; // PCINT10 / PC2 / A2
constexpr uint8_t PIN_EPD_RST = 4;   // PCINT20 / PD4 / D4
constexpr uint8_t PIN_EPD_DC = 3;    // PCINT19 / PD3 / D3
constexpr uint8_t PIN_EPD_CS = 6;    // PCINT22 / PD6 / D6

constexpr uint8_t AHT_ADDR = 0x38;
constexpr uint32_t SENSOR_REFRESH_MS = 30000;
constexpr uint32_t UI_CLOCK_REFRESH_MS = 300000;

const ThermioEink097::Pins einkPins = {
  PIN_EPD_CS,
  PIN_EPD_DC,
  PIN_EPD_RST,
  PIN_EPD_BUSY,
  PIN_RF_CSN
};

ThermioEink097 eink(einkPins);

bool lastDisplayOk = false;
uint32_t nextClockRefreshAt = 0;

enum UiPage : uint8_t {
  UI_PAGE_HOME,
  UI_PAGE_MENU
};

struct UiState {
  UiPage page;
  int16_t currentTempDeciC;
  int16_t setpointDeciC;
  int16_t outsideTempDeciC;
  uint16_t bootMinutes;
  bool currentTempKnown;
  bool outsideTempKnown;
  bool displayOk;
};

UiState ui = {
  UI_PAGE_HOME,
  0,
  190,
  85,
  0,
  false,
  true,
  false
};

int16_t roundToHalfDegree(int16_t tempDeciC) {
  if (tempDeciC >= 0) {
    return ((tempDeciC + 2) / 5) * 5;
  }
  return ((tempDeciC - 2) / 5) * 5;
}

class SondeDataService {
public:
  void begin() {
    nextSensorRefreshAt_ = 0;
  }

  bool update(uint32_t now, bool force = false) {
    if (!force && (int32_t)(now - nextSensorRefreshAt_) < 0) {
      return false;
    }

    nextSensorRefreshAt_ = now + SENSOR_REFRESH_MS;
    int16_t newTemp = currentTempDeciC_;
    const bool newValid = readAhtTemperatureDeciC(newTemp);
    if (newValid) {
      newTemp = roundToHalfDegree(newTemp);
    }
    const bool changed = newValid != currentTempKnown_ ||
        (newValid && newTemp != currentTempDeciC_);

    currentTempKnown_ = newValid;
    if (newValid) {
      currentTempDeciC_ = newTemp;
    }
    return changed;
  }

  bool currentTempKnown() const {
    return currentTempKnown_;
  }

  int16_t currentTempDeciC() const {
    return currentTempDeciC_;
  }

private:
  uint32_t nextSensorRefreshAt_ = 0;
  int16_t currentTempDeciC_ = 0;
  bool currentTempKnown_ = false;

  bool readAhtStatus(uint8_t &status) {
    Wire.requestFrom(AHT_ADDR, (uint8_t)1);
    if (Wire.available() != 1) {
      return false;
    }

    status = Wire.read();
    return true;
  }

  bool initAht() {
    Wire.beginTransmission(AHT_ADDR);
    Wire.write(0xBE);
    Wire.write(0x08);
    Wire.write(0x00);
    if (Wire.endTransmission() != 0) {
      return false;
    }

    delay(10);
    uint8_t status = 0;
    return readAhtStatus(status);
  }

  bool readAhtTemperatureDeciC(int16_t &temperatureDeciC) {
    uint8_t status = 0;
    if (!readAhtStatus(status)) {
      return false;
    }

    if ((status & 0x08) == 0 && !initAht()) {
      return false;
    }

    Wire.beginTransmission(AHT_ADDR);
    Wire.write(0xAC);
    Wire.write(0x33);
    Wire.write(0x00);
    if (Wire.endTransmission() != 0) {
      return false;
    }

    delay(80);
    uint8_t data[6] = {0};
    Wire.requestFrom(AHT_ADDR, (uint8_t)6);
    for (uint8_t i = 0; i < sizeof(data); i++) {
      if (!Wire.available()) {
        return false;
      }
      data[i] = Wire.read();
    }

    if ((data[0] & 0x80) != 0) {
      return false;
    }

    const uint32_t rawTemp =
        (((uint32_t)data[3] & 0x0F) << 16) |
        ((uint32_t)data[4] << 8) |
        data[5];
    temperatureDeciC = (int16_t)((rawTemp * 2000UL + 524288UL) / 1048576UL) - 500;
    return true;
  }
};

SondeDataService dataService;

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
    case ',': {
      const uint8_t glyph[5] = {0x00, 0x60, 0x60, 0x00, 0x00};
      return glyph[x];
    }
    case '*': {
      const uint8_t glyph[5] = {0x06, 0x09, 0x09, 0x06, 0x00};
      return glyph[x];
    }
    case '-': {
      const uint8_t glyph[5] = {0x08, 0x08, 0x08, 0x08, 0x08};
      return glyph[x];
    }
    case ':': {
      const uint8_t glyph[5] = {0x00, 0x36, 0x36, 0x00, 0x00};
      return glyph[x];
    }
    case 'B': {
      const uint8_t glyph[5] = {0x7F, 0x49, 0x49, 0x49, 0x36};
      return glyph[x];
    }
    case 'C': {
      const uint8_t glyph[5] = {0x3E, 0x41, 0x41, 0x41, 0x22};
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
    case 'W': {
      const uint8_t glyph[5] = {0x3F, 0x40, 0x38, 0x40, 0x3F};
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
  uint8_t textLen = 0;
  while (text[textLen] != '\0') {
    textLen++;
  }
  if (charIndex >= textLen) {
    return false;
  }

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

bool textPixelTopToRight(const char *text,
                         uint8_t rightX,
                         uint8_t y0,
                         uint16_t x,
                         uint16_t y,
                         uint8_t scale = 1) {
  const uint8_t glyphWidth = 5;
  const uint8_t glyphHeight = 7;
  const uint8_t charPitch = 6 * scale;
  const uint8_t rotatedWidth = glyphHeight * scale;

  if (x > rightX || rightX - x >= rotatedWidth || y < y0) {
    return false;
  }

  const uint16_t relY = y - y0;
  const uint8_t charIndex = relY / charPitch;
  uint8_t textLen = 0;
  while (text[textLen] != '\0') {
    textLen++;
  }
  if (charIndex >= textLen) {
    return false;
  }

  const uint8_t relCharY = relY % charPitch;
  const uint8_t glyphCol = relCharY / scale;
  if (glyphCol >= glyphWidth) {
    return false;
  }

  const uint8_t glyphRow = (rightX - x) / scale;
  if (glyphRow >= glyphHeight) {
    return false;
  }

  const char c = text[charIndex];
  if (c == '\0') {
    return false;
  }

  return (glyphColumn(c, glyphCol) & (1 << glyphRow)) != 0;
}

uint8_t appendUint16(char *buffer, uint8_t pos, uint16_t value) {
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

uint8_t appendTwoDigits(char *buffer, uint8_t pos, uint8_t value) {
  buffer[pos++] = '0' + (value / 10);
  buffer[pos++] = '0' + (value % 10);
  buffer[pos] = '\0';
  return pos;
}

void buildBootTimeText(uint16_t minutes, char *buffer, uint8_t bufferLen) {
  if (bufferLen < 6) {
    if (bufferLen > 0) {
      buffer[0] = '\0';
    }
    return;
  }

  const uint8_t hours = (minutes / 60) % 24;
  const uint8_t minute = minutes % 60;
  uint8_t pos = appendTwoDigits(buffer, 0, hours);
  buffer[pos++] = ':';
  pos = appendTwoDigits(buffer, pos, minute);
  buffer[pos] = '\0';
}

bool tempPixel(int16_t tempDeciC,
               bool known,
               uint8_t x0,
               uint8_t y0,
               uint16_t x,
               uint16_t y,
               uint8_t bigScale,
               uint8_t smallScale) {
  char integerText[5];
  char decimalText[2];
  tempDeciC = roundToHalfDegree(tempDeciC);
  uint8_t integerX = x0;
  const uint8_t bigPitch = 6 * bigScale;
  const uint8_t commaX = x0 + 11 * bigScale;
  const uint8_t commaY = y0 + 7 * (bigScale - smallScale) + smallScale + 1;
  const uint8_t decimalX = commaX + 6 * smallScale;
  const uint8_t decimalY = y0 + 7 * (bigScale - smallScale) - 1;
  const uint8_t degreeX = decimalX + 9 * smallScale;
  const uint8_t degreeY = y0;

  if (known) {
    const int16_t tempAbs = tempDeciC < 0 ? -tempDeciC : tempDeciC;
    uint8_t integerPos = 0;
    const uint16_t wholeDegrees = tempAbs / 10;
    if (tempDeciC < 0) {
      integerText[integerPos++] = '-';
    } else if (wholeDegrees < 10) {
      integerX += bigPitch;
    }
    appendUint16(integerText, integerPos, wholeDegrees);
    decimalText[0] = '0' + (tempAbs % 10);
    decimalText[1] = '\0';
  } else {
    integerText[0] = '-';
    integerText[1] = '-';
    integerText[2] = '\0';
    decimalText[0] = '-';
    decimalText[1] = '\0';
  }

  return textPixel(integerText, integerX, y0, x, y, bigScale) ||
      textPixel(",", commaX, commaY, x, y, smallScale) ||
      textPixel(decimalText, decimalX, decimalY, x, y, smallScale) ||
      textPixel("*", degreeX, degreeY, x, y, 1);
}

bool screenPixel(uint16_t x, uint16_t y, void *context) {
  UiState *state = (UiState *)context;

  // Cadre 1 px pour visualiser les limites exactes de l'ecran.
  if (x == 0 || y == 0 ||
      x == ThermioEink097::FrontWidth - 1 ||
      y == ThermioEink097::FrontHeight - 1) {
    return true;
  }

  if (state->page != UI_PAGE_HOME) {
    return textPixel("MENU", 80, 38, x, y, 2);
  }

  char timeText[6];
  buildBootTimeText(state->bootMinutes, timeText, sizeof(timeText));

  if (tempPixel(state->currentTempDeciC,
                state->currentTempKnown,
                2,
                4,
                x,
                y,
                7,
                3) ||
      tempPixel(state->setpointDeciC,
                true,
                2,
                62,
                x,
                y,
                3,
                2) ||
      tempPixel(state->outsideTempDeciC,
                state->outsideTempKnown,
                104,
                62,
                x,
                y,
                3,
                2) ||
      textPixelTopToRight(timeText, 181, 1, x, y, 2)) {
    return true;
  }

  if (y == 58 && x >= 1 && x < ThermioEink097::FrontWidth - 1) {
    return true;
  }

  return false;
}

void syncUiFromDataService() {
  ui.currentTempKnown = dataService.currentTempKnown();
  if (ui.currentTempKnown) {
    ui.currentTempDeciC = dataService.currentTempDeciC();
  }
  ui.bootMinutes = millis() / 60000UL;
}

void updateDisplay() {
  ledOn();
  isolateRfSpi();
  syncUiFromDataService();

  const bool initOk = eink.init();
  ui.displayOk = initOk && lastDisplayOk;
  const bool refreshOk = eink.writeFrontImage(screenPixel, &ui);

  lastDisplayOk = initOk && refreshOk;
  ui.displayOk = lastDisplayOk;
  if (lastDisplayOk) {
    ledOff();
  }
  eink.sleep();
}

void setup() {
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_RF_CSN, OUTPUT);
  ledOff();
  isolateRfSpi();

  Wire.begin();
  SPI.begin();
  eink.begin();

  dataService.begin();
  dataService.update(millis(), true);
  updateDisplay();
  nextClockRefreshAt = millis() + UI_CLOCK_REFRESH_MS;
}

void loop() {
  const uint32_t now = millis();
  bool displayNeedsRefresh = dataService.update(now);
  if ((int32_t)(now - nextClockRefreshAt) >= 0) {
    nextClockRefreshAt = now + UI_CLOCK_REFRESH_MS;
    displayNeedsRefresh = true;
  }

  if (displayNeedsRefresh) {
    updateDisplay();
  }

  if (lastDisplayOk) {
    ledOff();
  }
  isolateRfSpi();
}
