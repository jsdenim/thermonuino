#include "SondeUi.h"

#include "graphics/ThermioEink097.h"
#include "graphics/ThermioFont5x7.h"
#include "graphics/ThermioIcons.h"

UiState ui = {
  UI_PAGE_HOME,
  0,
  190,
  85,
  0,
  false,
  true,
  false,
  true,
  true,
  true,
  true
};

namespace {

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

int16_t roundToHalfDegree(int16_t tempDeciC) {
  if (tempDeciC >= 0) {
    return ((tempDeciC + 2) / 5) * 5;
  }
  return ((tempDeciC - 2) / 5) * 5;
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

bool compactTimePixel(const char *text,
                      uint8_t rightX,
                      uint8_t y0,
                      uint16_t x,
                      uint16_t y,
                      uint8_t scale = 3) {
  const uint8_t compactOffsets[5] = {
    0,
    (uint8_t)(6 * scale),
    (uint8_t)(11 * scale + 3),
    (uint8_t)(15 * scale + 3),
    (uint8_t)(21 * scale + 3)
  };
  const uint8_t compactWidth = 26 * scale + 3;
  const uint8_t x0 = rightX + 1 - compactWidth;

  for (uint8_t i = 0; i < 5 && text[i] != '\0'; i++) {
    const char character[2] = {text[i], '\0'};
    if (ThermioFont5x7::textPixel(character, x0 + compactOffsets[i], y0, x, y, scale)) {
      return true;
    }
  }
  return false;
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

  return ThermioFont5x7::textPixel(integerText, integerX, y0, x, y, bigScale) ||
      ThermioFont5x7::textPixel(",", commaX, commaY, x, y, smallScale) ||
      ThermioFont5x7::textPixel(decimalText, decimalX, decimalY, x, y, smallScale) ||
      ThermioFont5x7::textPixel("*", degreeX, degreeY, x, y, 1);
}

bool thickDiagonalPixel(uint16_t x1,
                        uint16_t y1,
                        uint16_t x2,
                        uint16_t y2,
                        uint16_t x,
                        uint16_t y) {
  const int32_t dx = (int32_t)x2 - x1;
  const int32_t dy = (int32_t)y2 - y1;
  const int32_t distance = ((int32_t)x - x1) * dy - ((int32_t)y - y1) * dx;
  return distance >= -220 && distance <= 220;
}

bool batteryDeadPixel(uint16_t x, uint16_t y) {
  if (thickDiagonalPixel(1, 1, ThermioEink097::FrontWidth - 2,
                         ThermioEink097::FrontHeight - 2, x, y) ||
      thickDiagonalPixel(ThermioEink097::FrontWidth - 2, 1, 1,
                         ThermioEink097::FrontHeight - 2, x, y)) {
    return true;
  }

  return ThermioFont5x7::textPixel("REMPLACER", 11, 20, x, y, 3) ||
      ThermioFont5x7::textPixel("PILES", 47, 48, x, y, 3);
}

bool stopPixel(uint16_t x, uint16_t y) {
  return ThermioFont5x7::textPixel("STOP", 56, 34, x, y, 3);
}

bool vacationPixel(uint16_t x, uint16_t y) {
  return ThermioFont5x7::textPixel("VACANCES", 29, 34, x, y, 3);
}

bool systemTrayPixel(const UiState *state, uint16_t x, uint16_t y) {
  return (state->batteryLow &&
          ThermioIcons::trayIconPixelAt(ThermioIcons::BatteryLow16, 116, 36, x, y)) ||
      (state->consoleOk &&
       ThermioIcons::trayIconPixelAt(ThermioIcons::ConsoleOk16, 133, 36, x, y)) ||
      (state->motionDetected &&
       ThermioIcons::trayIconPixelAt(ThermioIcons::Motion16, 150, 36, x, y)) ||
      (state->heatActive &&
       ThermioIcons::trayIconPixelAt(ThermioIcons::Heat16, 167, 36, x, y));
}

}

bool sondeScreenPixel(uint16_t x, uint16_t y, void *context) {
  UiState *state = (UiState *)context;

  if (x == 0 || y == 0 ||
      x == ThermioEink097::FrontWidth - 1 ||
      y == ThermioEink097::FrontHeight - 1) {
    return true;
  }

  if (state->page == UI_PAGE_BATTERY_DEAD) {
    return batteryDeadPixel(x, y);
  }
  if (state->page == UI_PAGE_STOP) {
    return stopPixel(x, y);
  }
  if (state->page == UI_PAGE_VACATION) {
    return vacationPixel(x, y);
  }

  if (state->page != UI_PAGE_HOME) {
    return ThermioFont5x7::textPixel("MENU", 80, 38, x, y, 2);
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
                26,
                62,
                x,
                y,
                3,
                2) ||
      tempPixel(state->outsideTempDeciC,
                state->outsideTempKnown,
                86,
                62,
                x,
                y,
                3,
                2) ||
      compactTimePixel(timeText, 181, 1, x, y, 3) ||
      systemTrayPixel(state, x, y) ||
      ThermioIcons::setpointPixelAt(1, 62, x, y) ||
      ThermioIcons::outsidePixelAt(159, 62, x, y)) {
    return true;
  }

  if (y == 58 && x >= 1 && x < ThermioEink097::FrontWidth - 1) {
    return true;
  }

  return false;
}
