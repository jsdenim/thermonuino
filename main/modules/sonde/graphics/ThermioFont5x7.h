#ifndef THERMIO_FONT_5X7_H
#define THERMIO_FONT_5X7_H

#include <Arduino.h>

namespace ThermioFont5x7 {

inline uint8_t glyphColumn(char c, uint8_t x) {
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
    case 'A': {
      const uint8_t glyph[5] = {0x7E, 0x09, 0x09, 0x09, 0x7E};
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
    case 'V': {
      const uint8_t glyph[5] = {0x1F, 0x20, 0x40, 0x20, 0x1F};
      return glyph[x];
    }
    case 'W': {
      const uint8_t glyph[5] = {0x3F, 0x40, 0x38, 0x40, 0x3F};
      return glyph[x];
    }
    case 'Z': {
      const uint8_t glyph[5] = {0x61, 0x51, 0x49, 0x45, 0x43};
      return glyph[x];
    }
    case '.': {
      const uint8_t glyph[5] = {0x00, 0x60, 0x60, 0x00, 0x00};
      return glyph[x];
    }
    default:
      return 0x00;
  }
}

inline bool textPixel(const char *text,
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

}

#endif
