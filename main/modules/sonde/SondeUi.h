#ifndef SONDE_UI_H
#define SONDE_UI_H

#include <Arduino.h>

enum UiPage : uint8_t {
  UI_PAGE_HOME,
  UI_PAGE_MENU,
  UI_PAGE_BATTERY_DEAD,
  UI_PAGE_STOP,
  UI_PAGE_VACATION
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
  bool batteryLow;
  bool consoleOk;
  bool motionDetected;
  bool heatActive;
};

extern UiState ui;

bool sondeScreenPixel(uint16_t x, uint16_t y, void *context);

#endif
