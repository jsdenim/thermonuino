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
#include <EEPROM.h>
#include <ThermioRfCc1101.h>
#include <ThermioRfFrame.h>
#include <ThermioRfIds.h>
#include <ThermioSlaveLink.h>
#include <ThermioSlavePower.h>

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
constexpr uint16_t RF_DEFAULT_NODE_ID = 0x0501;
constexpr uint8_t RF_ASSOC_ZONE_COUNT = 5;
constexpr uint32_t RF_CONSOLE_LEARN_WINDOW_MS = 180000;
constexpr uint16_t RF_REPORT_INTERVAL_WATCHDOG_TICKS = 8; // ~64 s for the current test phase.
constexpr uint16_t RF_STARTUP_AUTO_REPORT_DELAY_WATCHDOG_TICKS = 2; // ~16 s
constexpr uint16_t RF_CHANNEL_LISTEN_MS = 30;
constexpr uint16_t RF_ACK_TIMEOUT_MS = 2000;
constexpr uint16_t RF_RX_SETTLE_MS = 50;
constexpr uint16_t RF_TX_COMPLETE_TIMEOUT_MS = 350;
constexpr uint8_t RF_MAX_ATTEMPTS = 6;
constexpr uint8_t RF_TX_COPIES_PER_ATTEMPT = 3;
constexpr uint16_t RF_TX_COPY_GAP_MS = 60;
constexpr uint16_t RF_BACKOFF_MIN_MS = 100;
constexpr uint16_t RF_BACKOFF_SPAN_MS = 500;
constexpr uint16_t RF_BACKOFF_STEP_MS = 150;
constexpr int16_t SETPOINT_STEP_DECI_C = 5;
constexpr int16_t SETPOINT_MIN_DECI_C = 50;
constexpr int16_t SETPOINT_MAX_DECI_C = 300;
constexpr uint32_t CRITICAL_BATTERY_REPORT_MS = 3600000;
constexpr uint32_t SENSOR_PAUSE_AFTER_INPUT_MS = 120000;
constexpr uint32_t SETPOINT_EDIT_TIMEOUT_MS = 5000;
constexpr uint32_t MENU_IDLE_TIMEOUT_MS = 60000;
constexpr uint32_t CENTER_LONG_PRESS_MS = 3000;
constexpr uint16_t DISPLAY_SEND_MARKER_MS = 250;
constexpr uint8_t TEMPERATURE_REFRESH_WATCHDOG_TICKS = 8; // 8 x 8 s ~= 1 min.
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

constexpr ThermioRfIds::EepromSlot EEPROM_CONSOLE_ID = {
  0, 1, 2, 3, 0x54, 0x43
};

constexpr ThermioRfIds::EepromSlot EEPROM_NODE_ID = {
  4, 5, 6, 7, 0x54, 0x4E
};

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
ThermioSlaveLink link(RF_ASSOC_ZONE_COUNT);
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
uint8_t temperatureWatchdogTicks = 0;
uint32_t awakeWatchdogTicks = 0;
uint32_t autoReportEnabledAtWatchdogTick = 0;
uint16_t rfReportIntervalWatchdogTicks = RF_REPORT_INTERVAL_WATCHDOG_TICKS;
uint8_t rfSequence = 0;
uint8_t presenceCountSinceAck = 0;
int8_t pendingUserDeltaSteps = 0;
bool pendingRfReport = false;
bool lastMotionDetectedForReport = false;
uint32_t menuLastInteractionAt = 0;
uint32_t centerPressedAt = 0;
bool centerWasPressed = false;
bool centerLongHandled = false;

void ledOn() {
  digitalWrite(PIN_LED, HIGH);
}

void ledOff() {
  digitalWrite(PIN_LED, LOW);
}

void isolateRfSpi() {
  digitalWrite(PIN_RF_CSN, HIGH);
}

void clearLocalEeprom() {
  for (int address = 0; address < EEPROM.length(); address++) {
    EEPROM.update(address, 0xFF);
  }
  link.clearConsoleId();
}

void waitForNodeIdCreation() {
  while (digitalRead(PIN_CMD_BTN) == LOW) {
    ledOn();
    delay(80);
    ledOff();
    delay(220);
  }

  while (digitalRead(PIN_CMD_BTN) != LOW) {
    ledOn();
    delay(40);
    ledOff();
    delay(460);
  }

  const uint16_t nodeId = ThermioRfIds::generate(PIN_BAT_SENS, RF_DEFAULT_NODE_ID);
  ThermioRfIds::save(EEPROM_NODE_ID, nodeId);
  link.setLocalId(nodeId);
  for (uint8_t i = 0; i < 3; i++) {
    ledOn();
    delay(120);
    ledOff();
    delay(120);
  }
  while (digitalRead(PIN_CMD_BTN) == LOW) {
    delay(10);
  }
}

void markDisplaySendStart() {
  ledOff();
  delay(DISPLAY_SEND_MARKER_MS);
  ledOn();
}

bool consumeTemperatureRefreshWake(uint16_t watchdogTicks) {
  if (watchdogTicks == 0) {
    return false;
  }

  if (watchdogTicks >= TEMPERATURE_REFRESH_WATCHDOG_TICKS ||
      temperatureWatchdogTicks + watchdogTicks >= TEMPERATURE_REFRESH_WATCHDOG_TICKS) {
    temperatureWatchdogTicks = 0;
    return true;
  }

  temperatureWatchdogTicks += watchdogTicks;
  return false;
}

void sleepWhenIdle() {
  ledOff();
  isolateRfSpi();
  radio.sleep();
  ThermioSlavePower::sleepPowerDown(false);
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
    ui.rawTempDeciC = dataService.rawTempDeciC();
  }
  ui.ahtOffsetDeciC = dataService.temperatureOffsetDeciC();
  ui.batteryMv = batteryService.batteryMv();
  ui.batteryLow = batteryService.batteryLow();
  ui.batteryCritical = batteryService.batteryCritical();
  ui.rfSpiOk = rfStatusService.spiOk();
  ui.consoleOk = rfStatusService.consoleOk(millis());
  ui.localRfId = link.localId();
  ui.consoleRfId = link.consoleId();
  ui.assignedZone = link.assignedZone();
  ui.bootMinutes = millis() / 60000UL;
}

void capturePresenceForReport(bool motionDetected) {
  if (motionDetected && !lastMotionDetectedForReport && presenceCountSinceAck < 255) {
    presenceCountSinceAck++;
  }
  lastMotionDetectedForReport = motionDetected;
}

void markPresenceForReport() {
  if (presenceCountSinceAck < 255) {
    presenceCountSinceAck++;
  }
}

uint16_t nextReportDelayToWatchdogTicks(uint16_t seconds) {
  uint32_t ticks = ((uint32_t)seconds + 7) / 8;
  if (ticks < 1) {
    ticks = 1;
  }
  if (ticks > ThermioSlaveLink::ConsoleOfflineRetryWatchdogTicks) {
    ticks = ThermioSlaveLink::ConsoleOfflineRetryWatchdogTicks;
  }
  return ticks;
}

uint8_t buildReportPacket(uint8_t *packet, uint8_t sequence) {
  ThermioRfFrame::Header header;
  header.frameType = ThermioRfFrame::FrameReport;
  header.sourceId = link.localId();
  header.targetId = link.reportTargetId();
  header.sequence = sequence;
  header.ackSequence = 0xFF;
  header.payloadLen = ThermioRfFrame::ReportPayloadLen;
  ThermioRfFrame::writeHeader(packet, header);

  ThermioRfFrame::Report report;
  report.deviceType = ThermioRfFrame::DeviceSonde;
  report.batteryMv = batteryService.batteryMv();
  report.userDeltaSteps = pendingUserDeltaSteps;
  report.hasAhtOffset = true;
  report.ahtOffsetDeciC = dataService.temperatureOffsetDeciC();
  report.tempCount = dataService.currentTempKnown() ? 1 : 0;
  if (report.tempCount > 0) {
    report.temperaturesDeciC[0] = dataService.currentTempDeciC();
  }
  report.presenceCount = presenceCountSinceAck;
  report.doorToggleCount = 0;
  report.doorOpen = false;
  ThermioRfFrame::encodeReportPayload(packet + ThermioRfFrame::HeaderLen, report);
  return ThermioRfFrame::HeaderLen + ThermioRfFrame::ReportPayloadLen;
}

bool applyConsoleResponse(const ThermioRfFrame::Response &response, uint32_t now) {
  bool displayChanged = false;
  const uint8_t previousAssignedZone = link.assignedZone();

  if (response.assignedZone >= 1 && response.assignedZone <= RF_ASSOC_ZONE_COUNT) {
    link.markAckReceived(response.assignedZone);
  } else {
    link.markAckReceived(0);
  }

  if (response.nextReportDelayS > 0) {
    rfReportIntervalWatchdogTicks = nextReportDelayToWatchdogTicks(response.nextReportDelayS);
  }

  const int16_t previousSetpoint = ui.setpointDeciC;
  const int16_t previousOutside = ui.outsideTempDeciC;
  const bool previousOutsideKnown = ui.outsideTempKnown;
  const int16_t previousOffset = dataService.temperatureOffsetDeciC();
  const bool previousConsoleOk = rfStatusService.consoleOk(now);

  if (!ui.setpointEditing) {
    ui.setpointDeciC = response.currentSetpointDeciC;
  }
  ui.outsideTempDeciC = response.outsideTempDeciC;
  ui.outsideTempKnown = true;
  ui.lastGlobalMode = response.globalMode;
  ui.zoneDoorOpen = response.zoneDoorOpen;
  ui.heatActive = response.heatActive;
  ui.heatLastHour = (response.commandFlags & ThermioRfFrame::ResponseFlagHeatLastHour) != 0;
  dataService.setTemperatureOffsetDeciC(response.ahtOffsetDeciC);
  rfStatusService.recordConsoleResponse(now);

  if (previousOffset != response.ahtOffsetDeciC) {
    dataService.update(now, true);
  }

  displayChanged = previousConsoleOk != rfStatusService.consoleOk(now) ||
      previousAssignedZone != link.assignedZone() ||
      previousOutsideKnown != ui.outsideTempKnown ||
      previousOutside != ui.outsideTempDeciC ||
      previousOffset != response.ahtOffsetDeciC ||
      (!ui.setpointEditing && previousSetpoint != ui.setpointDeciC);
  return displayChanged;
}

bool readAck(uint8_t expectedSequence, bool &displayChanged) {
  if (radio.rxOverflow()) {
    radio.flushRx();
    radio.strobeRx();
    return false;
  }

  const uint8_t expectedLength = ThermioRfFrame::HeaderLen + ThermioRfFrame::ResponsePayloadLen;
  if (radio.rxBytes() < expectedLength + 1) {
    return false;
  }

  uint8_t packet[ThermioRfFrame::MaxPacketLen] = {0};
  const uint8_t length = radio.readPacket(packet, sizeof(packet));
  ThermioRfFrame::Header header;
  if (!ThermioRfFrame::readHeader(packet, length, header) ||
      header.frameType != ThermioRfFrame::FrameResponse ||
      header.targetId != link.localId() ||
      header.ackSequence != expectedSequence ||
      header.sourceId == link.localId()) {
    return false;
  }

  if (link.consoleIdKnown() && header.sourceId != link.consoleId()) {
    return false;
  }

  ThermioRfFrame::Response response;
  if (!ThermioRfFrame::decodeResponse(packet, length, response, RF_ASSOC_ZONE_COUNT)) {
    return false;
  }

  if (link.learnConsoleIdFromResponse(header.sourceId, millis(), RF_CONSOLE_LEARN_WINDOW_MS)) {
    ThermioRfIds::save(EEPROM_CONSOLE_ID, header.sourceId);
  }

  displayChanged = applyConsoleResponse(response, millis()) || displayChanged;
  presenceCountSinceAck = 0;
  pendingUserDeltaSteps = 0;
  return true;
}

bool runRfExchange() {
  radio.wake();
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  radio.configureTestRadio(ThermioRfFrame::MaxPacketLen);

  bool received = false;
  bool displayChanged = false;
  const uint8_t sequence = rfSequence++;
  for (uint8_t attempt = 0; attempt < RF_MAX_ATTEMPTS && !received; attempt++) {
    if (radio.channelBusy(RF_CHANNEL_LISTEN_MS)) {
      delay(random(RF_BACKOFF_MIN_MS, RF_BACKOFF_MIN_MS + RF_BACKOFF_SPAN_MS + 1) + attempt * RF_BACKOFF_STEP_MS);
    }

    for (uint8_t copy = 0; copy < RF_TX_COPIES_PER_ATTEMPT; copy++) {
      uint8_t packet[ThermioRfFrame::MaxPacketLen] = {0};
      const uint8_t length = buildReportPacket(packet, sequence);
      radio.writePacket(packet, length);
      radio.waitTxComplete(RF_TX_COMPLETE_TIMEOUT_MS);
      if (copy + 1 < RF_TX_COPIES_PER_ATTEMPT) {
        delay(RF_TX_COPY_GAP_MS);
      }
    }

    radio.strobeRx();
    delay(RF_RX_SETTLE_MS);
    const uint32_t rxStartedAt = millis();
    while ((uint32_t)(millis() - rxStartedAt) < RF_ACK_TIMEOUT_MS) {
      if (readAck(sequence, displayChanged)) {
        received = true;
        break;
      }
      delay(10);
    }

    if (!received) {
      delay(random(RF_BACKOFF_MIN_MS, RF_BACKOFF_MIN_MS + RF_BACKOFF_SPAN_MS + 1) + attempt * RF_BACKOFF_STEP_MS);
    }
  }

  radio.idle();
  SPI.endTransaction();
  radio.sleep();
  if (!received) {
    link.markAckMissed(awakeWatchdogTicks);
  }
  return displayChanged;
}

void touchMenu(uint32_t now) {
  menuLastInteractionAt = now;
}

void enterMenu(uint32_t now) {
  ui.page = UI_PAGE_MENU;
  ui.menuInSubmenu = false;
  ui.menuEditing = false;
  ui.setpointEditing = false;
  ui.menuSubPage = UI_SUB_NONE;
  touchMenu(now);
  updateDisplay();
}

void exitMenu() {
  ui.page = UI_PAGE_HOME;
  ui.menuInSubmenu = false;
  ui.menuEditing = false;
  ui.menuSubPage = UI_SUB_NONE;
  updateDisplay();
}

void refreshMenuFast() {
  updateDisplayPartialCurrentOnly(FULL_PARTIAL_REFRESH_X,
                                  FULL_PARTIAL_REFRESH_Y,
                                  FULL_PARTIAL_REFRESH_W,
                                  FULL_PARTIAL_REFRESH_H);
}

void moveMenuMain(int8_t direction) {
  int8_t next = (int8_t)ui.menuPage + direction;
  if (next < 0) {
    next = UI_MENU_COUNT - 1;
  } else if (next >= UI_MENU_COUNT) {
    next = 0;
  }
  ui.menuPage = (UiMenuPage)next;
  ui.menuSubPage = UI_SUB_NONE;
}

void moveMenuSub(int8_t direction) {
  const uint8_t count = uiMenuSubCount(ui.menuPage);
  if (count == 0) {
    return;
  }

  int8_t index = uiMenuSubIndex(ui.menuPage, ui.menuSubPage);
  if (index < 0) {
    index = 0;
  } else {
    index += direction;
  }
  if (index < 0) {
    index = count - 1;
  } else if (index >= count) {
    index = 0;
  }
  ui.menuSubPage = uiMenuSubAt(ui.menuPage, index);
}

bool currentMenuSubEditable() {
  return ui.menuSubPage == UI_SUB_THERMO_OFFSET;
}

void handleMenuCenterClick(uint32_t now) {
  touchMenu(now);
  if (!ui.menuInSubmenu) {
    if (uiMenuSubCount(ui.menuPage) == 0) {
      return;
    }
    ui.menuInSubmenu = true;
    ui.menuEditing = false;
    ui.menuSubPage = uiMenuSubAt(ui.menuPage, 0);
    refreshMenuFast();
    return;
  }

  if (currentMenuSubEditable()) {
    ui.menuEditing = !ui.menuEditing;
    if (!ui.menuEditing) {
      updateDisplay();
    } else {
      refreshMenuFast();
    }
    return;
  }

  ui.menuInSubmenu = false;
  ui.menuEditing = false;
  ui.menuSubPage = UI_SUB_NONE;
  updateDisplay();
}

bool updateCenterButton(uint32_t now) {
  const bool pressed = inputService.centerPressed();
  if (pressed && !centerWasPressed) {
    centerWasPressed = true;
    centerPressedAt = now;
    centerLongHandled = false;
  }

  if (pressed && !centerLongHandled &&
      (uint32_t)(now - centerPressedAt) >= CENTER_LONG_PRESS_MS) {
    centerLongHandled = true;
    if (ui.page == UI_PAGE_HOME) {
      enterMenu(now);
    } else if (ui.page == UI_PAGE_MENU) {
      exitMenu();
    }
    return true;
  }

  if (!pressed && centerWasPressed) {
    const bool shortPress = !centerLongHandled;
    centerWasPressed = false;
    centerLongHandled = false;
    if (shortPress && ui.page == UI_PAGE_MENU) {
      handleMenuCenterClick(now);
      return true;
    }
  }
  return false;
}

bool handleMenuInput(SondeInputEvent event, uint32_t now) {
  if (ui.page != UI_PAGE_MENU) {
    return false;
  }

  if (event == SONDE_INPUT_CENTER || event == SONDE_INPUT_NONE) {
    return false;
  }

  touchMenu(now);
  const int8_t direction = event == SONDE_INPUT_PLUS ? 1 : -1;
  if (ui.menuEditing && ui.menuSubPage == UI_SUB_THERMO_OFFSET) {
    int16_t nextOffset = ui.ahtOffsetDeciC + direction;
    if (nextOffset < -25) {
      nextOffset = -25;
    }
    if (nextOffset > 25) {
      nextOffset = 25;
    }
    dataService.setTemperatureOffsetDeciC(nextOffset);
    dataService.update(now, true);
    pendingRfReport = true;
  } else if (ui.menuInSubmenu) {
    moveMenuSub(direction);
  } else {
    moveMenuMain(direction);
  }
  refreshMenuFast();
  return true;
}

bool applyInputEvent(SondeInputEvent event) {
  if (ui.page != UI_PAGE_HOME) {
    return false;
  }

  if (event != SONDE_INPUT_PLUS && event != SONDE_INPUT_MINUS) {
    return false;
  }

  ui.motionDetected = true;
  markPresenceForReport();
  dataService.pauseUntil(millis() + SENSOR_PAUSE_AFTER_INPUT_MS);
  armSetpointEditTimeoutAfterRefresh = true;

  if (!ui.setpointEditing) {
    ui.setpointEditing = true;
    setpointEditEntered = true;
    return true;
  }

  int16_t nextSetpoint = ui.setpointDeciC;
  const int8_t deltaStep = event == SONDE_INPUT_PLUS ? 1 : -1;
  nextSetpoint += deltaStep * SETPOINT_STEP_DECI_C;
  if (nextSetpoint < SETPOINT_MIN_DECI_C) {
    nextSetpoint = SETPOINT_MIN_DECI_C;
  }
  if (nextSetpoint > SETPOINT_MAX_DECI_C) {
    nextSetpoint = SETPOINT_MAX_DECI_C;
  }
  if (nextSetpoint != ui.setpointDeciC) {
    ui.setpointDeciC = nextSetpoint;
    int16_t nextDelta = pendingUserDeltaSteps + deltaStep;
    if (nextDelta < -8) {
      nextDelta = -8;
    }
    if (nextDelta > 8) {
      nextDelta = 8;
    }
    pendingUserDeltaSteps = nextDelta;
    pendingRfReport = true;
  }
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

  if (inputService.centerPressed()) {
    clearLocalEeprom();
  }

  uint16_t nodeId = RF_DEFAULT_NODE_ID;
  if (!ThermioRfIds::load(EEPROM_NODE_ID, nodeId)) {
    waitForNodeIdCreation();
  } else {
    link.setLocalId(nodeId);
  }

  uint16_t consoleId = ThermioRfFrame::BroadcastId;
  if (ThermioRfIds::load(EEPROM_CONSOLE_ID, consoleId, link.localId())) {
    link.setConsoleId(consoleId);
  }

  ThermioSlavePower::setupWatchdog8s();
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
  randomSeed(analogRead(PIN_BAT_SENS) ^ micros());
  enterBatteryTerminalMode(millis());
  ui.motionDetected = inputService.motionDetected();
  lastMotionDetectedForReport = ui.motionDetected;
  dataService.update(millis(), true);
  pendingRfReport = true;
  autoReportEnabledAtWatchdogTick =
      awakeWatchdogTicks + RF_STARTUP_AUTO_REPORT_DELAY_WATCHDOG_TICKS;
  updateDisplay();
}

void loop() {
  if (FORCE_SCREEN_TEST_PAGE != UI_PAGE_HOME || startupTerminalMode) {
    sleepWhenIdle();
    return;
  }

  const uint32_t now = millis();
  if (batteryTerminalMode) {
    sleepWhenIdle();
    return;
  }

  const uint16_t watchdogTicks = ThermioSlavePower::consumeWatchdogTicks();
  awakeWatchdogTicks += watchdogTicks;
  ThermioSlavePower::consumePinWake();

  const bool forceTemperatureRefresh = consumeTemperatureRefreshWake(watchdogTicks);
  bool displayNeedsRefresh = dataService.update(now, forceTemperatureRefresh);
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
  capturePresenceForReport(inputService.motionDetected());
  if (updateCenterButton(now)) {
    return;
  }

  ui.motionDetected = inputService.motionDetected();
  if (handleMenuInput(inputEvent, now)) {
    return;
  }

  if (ui.page == UI_PAGE_MENU) {
    if ((uint32_t)(now - menuLastInteractionAt) >= MENU_IDLE_TIMEOUT_MS) {
      exitMenu();
    }
    return;
  }

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
  const bool autoReportDue = link.autoReportDue(
      awakeWatchdogTicks,
      autoReportEnabledAtWatchdogTick,
      rfReportIntervalWatchdogTicks);
  if (pendingRfReport || autoReportDue) {
    pendingRfReport = false;
    link.markReportAttemptStarted(awakeWatchdogTicks);
    if (runRfExchange()) {
      updateDisplayPartial(FULL_PARTIAL_REFRESH_X,
                           FULL_PARTIAL_REFRESH_Y,
                           FULL_PARTIAL_REFRESH_W,
                           FULL_PARTIAL_REFRESH_H);
    }
  }
  if (ui.page == UI_PAGE_HOME && !ui.setpointEditing && !inputService.centerPressed()) {
    sleepWhenIdle();
  }
}
