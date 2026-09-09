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
#include <ThermioRfCc1101.h>

#include "SondeBatteryService.h"
#include "SondeDataService.h"
#include "SondeInputService.h"
#include "SondeRfStatusService.h"
#include "SondeUi.h"
#include "graphics/ThermioEink097.h"

constexpr uint8_t PIN_LED = 5;       // PCINT21 / PD5 / D5
constexpr uint8_t PIN_RF_CSN = 10;   // PCINT2 / PB2 / D10
constexpr uint8_t PIN_EPD_BUSY = A2; // PCINT10 / PC2 / A2
constexpr uint8_t PIN_EPD_RST = 4;   // PCINT20 / PD4 / D4
constexpr uint8_t PIN_EPD_DC = 3;    // PCINT19 / PD3 / D3
constexpr uint8_t PIN_EPD_CS = 6;    // PCINT22 / PD6 / D6
constexpr uint8_t PIN_BAT_SENS = A0;  // PCINT8 / PC0 / A0
constexpr uint8_t PIN_BODYDETECT = 7; // PCINT23 / PD7 / D7
constexpr uint8_t PIN_CMD_SENS1 = 0;  // PCINT16 / PD0 / D0, actif GND
constexpr uint8_t PIN_CMD_SENS2 = A1; // PCINT9 / PC1 / A1, actif GND
constexpr uint8_t PIN_CMD_BTN = A3;   // PCINT11 / PC3 / A3, actif GND
constexpr uint8_t PIN_WAKE = 2;       // PCINT18 / PD2 / D2
constexpr uint8_t PIN_RF_GDO0 = 9;     // PCINT1 / PB1 / D9
constexpr uint8_t PIN_RF_MOSI = 11;    // PCINT3 / PB3 / D11
constexpr uint8_t PIN_RF_MISO = 12;    // PCINT4 / PB4 / D12
constexpr uint8_t PIN_RF_SCK = 13;     // PCINT5 / PB5 / D13

constexpr UiPage FORCE_SCREEN_TEST_PAGE = UI_PAGE_HOME;
constexpr bool ENABLE_RF_STARTUP_SELF_TEST = true;
constexpr bool FAST_SETPOINT_ENTRY_TEST = true;
constexpr uint32_t EPD_SPI_HZ = 2000000;
constexpr int16_t SETPOINT_STEP_DECI_C = 5;
constexpr int16_t SETPOINT_MIN_DECI_C = 50;
constexpr int16_t SETPOINT_MAX_DECI_C = 300;
constexpr uint32_t CRITICAL_BATTERY_REPORT_MS = 3600000;
constexpr uint32_t SENSOR_PAUSE_AFTER_INPUT_MS = 120000;
constexpr uint32_t SETPOINT_EDIT_TIMEOUT_MS = 5000;
constexpr uint16_t DISPLAY_SEND_MARKER_MS = 250;
constexpr uint8_t FULL_PARTIAL_REFRESH_X = 0;
constexpr uint8_t FULL_PARTIAL_REFRESH_Y = 0;
constexpr uint8_t FULL_PARTIAL_REFRESH_W = ThermioEink097::FrontWidth;
constexpr uint8_t FULL_PARTIAL_REFRESH_H = ThermioEink097::FrontHeight;
constexpr uint8_t HOME_DIGITS_REFRESH_X = 0;
constexpr uint8_t HOME_DIGITS_REFRESH_Y = 18;
constexpr uint8_t HOME_DIGITS_REFRESH_W = ThermioEink097::FrontWidth;
constexpr uint8_t HOME_DIGITS_REFRESH_H = 56;
constexpr uint8_t FULL_REFRESH_PENDING_X = 0;
constexpr uint8_t FULL_REFRESH_PENDING_Y = 29;
constexpr uint8_t FULL_REFRESH_PENDING_W = ThermioEink097::FrontWidth;
constexpr uint8_t FULL_REFRESH_PENDING_H = 30;

const ThermioEink097::Pins einkPins = {
  PIN_EPD_CS,
  PIN_EPD_DC,
  PIN_EPD_RST,
  PIN_EPD_BUSY,
  PIN_RF_CSN
};

const ThermioRfCc1101::Pins rfPins = {
  PIN_RF_CSN,
  PIN_RF_GDO0,
  PIN_RF_MOSI,
  PIN_RF_MISO,
  PIN_RF_SCK
};

ThermioEink097 eink(einkPins, SPISettings(EPD_SPI_HZ, MSBFIRST, SPI_MODE0));
ThermioRfCc1101 radio(rfPins, SPISettings(1000000, MSBFIRST, SPI_MODE0));
SondeBatteryService batteryService({PIN_BAT_SENS});
SondeDataService dataService;
SondeInputService inputService({
  PIN_BODYDETECT,
  PIN_CMD_SENS1,
  PIN_CMD_SENS2,
  PIN_CMD_BTN,
  PIN_WAKE
});
SondeRfStatusService rfStatusService(radio);

bool lastDisplayOk = false;
UiState displayedUi = ui;
uint32_t criticalBatteryReportUntilAt = 0;
uint32_t setpointEditUntilAt = 0;
bool armSetpointEditTimeoutAfterRefresh = false;
bool setpointEditEntered = false;
bool batteryTerminalMode = false;
bool startupTerminalMode = false;

void ledOn() {
  digitalWrite(PIN_LED, HIGH);
}

void ledOff() {
  digitalWrite(PIN_LED, LOW);
}

void isolateRfSpi() {
  digitalWrite(PIN_RF_CSN, HIGH);
}

void markDisplaySendStart() {
  ledOff();
  delay(DISPLAY_SEND_MARKER_MS);
  ledOn();
}

bool fullRefreshPendingPixel(uint16_t x, uint16_t y, void *context) {
  (void)context;
  if (y < FULL_REFRESH_PENDING_Y ||
      y >= (uint16_t)FULL_REFRESH_PENDING_Y + FULL_REFRESH_PENDING_H) {
    return false;
  }

  const uint8_t stripe = (x + y * 2) % 18;
  return stripe < 5;
}

void syncUiFromDataService() {
  ui.currentTempKnown = dataService.currentTempKnown();
  if (ui.currentTempKnown) {
    ui.currentTempDeciC = dataService.currentTempDeciC();
  }
  ui.batteryMv = batteryService.batteryMv();
  ui.batteryLow = batteryService.batteryLow();
  ui.batteryCritical = batteryService.batteryCritical();
  ui.consoleOk = rfStatusService.consoleOk(millis());
  ui.bootMinutes = millis() / 60000UL;
}

bool applyInputEvent(SondeInputEvent event) {
  if (ui.page != UI_PAGE_HOME) {
    return false;
  }

  if (event != SONDE_INPUT_PLUS && event != SONDE_INPUT_MINUS) {
    return false;
  }

  ui.motionDetected = true;
  dataService.pauseUntil(millis() + SENSOR_PAUSE_AFTER_INPUT_MS);
  armSetpointEditTimeoutAfterRefresh = true;

  if (!ui.setpointEditing) {
    ui.setpointEditing = true;
    setpointEditEntered = true;
    return true;
  }

  int16_t nextSetpoint = ui.setpointDeciC;
  nextSetpoint += event == SONDE_INPUT_PLUS ? SETPOINT_STEP_DECI_C : -SETPOINT_STEP_DECI_C;
  if (nextSetpoint < SETPOINT_MIN_DECI_C) {
    nextSetpoint = SETPOINT_MIN_DECI_C;
  }
  if (nextSetpoint > SETPOINT_MAX_DECI_C) {
    nextSetpoint = SETPOINT_MAX_DECI_C;
  }
  ui.setpointDeciC = nextSetpoint;
  return true;
}

bool enterBatteryTerminalMode(uint32_t now) {
  if (!batteryService.batteryCritical() || batteryTerminalMode) {
    return false;
  }

  batteryTerminalMode = true;
  criticalBatteryReportUntilAt = now + CRITICAL_BATTERY_REPORT_MS;
  ui.page = UI_PAGE_BATTERY_DEAD;
  return true;
}

void showFullRefreshPendingIndicator() {
  const bool wakeOk = eink.wakeForPartialUpdate();
  if (wakeOk) {
    eink.writeFrontImagePartial(fullRefreshPendingPixel,
                                nullptr,
                                FULL_REFRESH_PENDING_X,
                                FULL_REFRESH_PENDING_Y,
                                FULL_REFRESH_PENDING_W,
                                FULL_REFRESH_PENDING_H);
  }
  eink.sleep();
}

void updateDisplay() {
  ledOn();
  isolateRfSpi();
  syncUiFromDataService();

  showFullRefreshPendingIndicator();

  const bool initOk = eink.init();
  ui.displayOk = initOk && lastDisplayOk;
  markDisplaySendStart();
  const bool refreshOk = eink.writeFrontImage(sondeScreenPixel, &ui);

  lastDisplayOk = initOk && refreshOk;
  ui.displayOk = lastDisplayOk;
  if (lastDisplayOk) {
    displayedUi = ui;
    if (ui.setpointEditing) {
      setpointEditUntilAt = millis() + SETPOINT_EDIT_TIMEOUT_MS;
      armSetpointEditTimeoutAfterRefresh = false;
    } else {
      armSetpointEditTimeoutAfterRefresh = false;
    }
    ledOff();
  }
  eink.sleep();
}

void updateDisplayPartial(uint8_t x, uint8_t y, uint8_t w, uint8_t h) {
  if (!lastDisplayOk) {
    updateDisplay();
    return;
  }

  ledOn();
  isolateRfSpi();
  syncUiFromDataService();

  const bool wakeOk = eink.wakeForPartialUpdate();
  markDisplaySendStart();
  const bool refreshOk = wakeOk &&
      eink.writeFrontImagePartial(sondeScreenPixel,
                                  &displayedUi,
                                  sondeScreenPixel,
                                  &ui,
                                  x,
                                  y,
                                  w,
                                  h);

  lastDisplayOk = wakeOk && refreshOk;
  ui.displayOk = lastDisplayOk;
  if (lastDisplayOk) {
    displayedUi = ui;
    if (ui.setpointEditing) {
      setpointEditUntilAt = millis() + SETPOINT_EDIT_TIMEOUT_MS;
      armSetpointEditTimeoutAfterRefresh = false;
    } else {
      armSetpointEditTimeoutAfterRefresh = false;
    }
    ledOff();
  }
  eink.sleep();
}

void updateDisplayPartialCurrentOnly(uint8_t x, uint8_t y, uint8_t w, uint8_t h) {
  if (!lastDisplayOk) {
    updateDisplay();
    return;
  }

  ledOn();
  isolateRfSpi();
  syncUiFromDataService();

  const bool wakeOk = eink.wakeForPartialUpdate();
  markDisplaySendStart();
  const bool refreshOk = wakeOk &&
      eink.writeFrontImagePartial(sondeScreenPixel, &ui, x, y, w, h);

  lastDisplayOk = wakeOk && refreshOk;
  ui.displayOk = lastDisplayOk;
  if (lastDisplayOk) {
    displayedUi = ui;
    if (ui.setpointEditing) {
      setpointEditUntilAt = millis() + SETPOINT_EDIT_TIMEOUT_MS;
      armSetpointEditTimeoutAfterRefresh = false;
    } else {
      armSetpointEditTimeoutAfterRefresh = false;
    }
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
  inputService.begin();
  batteryService.begin();
  rfStatusService.begin();

  if (FORCE_SCREEN_TEST_PAGE != UI_PAGE_HOME) {
    ui.page = FORCE_SCREEN_TEST_PAGE;
    updateDisplay();
    return;
  }

  if (ENABLE_RF_STARTUP_SELF_TEST && !rfStatusService.spiOk()) {
    rfStatusService.update(millis(), true);
    if (!rfStatusService.spiOk()) {
      startupTerminalMode = true;
      ui.page = UI_PAGE_RF_ERROR;
      updateDisplay();
      return;
    }
  }

  dataService.begin();
  batteryService.update(millis(), true);
  rfStatusService.update(millis(), true);
  enterBatteryTerminalMode(millis());
  ui.motionDetected = inputService.motionDetected();
  dataService.update(millis(), true);
  updateDisplay();
}

void loop() {
  if (FORCE_SCREEN_TEST_PAGE != UI_PAGE_HOME || startupTerminalMode) {
    ledOff();
    isolateRfSpi();
    return;
  }

  const uint32_t now = millis();
  if (batteryTerminalMode) {
    ledOff();
    isolateRfSpi();
    return;
  }

  bool displayNeedsRefresh = dataService.update(now);
  if (batteryService.update(now)) {
    displayNeedsRefresh = true;
  }
  if (rfStatusService.update(now)) {
    displayNeedsRefresh = true;
  }
  if (enterBatteryTerminalMode(now)) {
    displayNeedsRefresh = true;
  }

  const SondeInputEvent inputEvent = inputService.update(now);

  ui.motionDetected = inputService.motionDetected();
  bool displayNeedsDigitsRefresh = false;
  bool displayNeedsFullRefresh = false;
  if (applyInputEvent(inputEvent)) {
    if (setpointEditEntered) {
      setpointEditEntered = false;
      displayNeedsFullRefresh = !FAST_SETPOINT_ENTRY_TEST;
      displayNeedsDigitsRefresh = FAST_SETPOINT_ENTRY_TEST;
    }
    if (!displayNeedsRefresh) {
      if (displayNeedsFullRefresh) {
        updateDisplay();
      } else {
        updateDisplayPartialCurrentOnly(HOME_DIGITS_REFRESH_X,
                                        HOME_DIGITS_REFRESH_Y,
                                        HOME_DIGITS_REFRESH_W,
                                        HOME_DIGITS_REFRESH_H);
      }
      return;
    }
    displayNeedsDigitsRefresh = !displayNeedsFullRefresh;
  }

  if (ui.setpointEditing && !armSetpointEditTimeoutAfterRefresh &&
      (int32_t)(now - setpointEditUntilAt) >= 0) {
    ui.setpointEditing = false;
    displayNeedsFullRefresh = true;
  }

  if (displayNeedsFullRefresh) {
    updateDisplay();
  } else if (displayNeedsRefresh) {
    updateDisplayPartial(FULL_PARTIAL_REFRESH_X,
                         FULL_PARTIAL_REFRESH_Y,
                         FULL_PARTIAL_REFRESH_W,
                         FULL_PARTIAL_REFRESH_H);
  } else if (displayNeedsDigitsRefresh) {
    updateDisplayPartialCurrentOnly(HOME_DIGITS_REFRESH_X,
                                    HOME_DIGITS_REFRESH_Y,
                                    HOME_DIGITS_REFRESH_W,
                                    HOME_DIGITS_REFRESH_H);
  }

  if (lastDisplayOk) {
    ledOff();
  }
  isolateRfSpi();
}
