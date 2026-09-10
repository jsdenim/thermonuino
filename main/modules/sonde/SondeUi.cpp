#include "SondeUi.h"

#include "graphics/ThermioEink097.h"
#include "graphics/ThermioFont5x7.h"

UiState ui = {
  UI_PAGE_HOME,
  UI_MENU_OUTSIDE,
  UI_SUB_NONE,
  0,
  0,
  190,
  85,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  false,
  true,
  false,
  false,
  false,
  false,
  false,
  false,
  true,
  false,
  false,
  false,
  false,
  false
};

namespace {

const UiMenuSubPage BatterySubPages[] = {
  UI_SUB_BATTERY_VOLTAGE,
  UI_SUB_BATTERY_THRESHOLDS,
  UI_SUB_BATTERY_STATE
};

const UiMenuSubPage ConsoleSubPages[] = {
  UI_SUB_CONSOLE_ZONE,
  UI_SUB_CONSOLE_PAIRING,
  UI_SUB_CONSOLE_RF_DEBUG,
  UI_SUB_CONSOLE_IDS,
  UI_SUB_CONSOLE_LAST_RESPONSE
};

const UiMenuSubPage ThermometerSubPages[] = {
  UI_SUB_THERMO_RAW,
  UI_SUB_THERMO_OFFSET,
  UI_SUB_THERMO_CORRECTED
};

const UiMenuSubPage LearningSubPages[] = {
  UI_SUB_LEARNING_SETPOINT,
  UI_SUB_LEARNING_SOURCE,
  UI_SUB_LEARNING_RESET_ZONE,
  UI_SUB_LEARNING_RESET_GLOBAL
};

bool rectPixel(int16_t x0,
               int16_t y0,
               int16_t w,
               int16_t h,
               uint16_t x,
               uint16_t y);
bool segmentedTempPixel(int16_t tempDeciC,
                        bool known,
                        int16_t x0,
                        int16_t y0,
                        uint16_t x,
                        uint16_t y);

bool inRect(uint16_t x,
            uint16_t y,
            uint8_t x0,
            uint8_t y0,
            uint8_t w,
            uint8_t h) {
  return x >= x0 && y >= y0 && x < (uint16_t)x0 + w && y < (uint16_t)y0 + h;
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

uint8_t appendSignedDeciText(char *buffer, int16_t deciC) {
  uint8_t pos = 0;
  if (deciC < 0) {
    buffer[pos++] = 'M';
    buffer[pos++] = 'O';
    buffer[pos++] = 'I';
    buffer[pos++] = 'N';
    buffer[pos++] = 'S';
    buffer[pos++] = ' ';
    deciC = -deciC;
  }
  return appendUint16(buffer, pos, (uint16_t)deciC);
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
  if (!inRect(x, y, x0, y0, compactWidth, 7 * scale)) {
    return false;
  }

  for (uint8_t i = 0; i < 5 && text[i] != '\0'; i++) {
    const char character[2] = {text[i], '\0'};
    if (ThermioFont5x7::textPixel(character, x0 + compactOffsets[i], y0, x, y, scale)) {
      return true;
    }
  }
  return false;
}

bool textLinePixel(const char *text,
                   uint8_t x0,
                   uint8_t y0,
                   uint16_t x,
                   uint16_t y,
                   uint8_t scale = 2) {
  return ThermioFont5x7::textPixel(text, x0, y0, x, y, scale);
}

bool textLinePixelP(PGM_P text,
                    uint8_t x0,
                    uint8_t y0,
                    uint16_t x,
                    uint16_t y,
                    uint8_t scale = 2) {
  return ThermioFont5x7::textPixel_P(text, x0, y0, x, y, scale);
}

bool plusMarkerPixel(uint16_t x, uint16_t y) {
  return rectPixel(168, 12, 13, 3, x, y) ||
      rectPixel(173, 7, 3, 13, x, y);
}

uint8_t batteryPercent(uint16_t batteryMv) {
  const uint16_t noBatteryMv = 50;
  const uint16_t criticalMv = 2200;
  const uint16_t fullMv = 3000;
  if (batteryMv <= noBatteryMv || batteryMv <= criticalMv) {
    return 0;
  }
  if (batteryMv >= fullMv) {
    return 100;
  }
  return (uint8_t)(((uint32_t)(batteryMv - criticalMv) * 100UL) / (fullMv - criticalMv));
}

bool numericValueLinePixel(uint16_t value,
                           const char *suffix,
                           uint8_t x0,
                           uint8_t y0,
                           uint16_t x,
                           uint16_t y,
                           uint8_t scale = 2) {
  char text[16];
  uint8_t pos = appendUint16(text, 0, value);
  if (suffix != nullptr) {
    while (*suffix != '\0' && pos < sizeof(text) - 1) {
      text[pos++] = *suffix++;
    }
    text[pos] = '\0';
  }
  return textLinePixel(text, x0, y0, x, y, scale);
}

bool signedDeciLinePixel(int16_t value,
                         uint8_t x0,
                         uint8_t y0,
                         uint16_t x,
                         uint16_t y,
                         uint8_t scale = 2) {
  char text[18];
  appendSignedDeciText(text, value);
  return textLinePixel(text, x0, y0, x, y, scale);
}

bool centeredStatusPixel(PGM_P title,
                         PGM_P value,
                         bool hasSubmenu,
                         uint16_t x,
                         uint16_t y) {
  if (textLinePixelP(title, 6, 7, x, y, 2)) {
    return true;
  }
  if (hasSubmenu && plusMarkerPixel(x, y)) {
    return true;
  }
  return textLinePixelP(value, 22, 43, x, y, 3);
}

bool menuMainPixel(const UiState *state, uint16_t x, uint16_t y) {
  switch (state->menuPage) {
    case UI_MENU_OUTSIDE:
      if (textLinePixelP(PSTR("EXTERIEUR"), 6, 7, x, y, 2)) {
        return true;
      }
      return state->outsideTempKnown ?
          segmentedTempPixel(state->outsideTempDeciC, true, 47, 30, x, y) :
          textLinePixelP(PSTR("INCONNUE"), 23, 43, x, y, 3);
    case UI_MENU_PRESENCE:
      return centeredStatusPixel(PSTR("PRESENCE"), state->motionDetected ? PSTR("OUI") : PSTR("NON"), false, x, y);
    case UI_MENU_BATTERY:
      if (centeredStatusPixel(PSTR("BATTERIE"), PSTR(""), true, x, y)) {
        return true;
      }
      if (state->batteryMv <= 50) {
        return textLinePixelP(PSTR("TEST"), 58, 43, x, y, 3);
      }
      return numericValueLinePixel(batteryPercent(state->batteryMv), " PCT", 36, 43, x, y, 3);
    case UI_MENU_CONSOLE:
      return centeredStatusPixel(PSTR("CONSOLE"), state->consoleOk ? PSTR("OK") : PSTR("KO"), true, x, y);
    case UI_MENU_THERMOMETER:
      if (textLinePixelP(PSTR("AHT30"), 6, 7, x, y, 2) || plusMarkerPixel(x, y)) {
        return true;
      }
      return segmentedTempPixel(state->rawTempDeciC, state->currentTempKnown, 47, 30, x, y);
    case UI_MENU_LEARNING:
    default:
      return centeredStatusPixel(PSTR("APPRENT"), PSTR("ZONE"), true, x, y);
  }
}

bool menuSubPixel(const UiState *state, uint16_t x, uint16_t y) {
  switch (state->menuSubPage) {
    case UI_SUB_BATTERY_VOLTAGE:
      return textLinePixelP(PSTR("TENSION"), 6, 7, x, y, 2) ||
          numericValueLinePixel(state->batteryMv, " MV", 32, 43, x, y, 3);
    case UI_SUB_BATTERY_THRESHOLDS:
      return textLinePixelP(PSTR("SEUILS"), 6, 7, x, y, 2) ||
          textLinePixelP(PSTR("LOW 2400"), 20, 35, x, y, 2) ||
          textLinePixelP(PSTR("STOP 2200"), 20, 57, x, y, 2);
    case UI_SUB_BATTERY_STATE:
      return centeredStatusPixel(PSTR("ETAT PILE"),
                                 state->batteryCritical ? PSTR("STOP") : state->batteryLow ? PSTR("FAIBLE") : PSTR("OK"),
                                 false,
                                 x,
                                 y);
    case UI_SUB_CONSOLE_ZONE:
      return textLinePixelP(PSTR("ZONE"), 6, 7, x, y, 2) ||
          (state->assignedZone == 0 ?
              textLinePixelP(PSTR("INCONNUE"), 23, 43, x, y, 3) :
              numericValueLinePixel(state->assignedZone, nullptr, 82, 43, x, y, 3));
    case UI_SUB_CONSOLE_PAIRING:
      return centeredStatusPixel(PSTR("ASSOC"), PSTR("BOUTON"), false, x, y);
    case UI_SUB_CONSOLE_RF_DEBUG:
      return textLinePixelP(PSTR("DEBUG RF"), 6, 7, x, y, 2) ||
          textLinePixelP(state->rfSpiOk ? PSTR("SPI OK") : PSTR("SPI KO"), 20, 35, x, y, 2) ||
          textLinePixelP(state->consoleOk ? PSTR("ACK OK") : PSTR("ACK KO"), 20, 57, x, y, 2);
    case UI_SUB_CONSOLE_IDS:
      return textLinePixelP(PSTR("IDS RF"), 6, 7, x, y, 2) ||
          numericValueLinePixel(state->localRfId, " LOCAL", 12, 35, x, y, 2) ||
          numericValueLinePixel(state->consoleRfId, " CONS", 12, 57, x, y, 2);
    case UI_SUB_CONSOLE_LAST_RESPONSE:
      return textLinePixelP(PSTR("REPONSE"), 6, 7, x, y, 2) ||
          textLinePixelP(state->heatLastHour ? PSTR("CHAUF 1H") : PSTR("CHAUF 24H"), 16, 35, x, y, 2) ||
          textLinePixelP(state->zoneDoorOpen ? PSTR("PORTE OUV") : PSTR("PORTE OK"), 16, 57, x, y, 2);
    case UI_SUB_THERMO_RAW:
      if (textLinePixelP(PSTR("BRUT"), 6, 7, x, y, 2)) {
        return true;
      }
      return segmentedTempPixel(state->rawTempDeciC, state->currentTempKnown, 47, 30, x, y);
    case UI_SUB_THERMO_OFFSET:
      return textLinePixelP(state->menuEditing ? PSTR("OFFSET EDIT") : PSTR("OFFSET"), 6, 7, x, y, 2) ||
          signedDeciLinePixel(state->ahtOffsetDeciC, 48, 43, x, y, 3);
    case UI_SUB_THERMO_CORRECTED:
      if (textLinePixelP(PSTR("CORRIGE"), 6, 7, x, y, 2)) {
        return true;
      }
      return segmentedTempPixel(state->currentTempDeciC, state->currentTempKnown, 47, 30, x, y);
    case UI_SUB_LEARNING_SETPOINT:
      if (textLinePixelP(PSTR("CONSIGNE"), 6, 7, x, y, 2)) {
        return true;
      }
      return segmentedTempPixel(state->setpointDeciC, true, 47, 30, x, y);
    case UI_SUB_LEARNING_SOURCE:
      return centeredStatusPixel(PSTR("SOURCE"), PSTR("SECOURS"), false, x, y);
    case UI_SUB_LEARNING_RESET_ZONE:
      return centeredStatusPixel(PSTR("RESET ZONE"), PSTR("NON"), false, x, y);
    case UI_SUB_LEARNING_RESET_GLOBAL:
      return centeredStatusPixel(PSTR("RESET ALL"), PSTR("NON"), false, x, y);
    case UI_SUB_NONE:
    default:
      return menuMainPixel(state, x, y);
  }
}
bool tempPixel(int16_t tempDeciC,
               bool known,
               uint8_t x0,
               uint8_t y0,
               uint16_t x,
               uint16_t y,
               uint8_t bigScale,
               uint8_t smallScale) {
  const uint8_t bigPitch = 6 * bigScale;
  const uint8_t commaX = x0 + 11 * bigScale;
  const uint8_t commaY = y0 + 7 * (bigScale - smallScale) + smallScale + 1;
  const uint8_t decimalX = commaX + 6 * smallScale;
  const uint8_t decimalY = y0 + 7 * (bigScale - smallScale) - 1;
  const uint8_t degreeX = decimalX + 9 * smallScale;
  const uint8_t degreeY = y0;
  const uint8_t blockW = (degreeX - x0) + 5;
  const uint8_t blockH = 7 * bigScale;

  if (!inRect(x, y, x0, y0, blockW, blockH)) {
    return false;
  }

  char integerText[5];
  char decimalText[2];
  tempDeciC = roundToHalfDegree(tempDeciC);
  uint8_t integerX = x0;

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

  return ThermioFont5x7::textPixel_P(PSTR("REMPLACER"), 11, 20, x, y, 3) ||
      ThermioFont5x7::textPixel_P(PSTR("PILES"), 47, 48, x, y, 3);
}

bool stopPixel(uint16_t x, uint16_t y) {
  return ThermioFont5x7::textPixel_P(PSTR("STOP"), 56, 34, x, y, 3);
}

bool vacationPixel(uint16_t x, uint16_t y) {
  return ThermioFont5x7::textPixel_P(PSTR("VACANCES"), 29, 34, x, y, 3);
}

bool rfErrorPixel(uint16_t x, uint16_t y) {
  if (thickDiagonalPixel(1, 1, ThermioEink097::FrontWidth - 2,
                         ThermioEink097::FrontHeight - 2, x, y) ||
      thickDiagonalPixel(ThermioEink097::FrontWidth - 2, 1, 1,
                         ThermioEink097::FrontHeight - 2, x, y)) {
    return true;
  }

  return ThermioFont5x7::textPixel_P(PSTR("RF 433 MHZ"), 2, 26, x, y, 3) ||
      ThermioFont5x7::textPixel_P(PSTR("KO"), 65, 52, x, y, 3);
}

bool rectPixel(int16_t x0,
               int16_t y0,
               int16_t w,
               int16_t h,
               uint16_t x,
               uint16_t y) {
  return (int16_t)x >= x0 && (int16_t)y >= y0 &&
      (int16_t)x < x0 + w && (int16_t)y < y0 + h;
}

uint8_t sevenSegmentMask(char value) {
  switch (value) {
    case '0':
      return 0b0111111;
    case '1':
      return 0b0000110;
    case '2':
      return 0b1011011;
    case '3':
      return 0b1001111;
    case '4':
      return 0b1100110;
    case '5':
      return 0b1101101;
    case '6':
      return 0b1111101;
    case '7':
      return 0b0000111;
    case '8':
      return 0b1111111;
    case '9':
      return 0b1101111;
    case '-':
      return 0b1000000;
    default:
      return 0;
  }
}

bool sevenSegmentDigitPixel(char value,
                            int16_t x0,
                            int16_t y0,
                            uint16_t x,
                            uint16_t y) {
  const int16_t digitW = 29;
  const int16_t digitH = 48;
  const int16_t thick = 6;
  const uint8_t mask = sevenSegmentMask(value);
  if (mask == 0 || !rectPixel(x0, y0, digitW, digitH, x, y)) {
    return false;
  }

  return ((mask & 0b0000001) && rectPixel(x0 + thick, y0, digitW - 2 * thick, thick, x, y)) ||
      ((mask & 0b0000010) && rectPixel(x0 + digitW - thick, y0 + thick, thick, digitH / 2 - thick, x, y)) ||
      ((mask & 0b0000100) && rectPixel(x0 + digitW - thick, y0 + digitH / 2, thick, digitH / 2 - thick, x, y)) ||
      ((mask & 0b0001000) && rectPixel(x0 + thick, y0 + digitH - thick, digitW - 2 * thick, thick, x, y)) ||
      ((mask & 0b0010000) && rectPixel(x0, y0 + digitH / 2, thick, digitH / 2 - thick, x, y)) ||
      ((mask & 0b0100000) && rectPixel(x0, y0 + thick, thick, digitH / 2 - thick, x, y)) ||
      ((mask & 0b1000000) && rectPixel(x0 + thick, y0 + digitH / 2 - thick / 2, digitW - 2 * thick, thick, x, y));
}

bool sevenSegmentCommaPixel(int16_t x0, int16_t y0, uint16_t x, uint16_t y) {
  return rectPixel(x0, y0 + 35, 4, 4, x, y) ||
      rectPixel(x0 - 1, y0 + 39, 3, 3, x, y);
}

bool sevenSegmentDegreePixel(int16_t x0, int16_t y0, uint16_t x, uint16_t y) {
  return rectPixel(x0 + 2, y0 + 1, 5, 2, x, y) ||
      rectPixel(x0 + 2, y0 + 8, 5, 2, x, y) ||
      rectPixel(x0, y0 + 3, 2, 5, x, y) ||
      rectPixel(x0 + 7, y0 + 3, 2, 5, x, y);
}

bool segmentedTempPixel(int16_t tempDeciC,
                        bool known,
                        int16_t x0,
                        int16_t y0,
                        uint16_t x,
                        uint16_t y) {
  const int16_t digitW = 29;
  const int16_t digitH = 48;
  const int16_t gap = 4;
  const int16_t commaW = 4;
  const int16_t totalW = digitW * 3 + gap * 4 + commaW + 9;
  if (!rectPixel(x0, y0, totalW, digitH, x, y)) {
    return false;
  }

  char tens = '-';
  char units = '-';
  char decimal = '-';
  if (known) {
    tempDeciC = roundToHalfDegree(tempDeciC);
    const int16_t tempAbs = tempDeciC < 0 ? -tempDeciC : tempDeciC;
    uint8_t whole = tempAbs / 10;
    if (whole > 99) {
      whole = 99;
    }
    tens = whole >= 10 ? (char)('0' + whole / 10) : ' ';
    if (tempDeciC < 0 && whole < 10) {
      tens = '-';
    }
    units = (char)('0' + whole % 10);
    decimal = (char)('0' + tempAbs % 10);
  }

  const int16_t tensX = x0;
  const int16_t unitsX = tensX + digitW + gap;
  const int16_t commaX = unitsX + digitW + gap - 1;
  const int16_t decimalX = commaX + commaW + gap;
  const int16_t degreeX = decimalX + digitW + gap;

  return sevenSegmentDigitPixel(tens, tensX, y0, x, y) ||
      sevenSegmentDigitPixel(units, unitsX, y0, x, y) ||
      sevenSegmentCommaPixel(commaX, y0, x, y) ||
      sevenSegmentDigitPixel(decimal, decimalX, y0, x, y) ||
      sevenSegmentDegreePixel(degreeX, y0, x, y);
}

bool segmentPixel(int16_t x1,
                  int16_t y1,
                  int16_t x2,
                  int16_t y2,
                  uint16_t x,
                  uint16_t y,
                  int16_t thickness) {
  const int32_t dx = (int32_t)x2 - x1;
  const int32_t dy = (int32_t)y2 - y1;
  const int32_t px = (int32_t)x - x1;
  const int32_t py = (int32_t)y - y1;
  const int32_t dot = px * dx + py * dy;
  const int32_t len2 = dx * dx + dy * dy;
  if (dot < 0 || dot > len2) {
    return false;
  }

  const int32_t cross = px * dy - py * dx;
  return cross * cross <= (int32_t)thickness * thickness * len2;
}

bool arrowToTemperaturePixel(const UiState *state, uint16_t x, uint16_t y) {
  const int16_t current = state->currentTempKnown ? state->currentTempDeciC : state->setpointDeciC;
  int16_t diff = state->setpointDeciC - current;
  if (diff > 20) {
    diff = 20;
  }
  if (diff < -20) {
    diff = -20;
  }

  const int16_t centerX = 34;
  const int16_t centerY = 44;
  const int16_t vectorX = ((20 - (diff < 0 ? -diff : diff)) * 34) / 20;
  const int16_t vectorY = (-diff * 34) / 20;
  const int16_t tailX = centerX - vectorX / 2;
  const int16_t tailY = centerY - vectorY / 2;
  const int16_t headX = centerX + vectorX / 2;
  const int16_t headY = centerY + vectorY / 2;
  const int16_t dx = headX - tailX;
  const int16_t dy = headY - tailY;

  if (segmentPixel(tailX, tailY, headX, headY, x, y, 3)) {
    return true;
  }

  const int16_t backX = headX - (dx * 9) / 34;
  const int16_t backY = headY - (dy * 9) / 34;
  const int16_t perpX = (-dy * 7) / 34;
  const int16_t perpY = (dx * 7) / 34;
  return segmentPixel(headX, headY, backX + perpX, backY + perpY, x, y, 3) ||
      segmentPixel(headX, headY, backX - perpX, backY - perpY, x, y, 3);
}

}

uint8_t uiMenuSubCount(UiMenuPage page) {
  switch (page) {
    case UI_MENU_BATTERY:
      return sizeof(BatterySubPages) / sizeof(BatterySubPages[0]);
    case UI_MENU_CONSOLE:
      return sizeof(ConsoleSubPages) / sizeof(ConsoleSubPages[0]);
    case UI_MENU_THERMOMETER:
      return sizeof(ThermometerSubPages) / sizeof(ThermometerSubPages[0]);
    case UI_MENU_LEARNING:
      return sizeof(LearningSubPages) / sizeof(LearningSubPages[0]);
    case UI_MENU_OUTSIDE:
    case UI_MENU_PRESENCE:
    default:
      return 0;
  }
}

UiMenuSubPage uiMenuSubAt(UiMenuPage page, uint8_t index) {
  switch (page) {
    case UI_MENU_BATTERY:
      return index < uiMenuSubCount(page) ? BatterySubPages[index] : UI_SUB_NONE;
    case UI_MENU_CONSOLE:
      return index < uiMenuSubCount(page) ? ConsoleSubPages[index] : UI_SUB_NONE;
    case UI_MENU_THERMOMETER:
      return index < uiMenuSubCount(page) ? ThermometerSubPages[index] : UI_SUB_NONE;
    case UI_MENU_LEARNING:
      return index < uiMenuSubCount(page) ? LearningSubPages[index] : UI_SUB_NONE;
    case UI_MENU_OUTSIDE:
    case UI_MENU_PRESENCE:
    default:
      return UI_SUB_NONE;
  }
}

int8_t uiMenuSubIndex(UiMenuPage page, UiMenuSubPage subPage) {
  const uint8_t count = uiMenuSubCount(page);
  for (uint8_t i = 0; i < count; i++) {
    if (uiMenuSubAt(page, i) == subPage) {
      return i;
    }
  }
  return -1;
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
  if (state->page == UI_PAGE_RF_ERROR) {
    return rfErrorPixel(x, y);
  }

  if (state->page == UI_PAGE_MENU) {
    return state->menuInSubmenu ? menuSubPixel(state, x, y) : menuMainPixel(state, x, y);
  }

  if (state->page != UI_PAGE_HOME) {
    return ThermioFont5x7::textPixel_P(PSTR("MENU"), 80, 38, x, y, 2);
  }

  if (state->setpointEditing) {
    return segmentedTempPixel(state->setpointDeciC, true, 66, 20, x, y);
  }

  return (state->heatActive && arrowToTemperaturePixel(state, x, y)) ||
      segmentedTempPixel(state->currentTempDeciC, state->currentTempKnown, 66, 20, x, y);
}
