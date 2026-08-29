#ifndef THERMIO_ICONS_H
#define THERMIO_ICONS_H

#include <Arduino.h>
#include <avr/pgmspace.h>

namespace ThermioIcons {

static const uint8_t SetpointWidth = 24;
static const uint8_t SetpointHeight = 24;
static const uint8_t OutsideWidth = 24;
static const uint8_t OutsideHeight = 24;
static const uint8_t TrayIconWidth = 16;
static const uint8_t TrayIconHeight = 16;

static const uint32_t Setpoint24[SetpointHeight] PROGMEM = {
  0x000000,
  0x000000,
  0x000000,
  0x000800,
  0x001C00,
  0x003600,
  0x003300,
  0x3FF180,
  0x7FF4C0,
  0x600260,
  0x600130,
  0x600098,
  0x600118,
  0x600230,
  0x600460,
  0x7FF0C0,
  0x7FF180,
  0x003300,
  0x003600,
  0x001C00,
  0x000800,
  0x000000,
  0x000000,
  0x000000
};

static const uint32_t Outside24[OutsideHeight] PROGMEM = {
  0x7FC200,
  0x7F8700,
  0x600D80,
  0x6618C0,
  0x6F9060,
  0x6CC030,
  0x676018,
  0x61B00C,
  0x40D806,
  0x06680B,
  0x05240D,
  0x01140C,
  0x01940C,
  0x01880C,
  0x01800C,
  0x01800C,
  0x01800C,
  0x01800C,
  0x01800C,
  0x01800C,
  0x01800C,
  0x01800C,
  0x01FFFC,
  0x000000
};

static const uint16_t BatteryLow16[TrayIconHeight] PROGMEM = {
  0x0000,
  0x0000,
  0x0000,
  0x7FF0,
  0xFFF8,
  0xC01B,
  0xD01B,
  0xD01B,
  0xD01B,
  0xD01B,
  0xC01B,
  0xFFF8,
  0x7FF0,
  0x0000,
  0x0000,
  0x0000
};

static const uint16_t ConsoleOk16[TrayIconHeight] PROGMEM = {
  0x0000,
  0x43C2,
  0x6666,
  0x300C,
  0x1998,
  0x0C18,
  0x0630,
  0x0360,
  0x01C0,
  0x00C0,
  0x00C0,
  0x00C0,
  0x00C0,
  0x00C0,
  0x00C0,
  0x0000
};

static const uint16_t Motion16[TrayIconHeight] PROGMEM = {
  0x0380,
  0x0440,
  0x0AA0,
  0x0820,
  0x0920,
  0x0440,
  0x0380,
  0x0120,
  0x0160,
  0x0780,
  0x1900,
  0x0100,
  0x0100,
  0x03C0,
  0x0240,
  0x0440
};

static const uint16_t Heat16[TrayIconHeight] PROGMEM = {
  0x0000,
  0x0000,
  0x0CCC,
  0x0444,
  0x0444,
  0x0CCC,
  0x1998,
  0x1998,
  0x0CCC,
  0x0000,
  0x3FFE,
  0x4002,
  0x5FFA,
  0x4002,
  0x5FFA,
  0x4002
};

inline bool trayIconPixel(const uint16_t *icon, uint8_t iconX, uint8_t iconY) {
  if (iconX >= TrayIconWidth || iconY >= TrayIconHeight) {
    return false;
  }
  const uint16_t row = pgm_read_word(&icon[iconY]);
  return (row & (1U << (TrayIconWidth - 1 - iconX))) != 0;
}

inline bool trayIconPixelAt(const uint16_t *icon,
                            uint8_t x0,
                            uint8_t y0,
                            uint16_t x,
                            uint16_t y) {
  if (x < x0 || y < y0) {
    return false;
  }
  return trayIconPixel(icon, x - x0, y - y0);
}

inline bool setpointPixel(uint8_t iconX, uint8_t iconY) {
  if (iconX >= SetpointWidth || iconY >= SetpointHeight) {
    return false;
  }
  const uint32_t row = pgm_read_dword(&Setpoint24[iconY]);
  return (row & (1UL << (SetpointWidth - 1 - iconX))) != 0;
}

inline bool setpointPixelAt(uint8_t x0,
                            uint8_t y0,
                            uint16_t x,
                            uint16_t y) {
  if (x < x0 || y < y0) {
    return false;
  }
  return setpointPixel(x - x0, y - y0);
}

inline bool outsidePixel(uint8_t iconX, uint8_t iconY) {
  if (iconX >= OutsideWidth || iconY >= OutsideHeight) {
    return false;
  }
  const uint32_t row = pgm_read_dword(&Outside24[iconY]);
  return (row & (1UL << (OutsideWidth - 1 - iconX))) != 0;
}

inline bool outsidePixelAt(uint8_t x0,
                           uint8_t y0,
                           uint16_t x,
                           uint16_t y) {
  if (x < x0 || y < y0) {
    return false;
  }
  return outsidePixel(x - x0, y - y0);
}

}

#endif
