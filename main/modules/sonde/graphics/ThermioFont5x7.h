#ifndef THERMIO_FONT_5X7_H
#define THERMIO_FONT_5X7_H

#include <Arduino.h>
#include <avr/pgmspace.h>

namespace ThermioFont5x7 {

static const uint8_t GlyphWidth = 5;
static const uint8_t GlyphHeight = 7;
static const uint8_t GlyphCount = 36;

static const uint8_t Glyphs[GlyphCount][GlyphWidth] PROGMEM = {
  {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
  {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
  {0x62, 0x51, 0x49, 0x49, 0x46}, // 2
  {0x22, 0x41, 0x49, 0x49, 0x36}, // 3
  {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
  {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
  {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
  {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
  {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
  {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
  {0x7E, 0x09, 0x09, 0x09, 0x7E}, // A
  {0x7F, 0x49, 0x49, 0x49, 0x36}, // B
  {0x3E, 0x41, 0x41, 0x41, 0x22}, // C
  {0x7F, 0x41, 0x41, 0x22, 0x1C}, // D
  {0x7F, 0x49, 0x49, 0x49, 0x41}, // E
  {0x7F, 0x09, 0x09, 0x09, 0x01}, // F
  {0x3E, 0x41, 0x49, 0x49, 0x7A}, // G
  {0x7F, 0x08, 0x08, 0x08, 0x7F}, // H
  {0x00, 0x41, 0x7F, 0x41, 0x00}, // I
  {0x20, 0x40, 0x41, 0x3F, 0x01}, // J
  {0x7F, 0x08, 0x14, 0x22, 0x41}, // K
  {0x7F, 0x40, 0x40, 0x40, 0x40}, // L
  {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // M
  {0x7F, 0x04, 0x08, 0x10, 0x7F}, // N
  {0x3E, 0x41, 0x41, 0x41, 0x3E}, // O
  {0x7F, 0x09, 0x09, 0x09, 0x06}, // P
  {0x3E, 0x41, 0x51, 0x21, 0x5E}, // Q
  {0x7F, 0x09, 0x19, 0x29, 0x46}, // R
  {0x46, 0x49, 0x49, 0x49, 0x31}, // S
  {0x01, 0x01, 0x7F, 0x01, 0x01}, // T
  {0x3F, 0x40, 0x40, 0x40, 0x3F}, // U
  {0x1F, 0x20, 0x40, 0x20, 0x1F}, // V
  {0x3F, 0x40, 0x38, 0x40, 0x3F}, // W
  {0x63, 0x14, 0x08, 0x14, 0x63}, // X
  {0x07, 0x08, 0x70, 0x08, 0x07}, // Y
  {0x61, 0x51, 0x49, 0x45, 0x43}  // Z
};

inline uint8_t glyphIndex(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'A' && c <= 'Z') {
    return 10 + c - 'A';
  }
  return 0xFF;
}

inline uint8_t glyphColumn(char c, uint8_t x) {
  const uint8_t index = glyphIndex(c);
  if (index >= GlyphCount || x >= GlyphWidth) {
    return 0x00;
  }
  return pgm_read_byte(&Glyphs[index][x]);
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
  if (relY >= (uint16_t)GlyphHeight * scale) {
    return false;
  }

  const uint8_t charPitch = 6 * scale;
  const uint8_t charIndex = relX / charPitch;
  const char *target = text;
  for (uint8_t i = 0; i < charIndex; i++) {
    if (*target == '\0') {
      return false;
    }
    target++;
  }

  const uint8_t col = (relX % charPitch) / scale;
  if (col >= GlyphWidth) {
    return false;
  }

  const char c = *target;
  if (c == '\0') {
    return false;
  }

  const uint8_t row = relY / scale;
  return (glyphColumn(c, col) & (1 << row)) != 0;
}

inline bool textPixel_P(PGM_P text,
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
  if (relY >= (uint16_t)GlyphHeight * scale) {
    return false;
  }

  const uint8_t charPitch = 6 * scale;
  const uint8_t charIndex = relX / charPitch;
  char c = '\0';
  for (uint8_t i = 0; i <= charIndex; i++) {
    c = (char)pgm_read_byte(text + i);
    if (c == '\0') {
      return false;
    }
  }

  const uint8_t col = (relX % charPitch) / scale;
  if (col >= GlyphWidth) {
    return false;
  }

  const uint8_t row = relY / scale;
  return (glyphColumn(c, col) & (1 << row)) != 0;
}

}

#endif
