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

#include "SondeDataService.h"
#include "SondeUi.h"
#include "graphics/ThermioEink097.h"

constexpr uint8_t PIN_LED = 5;       // PCINT21 / PD5 / D5
constexpr uint8_t PIN_RF_CSN = 10;   // PCINT2 / PB2 / D10
constexpr uint8_t PIN_EPD_BUSY = A2; // PCINT10 / PC2 / A2
constexpr uint8_t PIN_EPD_RST = 4;   // PCINT20 / PD4 / D4
constexpr uint8_t PIN_EPD_DC = 3;    // PCINT19 / PD3 / D3
constexpr uint8_t PIN_EPD_CS = 6;    // PCINT22 / PD6 / D6

constexpr uint32_t UI_CLOCK_REFRESH_MS = 300000;
constexpr UiPage FORCE_SCREEN_TEST_PAGE = UI_PAGE_HOME;

const ThermioEink097::Pins einkPins = {
  PIN_EPD_CS,
  PIN_EPD_DC,
  PIN_EPD_RST,
  PIN_EPD_BUSY,
  PIN_RF_CSN
};

ThermioEink097 eink(einkPins);
SondeDataService dataService;

bool lastDisplayOk = false;
uint32_t nextClockRefreshAt = 0;

void ledOn() {
  digitalWrite(PIN_LED, HIGH);
}

void ledOff() {
  digitalWrite(PIN_LED, LOW);
}

void isolateRfSpi() {
  digitalWrite(PIN_RF_CSN, HIGH);
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
  const bool refreshOk = eink.writeFrontImage(sondeScreenPixel, &ui);

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

  if (FORCE_SCREEN_TEST_PAGE != UI_PAGE_HOME) {
    ui.page = FORCE_SCREEN_TEST_PAGE;
    updateDisplay();
    return;
  }

  dataService.begin();
  dataService.update(millis(), true);
  updateDisplay();
  nextClockRefreshAt = millis() + UI_CLOCK_REFRESH_MS;
}

void loop() {
  if (FORCE_SCREEN_TEST_PAGE != UI_PAGE_HOME) {
    ledOff();
    isolateRfSpi();
    return;
  }

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
