#ifndef SONDE_UI_H
#define SONDE_UI_H

#include <Arduino.h>

enum UiPage : uint8_t {
  UI_PAGE_HOME,
  UI_PAGE_MENU,
  UI_PAGE_BATTERY_DEAD,
  UI_PAGE_STOP,
  UI_PAGE_VACATION,
  UI_PAGE_RF_ERROR
};

enum UiMenuPage : uint8_t {
  UI_MENU_OUTSIDE,
  UI_MENU_PRESENCE,
  UI_MENU_BATTERY,
  UI_MENU_CONSOLE,
  UI_MENU_THERMOMETER,
  UI_MENU_LEARNING,
  UI_MENU_COUNT
};

enum UiMenuSubPage : uint8_t {
  UI_SUB_NONE,
  UI_SUB_BATTERY_VOLTAGE,
  UI_SUB_BATTERY_THRESHOLDS,
  UI_SUB_BATTERY_STATE,
  UI_SUB_CONSOLE_ZONE,
  UI_SUB_CONSOLE_PAIRING,
  UI_SUB_CONSOLE_RF_DEBUG,
  UI_SUB_CONSOLE_IDS,
  UI_SUB_CONSOLE_LAST_RESPONSE,
  UI_SUB_THERMO_RAW,
  UI_SUB_THERMO_OFFSET,
  UI_SUB_THERMO_CORRECTED,
  UI_SUB_LEARNING_SETPOINT,
  UI_SUB_LEARNING_SOURCE,
  UI_SUB_LEARNING_RESET_ZONE,
  UI_SUB_LEARNING_RESET_GLOBAL
};

struct UiState {
  UiPage page;
  UiMenuPage menuPage;
  UiMenuSubPage menuSubPage;
  int16_t currentTempDeciC;
  int16_t rawTempDeciC;
  int16_t setpointDeciC;
  int16_t outsideTempDeciC;
  int16_t ahtOffsetDeciC;
  uint16_t batteryMv;
  uint16_t bootMinutes;
  uint16_t localRfId;
  uint16_t consoleRfId;
  uint8_t assignedZone;
  uint8_t lastGlobalMode;
  bool currentTempKnown;
  bool outsideTempKnown;
  bool displayOk;
  bool batteryLow;
  bool batteryCritical;
  bool rfSpiOk;
  bool consoleOk;
  bool motionDetected;
  bool heatActive;
  bool heatLastHour;
  bool zoneDoorOpen;
  bool setpointEditing;
  bool menuInSubmenu;
  bool menuEditing;
};

extern UiState ui;

uint8_t uiMenuSubCount(UiMenuPage page);
UiMenuSubPage uiMenuSubAt(UiMenuPage page, uint8_t index);
int8_t uiMenuSubIndex(UiMenuPage page, UiMenuSubPage subPage);

bool sondeScreenPixel(uint16_t x, uint16_t y, void *context);

#endif
