/*
  Thermonuino - console

  Premier sketch de production console :
    - ID console en EEPROM interne ;
    - reception RF CC1101 permanente ;
    - association des esclaves, 4 zones de chauffage ;
    - table courte EEPROM interne : 2 esclaves par zone ;
    - ACK applicatif commun vers les esclaves.

  La logique chauffage/metier sera branchee ensuite.
*/

#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>
#include <SPI.h>
#include <TimeLib.h>
#include <Wire.h>
#include <stdlib.h>

#include <ThermioRfCc1101.h>
#include <ThermioRfFrame.h>
#include <ThermioRfIds.h>
#include <ThermioHeatingRegulator.h>

constexpr uint8_t PIN_LED_CHAIN_DATA = A3;
constexpr uint8_t LED_COUNT = 6;
constexpr uint8_t LED_SALON = 0;
constexpr uint8_t LED_CHAMBRE = 1;
constexpr uint8_t LED_BUREAU = 2;
constexpr uint8_t LED_SDB = 3;
constexpr uint8_t LED_CENTRE = 4;
constexpr uint8_t LED_MODE = 5;

constexpr uint8_t PIN_CC1101_CSN = 2;
constexpr uint8_t PIN_CC1101_GDO0 = A2;
constexpr uint8_t PIN_CC1101_MOSI = 11;
constexpr uint8_t PIN_CC1101_MISO = 12;
constexpr uint8_t PIN_CC1101_SCK = 13;

constexpr uint8_t PIN_MODE_DOUCHE = A0;
constexpr uint8_t PIN_MODE_STOP = A1;
constexpr uint8_t PIN_MODE_PLUS = 10;
constexpr uint8_t PIN_MODE_NORMAL = 9;
constexpr uint8_t PIN_MODE_MOINS = 8;
constexpr uint8_t PIN_MODE_VAC = 7;

constexpr uint16_t RF_DEFAULT_CONSOLE_ID = 0x0C01;
constexpr uint8_t PILOTE_ZONE_COUNT = 4;
constexpr uint8_t RF_ASSOC_ZONE_COUNT = PILOTE_ZONE_COUNT;
constexpr uint8_t RF_ASSOC_SLAVES_PER_ZONE = 2;
constexpr uint8_t RF_MAX_ASSOCIATED_SLAVES = RF_ASSOC_ZONE_COUNT * RF_ASSOC_SLAVES_PER_ZONE;
constexpr uint8_t RF_ASSOC_ENTRY_LEN = 4;
constexpr uint32_t RF_ASSOCIATION_WINDOW_MS = 180000;
constexpr uint32_t RF_ASSOCIATION_SAVE_DELAY_MS = 10000;
constexpr uint32_t RF_RX_REFRESH_INTERVAL_MS = 500;
constexpr uint32_t RF_TX_COMPLETE_TIMEOUT_MS = 350;
constexpr uint32_t RF_ACK_REPLY_DELAY_MS = 300;
constexpr uint8_t RF_ACK_TX_COUNT = 3;
constexpr uint16_t RF_ACK_TX_GAP_MS = 150;
constexpr uint8_t PILOTE_DEFAULT_CYCLE_MINUTES = 30;
constexpr unsigned long PILOTE_SERIAL_BAUD = 9600;
constexpr uint16_t MODE_DEBOUNCE_MS = 35;
constexpr uint16_t PLUS_MINUS_NORMAL_RETURN_MS = 5000;
constexpr uint32_t DOUCHE_DURATION_MS = 30UL * 60UL * 1000UL;
constexpr uint8_t ZONE_SDB = 4;
constexpr int16_t SETPOINT_NORMAL_DECI_C = 190;
constexpr int16_t SETPOINT_VACANCE_DECI_C = 170;
constexpr int16_t FALLBACK_MEASURED_TEMP_DECI_C = 180;
constexpr uint16_t FALLBACK_ZONE_POWER_VA = 1500;
constexpr uint16_t SONDE_LOW_BATTERY_MV = 2000;
constexpr uint16_t DOOR_LOW_BATTERY_MV = 1000;
constexpr uint16_t BATTERY_NO_BATTERY_MV = 50;
constexpr int8_t MODE_DELTA_STEP_C = 1;
constexpr int8_t DOUCHE_SDB_DELTA_C = 2;
constexpr int8_t DOUCHE_OTHER_DELTA_C = -1;
constexpr uint32_t HEAT_LAST_HOUR_MS = 60UL * 60UL * 1000UL;
constexpr uint32_t HEAT_LAST_DAY_MS = 24UL * 60UL * 60UL * 1000UL;
constexpr uint32_t DEVICE_MISSING_TIMEOUT_MS = 2UL * 60UL * 60UL * 1000UL;
constexpr uint16_t USER_DELTA_FEEDBACK_RAMP_UP_MS = 500;
constexpr uint16_t USER_DELTA_FEEDBACK_TOTAL_MS = 3000;
constexpr uint8_t LED_BRIGHTNESS_MIN = 3;
constexpr uint8_t LED_BRIGHTNESS_MAX = 18;
constexpr uint16_t LED_BRIGHTNESS_MORNING_RAMP_START_MIN = 7 * 60;
constexpr uint16_t LED_BRIGHTNESS_MAX_START_MIN = 9 * 60;
constexpr uint16_t LED_BRIGHTNESS_EVENING_RAMP_START_MIN = 19 * 60;
constexpr uint16_t LED_BRIGHTNESS_MIN_START_MIN = 21 * 60;
constexpr uint32_t CLOCK_RESYNC_INTERVAL_MS = 12UL * 60UL * 60UL * 1000UL;
constexpr uint8_t CLOCK_FORCED_RESYNC_HOUR = 3;
constexpr uint8_t AHT_ADDR = 0x38;
constexpr uint32_t CONSOLE_TEMP_REFRESH_MS = 60000UL;
constexpr uint32_t PILOTE_BOOT_TIMEOUT_MS = 8000UL;
constexpr uint32_t CENTER_BOOT_OK_MS = 3000UL;
constexpr bool DEBUG_ENABLED = true;

enum ResponseGlobalMode : uint8_t {
  RESPONSE_MODE_NORMAL = 0,
  RESPONSE_MODE_PLUS = 1,
  RESPONSE_MODE_MOINS = 2,
  RESPONSE_MODE_DOUCHE = 3,
  RESPONSE_MODE_STOP = 4,
  RESPONSE_MODE_VACANCE = 5,
};

constexpr ThermioRfIds::EepromSlot EEPROM_CONSOLE_ID = {
  0, 1, 2, 3, 0x54, 0x43
};

constexpr int EEPROM_ASSOC_MAGIC_0 = 4;
constexpr int EEPROM_ASSOC_MAGIC_1 = 5;
constexpr int EEPROM_ASSOC_FIRST = 6;
constexpr uint8_t EEPROM_ASSOC_MAGIC_VALUE_0 = 0x54;
constexpr uint8_t EEPROM_ASSOC_MAGIC_VALUE_1 = 0x41;
constexpr int EEPROM_AHT_OFFSET_MAGIC = EEPROM_ASSOC_FIRST + RF_MAX_ASSOCIATED_SLAVES * RF_ASSOC_ENTRY_LEN;
constexpr int EEPROM_AHT_OFFSET_VALUE = EEPROM_AHT_OFFSET_MAGIC + 1;
constexpr uint8_t EEPROM_AHT_OFFSET_MAGIC_VALUE = 0xA7;
constexpr int EEPROM_LEARNING_MAGIC = EEPROM_AHT_OFFSET_VALUE + 1;
constexpr int EEPROM_LEARNING_MASK = EEPROM_LEARNING_MAGIC + 1;
constexpr uint8_t EEPROM_LEARNING_MAGIC_VALUE = 0x4C;

struct AssociatedSlave {
  uint16_t nodeId;
  uint8_t deviceType;
  uint8_t zone;
};

enum ModeValue : uint8_t {
  MODE_NONE,
  MODE_NORMAL,
  MODE_MOINS,
  MODE_PLUS,
  MODE_VACANCES,
  MODE_STOP,
  MODE_DOUCHE,
  MODE_INVALID
};

struct ModeInput {
  uint8_t pin;
  ModeValue mode;
};

struct ZoneState {
  uint8_t workload;
  uint16_t powerVa;
  uint32_t lastHeatAt;
  uint32_t lastPresenceAt;
  uint32_t lastSondeReportAt;
  uint32_t lastDoorReportAt;
  int16_t usualSetpointDeciC;
  int16_t currentSetpointDeciC;
  int16_t measuredTempDeciC;
  int16_t sondeSetpointDeciC;
  uint16_t lastRequestedBtuPerHour;
  uint16_t lastMaintenanceBtuPerHour;
  bool hasTemperature;
  bool hasSondeSetpoint;
  bool hasRegulationDecision;
  bool learningEnabled;
  bool heatSeen;
  bool presenceSeen;
  bool sondeSeen;
  bool doorSeen;
  bool doorOpen;
  bool sondeLowBattery;
  bool doorLowBattery;
  bool sondeMissing;
  bool doorMissing;
};

const ModeInput modeInputs[] = {
  {PIN_MODE_DOUCHE, MODE_DOUCHE},
  {PIN_MODE_STOP, MODE_STOP},
  {PIN_MODE_PLUS, MODE_PLUS},
  {PIN_MODE_NORMAL, MODE_NORMAL},
  {PIN_MODE_MOINS, MODE_MOINS},
  {PIN_MODE_VAC, MODE_VACANCES},
};

ThermioRfCc1101::Pins rfPins = {
  PIN_CC1101_CSN,
  PIN_CC1101_GDO0,
  PIN_CC1101_MOSI,
  PIN_CC1101_MISO,
  PIN_CC1101_SCK
};

const SPISettings RF_SPI_SETTINGS(1000000, MSBFIRST, SPI_MODE0);

Adafruit_NeoPixel leds(LED_COUNT, PIN_LED_CHAIN_DATA, NEO_GRB + NEO_KHZ800);
ThermioRfCc1101 radio(rfPins, RF_SPI_SETTINGS);
ThermioHeatingRegulator heatingRegulator;
AssociatedSlave associatedSlaves[RF_MAX_ASSOCIATED_SLAVES] = {};

uint16_t consoleId = RF_DEFAULT_CONSOLE_ID;
uint8_t rfSequence = 0;
uint32_t lastRfRxRefreshAt = 0;
bool associationActive = false;
uint16_t associationNodeId = ThermioRfFrame::BroadcastId;
uint8_t associationDeviceType = 0;
uint8_t associationZone = 1;
uint32_t associationSaveAt = 0;
bool associationConfirmActive = false;
uint8_t associationConfirmZone = 1;
uint32_t associationConfirmUntil = 0;
bool hasLastReportSequence = false;
uint16_t lastReportSourceId = ThermioRfFrame::BroadcastId;
uint8_t lastReportSequence = 0;
uint16_t lastPacketSourceId = ThermioRfFrame::BroadcastId;
ThermioRfFrame::Report lastReport;
bool rfBlinkActive = false;
uint8_t rfBlinkStep = 0;
uint8_t rfBlinkZone = 1;
uint32_t rfBlinkColor = 0;
uint32_t nextRfBlinkAt = 0;
bool userDeltaFeedbackActive = false;
uint8_t userDeltaFeedbackZone = 1;
bool userDeltaFeedbackWarm = true;
uint32_t userDeltaFeedbackStartedAt = 0;
ModeValue stableMode = MODE_NONE;
ModeValue lastRawMode = MODE_NONE;
ModeValue lastModeBeforeNormal = MODE_NORMAL;
unsigned long modeChangedAt = 0;
uint32_t enteredNormalAt = 0;
uint32_t doucheUntil = 0;
bool doucheWasActive = false;
int8_t plusMinusOffsetC = 0;
int8_t ahtOffsetDeciC = 0;
int16_t consoleTempDeciC = FALLBACK_MEASURED_TEMP_DECI_C;
bool consoleTempKnown = false;
uint32_t nextConsoleTempRefreshAt = 0;
bool clockSet = false;
uint32_t lastClockSyncAt = 0;
uint16_t lastForcedClockSyncDayKey = 0;
ZoneState zones[PILOTE_ZONE_COUNT] = {};
char piloteLine[96];
uint8_t piloteLineLen = 0;
bool piloteSerialOk = false;
bool rfOk = false;
bool consoleHalted = false;
bool consoleHaltBlink = false;
uint32_t centerBootOkUntil = 0;
bool debugForcedSetpointActive[PILOTE_ZONE_COUNT] = {false, false, false, false};
int16_t debugForcedSetpointDeciC[PILOTE_ZONE_COUNT] = {
  SETPOINT_NORMAL_DECI_C,
  SETPOINT_NORMAL_DECI_C,
  SETPOINT_NORMAL_DECI_C,
  SETPOINT_NORMAL_DECI_C
};

uint32_t rgb(uint8_t red, uint8_t green, uint8_t blue) {
  return leds.Color(red, green, blue);
}

void setPixel(uint8_t index, uint32_t color) {
  leds.setPixelColor(index, color);
}

void clearAllLeds() {
  for (uint8_t i = 0; i < LED_COUNT; i++) {
    setPixel(i, rgb(0, 0, 0));
  }
}

uint8_t ledForZone(uint8_t zone) {
  return zone >= 1 && zone <= PILOTE_ZONE_COUNT ? zone - 1 : LED_CENTRE;
}

uint8_t ledBrightnessForMinuteOfDay(uint16_t minuteOfDay) {
  if (minuteOfDay < LED_BRIGHTNESS_MORNING_RAMP_START_MIN ||
      minuteOfDay >= LED_BRIGHTNESS_MIN_START_MIN) {
    return LED_BRIGHTNESS_MIN;
  }
  if (minuteOfDay < LED_BRIGHTNESS_MAX_START_MIN) {
    const uint16_t rampElapsed = minuteOfDay - LED_BRIGHTNESS_MORNING_RAMP_START_MIN;
    const uint16_t rampDuration = LED_BRIGHTNESS_MAX_START_MIN - LED_BRIGHTNESS_MORNING_RAMP_START_MIN;
    return LED_BRIGHTNESS_MIN +
        (uint32_t)rampElapsed * (LED_BRIGHTNESS_MAX - LED_BRIGHTNESS_MIN) / rampDuration;
  }
  if (minuteOfDay < LED_BRIGHTNESS_EVENING_RAMP_START_MIN) {
    return LED_BRIGHTNESS_MAX;
  }
  if (minuteOfDay < LED_BRIGHTNESS_MIN_START_MIN) {
    const uint16_t rampElapsed = minuteOfDay - LED_BRIGHTNESS_EVENING_RAMP_START_MIN;
    const uint16_t rampDuration = LED_BRIGHTNESS_MIN_START_MIN - LED_BRIGHTNESS_EVENING_RAMP_START_MIN;
    return LED_BRIGHTNESS_MAX -
        (uint32_t)rampElapsed * (LED_BRIGHTNESS_MAX - LED_BRIGHTNESS_MIN) / rampDuration;
  }
  return LED_BRIGHTNESS_MIN;
}

uint8_t currentLedBrightness() {
  if (!clockSet) {
    return LED_BRIGHTNESS_MIN;
  }
  return ledBrightnessForMinuteOfDay((uint16_t)hour() * 60 + minute());
}

void applyCurrentLedBrightness() {
  leds.setBrightness(currentLedBrightness());
}

const __FlashStringHelper *modeName(ModeValue mode) {
  switch (mode) {
    case MODE_NORMAL:
      return F("NORMAL");
    case MODE_MOINS:
      return F("MOINS");
    case MODE_PLUS:
      return F("PLUS");
    case MODE_VACANCES:
      return F("VACANCES");
    case MODE_STOP:
      return F("STOP");
    case MODE_DOUCHE:
      return F("DOUCHE");
    case MODE_INVALID:
      return F("INVALID");
    case MODE_NONE:
    default:
      return F("NONE");
  }
}

const __FlashStringHelper *deviceTypeName(uint8_t deviceType) {
  if (deviceType == ThermioRfFrame::DeviceSonde) {
    return F("SONDE");
  }
  if (deviceType == ThermioRfFrame::DeviceDoor) {
    return F("DOOR");
  }
  return F("UNKNOWN");
}

void debugPrefix() {
  if (!DEBUG_ENABLED) {
    return;
  }
  Serial.print(F("CON> #DBG "));
}

void debugLine(const __FlashStringHelper *message) {
  if (!DEBUG_ENABLED) {
    return;
  }
  debugPrefix();
  Serial.println(message);
}

void debugPrintDeciC(int16_t value) {
  if (value < 0) {
    Serial.print('-');
    value = -value;
  }
  Serial.print(value / 10);
  Serial.print('.');
  Serial.print(value % 10);
}

void debugPrintZoneState(uint8_t zone) {
  if (!DEBUG_ENABLED) {
    return;
  }
  const ZoneState *state = zoneStateConst(zone);
  if (state == nullptr) {
    return;
  }
  debugPrefix();
  Serial.print(F("ZONE Z"));
  Serial.print(zone);
  Serial.print(F(" TEMP="));
  if (state->hasTemperature) {
    debugPrintDeciC(state->measuredTempDeciC);
    Serial.print(F("(SONDE)"));
  } else if (consoleTempKnown) {
    debugPrintDeciC(consoleTempDeciC);
    Serial.print(F("(CONSOLE)"));
  } else {
    debugPrintDeciC(FALLBACK_MEASURED_TEMP_DECI_C);
    Serial.print(F("(FALLBACK)"));
  }
  Serial.print(F(" USUAL="));
  debugPrintDeciC(state->usualSetpointDeciC);
  Serial.print(F(" CURRENT="));
  debugPrintDeciC(state->currentSetpointDeciC);
  if (debugForcedSetpointActive[zone - 1]) {
    Serial.print(F("(FORCED)"));
  }
  Serial.print(F(" SONDE_SET="));
  if (state->hasSondeSetpoint) {
    debugPrintDeciC(state->sondeSetpointDeciC);
  } else {
    Serial.print(F("NA"));
  }
  Serial.print(F(" WORKLOAD="));
  Serial.print(state->workload);
  Serial.print(F("/255 POWER="));
  Serial.print(installedPowerForZone(zone));
  Serial.print(F("VA HOLD="));
  Serial.print(heatingRegulator.learnedHoldBtuPerHour(zone - 1));
  Serial.print(F("BTU/H CONF="));
  Serial.print(heatingRegulator.holdConfidence(zone - 1));
  Serial.print(F(" RESP="));
  Serial.print(heatingRegulator.learnedResponseBtuPerC());
  Serial.print(F("BTU/C DOOR="));
  Serial.print(state->doorOpen ? F("OPEN") : F("CLOSED"));
  Serial.print(F(" PRESENCE="));
  Serial.print(state->presenceSeen ? F("YES") : F("NO"));
  Serial.print(F(" SONDE_BAT="));
  Serial.print(state->sondeLowBattery ? F("LOW") : F("OK"));
  Serial.print(F(" DOOR_BAT="));
  Serial.print(state->doorLowBattery ? F("LOW") : F("OK"));
  Serial.println();
}

void debugPrintAssociations() {
  if (!DEBUG_ENABLED) {
    return;
  }
  debugPrefix();
  Serial.print(F("ASSOC ACTIVE="));
  Serial.print(associationActive ? F("YES") : F("NO"));
  if (associationActive) {
    Serial.print(F(" NODE="));
    Serial.print(associationNodeId, HEX);
    Serial.print(F(" ZONE="));
    Serial.print(associationZone);
  }
  Serial.println();

  for (uint8_t i = 0; i < RF_MAX_ASSOCIATED_SLAVES; i++) {
    if (associatedSlaves[i].nodeId == ThermioRfFrame::BroadcastId) {
      continue;
    }
    debugPrefix();
    Serial.print(F("ASSOC SLOT="));
    Serial.print(i);
    Serial.print(F(" NODE="));
    Serial.print(associatedSlaves[i].nodeId, HEX);
    Serial.print(F(" TYPE="));
    Serial.print(deviceTypeName(associatedSlaves[i].deviceType));
    Serial.print(F(" ZONE="));
    Serial.println(associatedSlaves[i].zone);
  }
}

void debugPrintProgram(uint8_t zone) {
  if (!DEBUG_ENABLED) {
    return;
  }
  const ZoneState *state = zoneStateConst(zone);
  if (state == nullptr) {
    return;
  }
  debugPrefix();
  Serial.print(F("PROG Z"));
  Serial.print(zone);
  Serial.print(F(" LEARNING="));
  Serial.print(state->learningEnabled ? F("ON") : F("OFF"));
  Serial.print(F(" USUAL="));
  debugPrintDeciC(state->usualSetpointDeciC);
  Serial.print(F(" CURRENT="));
  debugPrintDeciC(state->currentSetpointDeciC);
  Serial.print(F(" MODE="));
  Serial.print(modeName(stableMode));
  Serial.print(F(" PLUS_MINUS="));
  Serial.print(plusMinusOffsetC);
  Serial.print(F(" DOUCHE="));
  Serial.print(doucheActive() ? F("ON") : F("OFF"));
  Serial.println();
}

void debugPrintOverview() {
  if (!DEBUG_ENABLED) {
    return;
  }
  debugPrefix();
  Serial.print(F("OVERVIEW ID="));
  Serial.print(consoleId, HEX);
  Serial.print(F(" MODE="));
  Serial.print(modeName(stableMode));
  Serial.print(F(" CLOCK="));
  Serial.print(clockSet ? F("OK") : F("NA"));
  Serial.print(F(" AHT_OFFSET="));
  Serial.print(ahtOffsetDeciC);
  Serial.print(F(" RFSEQ="));
  Serial.println(rfSequence);
  for (uint8_t zone = 1; zone <= PILOTE_ZONE_COUNT; zone++) {
    debugPrintZoneState(zone);
  }
}

void debugPrintHelp() {
  debugLine(F("COMMANDS"));
  debugLine(F("  DBG / DBG?"));
  debugLine(F("  DBG Z1 .. DBG Z4"));
  debugLine(F("  DBG PROG Z1 .. DBG PROG Z4"));
  debugLine(F("  DBG ASSOC"));
  debugLine(F("  DBG SET 28        force toutes les zones a 28.0 C"));
  debugLine(F("  DBG SET Z1 27.5   force Z1 a 27.5 C"));
  debugLine(F("  DBG SET OFF       annule tous les forcages"));
  debugLine(F("  DBG SET Z1 OFF    annule le forcage Z1"));
  debugLine(F("  DBG TIME?"));
  debugLine(F("  DBG TIME yyyy-mm-dd hh:mm:ss"));
  debugLine(F("  DBG HELP"));
}

bool zoneIsValid(uint8_t zone) {
  return zone >= 1 && zone <= PILOTE_ZONE_COUNT;
}

ZoneState *zoneState(uint8_t zone) {
  return zoneIsValid(zone) ? &zones[zone - 1] : nullptr;
}

const ZoneState *zoneStateConst(uint8_t zone) {
  return zoneIsValid(zone) ? &zones[zone - 1] : nullptr;
}

void showLeds() {
  applyCurrentLedBrightness();
  leds.show();
}

uint8_t workloadForZone(uint8_t zone) {
  const ZoneState *state = zoneStateConst(zone);
  if (state == nullptr) {
    return 0;
  }
  return state->doorOpen ? 0 : state->workload;
}

bool doorOpenForZone(uint8_t zone) {
  const ZoneState *state = zoneStateConst(zone);
  if (state == nullptr) {
    return false;
  }
  return state->doorOpen;
}

void updateHeatingHistory(uint32_t now) {
  for (uint8_t i = 0; i < PILOTE_ZONE_COUNT; i++) {
    if (workloadForZone(i + 1) > 0) {
      zones[i].lastHeatAt = now;
      zones[i].heatSeen = true;
    }
  }
}

bool heatSeenWithin(uint8_t zone, uint32_t now, uint32_t windowMs) {
  const ZoneState *state = zoneStateConst(zone);
  if (state == nullptr) {
    return false;
  }
  return state->heatSeen && (uint32_t)(now - state->lastHeatAt) <= windowMs;
}

bool presenceSeenWithin(uint8_t zone, uint32_t now, uint32_t windowMs) {
  const ZoneState *state = zoneStateConst(zone);
  if (state == nullptr) {
    return false;
  }
  return state->presenceSeen && (uint32_t)(now - state->lastPresenceAt) <= windowMs;
}

void updateMissingDeviceStates(uint32_t now) {
  for (uint8_t i = 0; i < PILOTE_ZONE_COUNT; i++) {
    zones[i].sondeMissing = zones[i].sondeSeen &&
        (uint32_t)(now - zones[i].lastSondeReportAt) > DEVICE_MISSING_TIMEOUT_MS;
    zones[i].doorMissing = zones[i].doorSeen &&
        (uint32_t)(now - zones[i].lastDoorReportAt) > DEVICE_MISSING_TIMEOUT_MS;
  }
}

bool lowBatteryForType(uint8_t deviceType, uint16_t batteryMv) {
  if (batteryMv <= BATTERY_NO_BATTERY_MV) {
    return false;
  }
  if (deviceType == ThermioRfFrame::DeviceDoor) {
    return batteryMv <= DOOR_LOW_BATTERY_MV;
  }
  if (deviceType == ThermioRfFrame::DeviceSonde) {
    return batteryMv <= SONDE_LOW_BATTERY_MV;
  }
  return false;
}

void saveAhtOffsetToEeprom() {
  EEPROM.update(EEPROM_AHT_OFFSET_MAGIC, EEPROM_AHT_OFFSET_MAGIC_VALUE);
  EEPROM.update(EEPROM_AHT_OFFSET_VALUE, (uint8_t)ahtOffsetDeciC);
}

void loadAhtOffsetFromEeprom() {
  if (EEPROM.read(EEPROM_AHT_OFFSET_MAGIC) != EEPROM_AHT_OFFSET_MAGIC_VALUE) {
    ahtOffsetDeciC = 0;
    saveAhtOffsetToEeprom();
    return;
  }

  const int8_t stored = (int8_t)EEPROM.read(EEPROM_AHT_OFFSET_VALUE);
  ahtOffsetDeciC =
      (stored >= ThermioRfFrame::MinAhtOffsetDeciC &&
       stored <= ThermioRfFrame::MaxAhtOffsetDeciC) ? stored : 0;
}

void updateAhtOffsetFromReport(const ThermioRfFrame::Report &report) {
  if (!report.hasAhtOffset) {
    return;
  }
  if (report.ahtOffsetDeciC < ThermioRfFrame::MinAhtOffsetDeciC ||
      report.ahtOffsetDeciC > ThermioRfFrame::MaxAhtOffsetDeciC ||
      report.ahtOffsetDeciC == ahtOffsetDeciC) {
    return;
  }

  ahtOffsetDeciC = report.ahtOffsetDeciC;
  saveAhtOffsetToEeprom();
}

uint8_t learningEnabledMask() {
  uint8_t mask = 0;
  for (uint8_t i = 0; i < PILOTE_ZONE_COUNT; i++) {
    if (zones[i].learningEnabled) {
      mask |= _BV(i);
    }
  }
  return mask;
}

void saveLearningEnabledToEeprom() {
  EEPROM.update(EEPROM_LEARNING_MAGIC, EEPROM_LEARNING_MAGIC_VALUE);
  EEPROM.update(EEPROM_LEARNING_MASK, learningEnabledMask());
}

void loadLearningEnabledFromEeprom() {
  if (EEPROM.read(EEPROM_LEARNING_MAGIC) != EEPROM_LEARNING_MAGIC_VALUE) {
    for (uint8_t i = 0; i < PILOTE_ZONE_COUNT; i++) {
      zones[i].learningEnabled = true;
    }
    saveLearningEnabledToEeprom();
    return;
  }

  const uint8_t mask = EEPROM.read(EEPROM_LEARNING_MASK);
  for (uint8_t i = 0; i < PILOTE_ZONE_COUNT; i++) {
    zones[i].learningEnabled = (mask & _BV(i)) != 0;
  }
}

bool readAhtStatus(uint8_t &status) {
  Wire.requestFrom(AHT_ADDR, (uint8_t)1);
  if (Wire.available() != 1) {
    return false;
  }
  status = Wire.read();
  return true;
}

bool initAht() {
  Wire.beginTransmission(AHT_ADDR);
  Wire.write(0xBE);
  Wire.write(0x08);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) {
    return false;
  }
  delay(10);
  uint8_t status = 0;
  return readAhtStatus(status);
}

bool readAhtTemperatureDeciC(int16_t &temperatureDeciC) {
  uint8_t status = 0;
  if (!readAhtStatus(status)) {
    return false;
  }
  if ((status & 0x08) == 0 && !initAht()) {
    return false;
  }

  Wire.beginTransmission(AHT_ADDR);
  Wire.write(0xAC);
  Wire.write(0x33);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) {
    return false;
  }

  delay(80);
  uint8_t data[6] = {0};
  Wire.requestFrom(AHT_ADDR, (uint8_t)6);
  for (uint8_t i = 0; i < sizeof(data); i++) {
    if (!Wire.available()) {
      return false;
    }
    data[i] = Wire.read();
  }
  if ((data[0] & 0x80) != 0) {
    return false;
  }

  const uint32_t rawTemp =
      (((uint32_t)data[3] & 0x0F) << 16) |
      ((uint32_t)data[4] << 8) |
      data[5];
  temperatureDeciC = (int16_t)((rawTemp * 2000UL + 524288UL) / 1048576UL) - 500;
  return true;
}

void updateConsoleTemperature(uint32_t now, bool force) {
  if (!force && (int32_t)(now - nextConsoleTempRefreshAt) < 0) {
    return;
  }
  nextConsoleTempRefreshAt = now + CONSOLE_TEMP_REFRESH_MS;

  int16_t measured = consoleTempDeciC;
  const bool ok = readAhtTemperatureDeciC(measured);
  consoleTempKnown = ok;
  if (ok) {
    consoleTempDeciC = measured + ahtOffsetDeciC;
  }
}

bool doucheActive() {
  return stableMode == MODE_DOUCHE && (int32_t)(millis() - doucheUntil) < 0;
}

int16_t measuredTempForZone(uint8_t zone) {
  const ZoneState *state = zoneStateConst(zone);
  if (state == nullptr) {
    return consoleTempKnown ? consoleTempDeciC : FALLBACK_MEASURED_TEMP_DECI_C;
  }
  if (state->hasTemperature) {
    return state->measuredTempDeciC;
  }
  return consoleTempKnown ? consoleTempDeciC : FALLBACK_MEASURED_TEMP_DECI_C;
}

uint16_t installedPowerForZone(uint8_t zone) {
  const ZoneState *state = zoneStateConst(zone);
  if (state == nullptr) {
    return FALLBACK_ZONE_POWER_VA;
  }
  return state->powerVa > 0 ? state->powerVa : FALLBACK_ZONE_POWER_VA;
}

uint8_t responseModeValue() {
  switch (stableMode) {
    case MODE_PLUS:
      return RESPONSE_MODE_PLUS;
    case MODE_MOINS:
      return RESPONSE_MODE_MOINS;
    case MODE_DOUCHE:
      return RESPONSE_MODE_DOUCHE;
    case MODE_STOP:
      return RESPONSE_MODE_STOP;
    case MODE_VACANCES:
      return RESPONSE_MODE_VACANCE;
    case MODE_NORMAL:
    default:
      return RESPONSE_MODE_NORMAL;
  }
}

int16_t usualSetpointForZone(uint8_t zone) {
  const ZoneState *state = zoneStateConst(zone);
  return state != nullptr ? state->usualSetpointDeciC : 0;
}

int16_t currentSetpointForZone(uint8_t zone) {
  const ZoneState *state = zoneStateConst(zone);
  return state != nullptr ? state->currentSetpointDeciC : 0;
}

int16_t computeCurrentSetpointForZone(uint8_t zone) {
  ZoneState *state = zoneState(zone);
  if (state == nullptr) {
    return 0;
  }
  if (stableMode == MODE_STOP) {
    return 0;
  }
  if (debugForcedSetpointActive[zone - 1]) {
    return debugForcedSetpointDeciC[zone - 1];
  }
  if (stableMode == MODE_VACANCES) {
    return SETPOINT_VACANCE_DECI_C;
  }
  if (!state->learningEnabled && state->hasSondeSetpoint) {
    return state->sondeSetpointDeciC;
  }
  int16_t setpoint = state->usualSetpointDeciC;
  if (stableMode == MODE_PLUS || stableMode == MODE_MOINS) {
    setpoint += (int16_t)plusMinusOffsetC * 10;
  } else if (doucheActive()) {
    setpoint += (zone == ZONE_SDB ? DOUCHE_SDB_DELTA_C : DOUCHE_OTHER_DELTA_C) * 10;
  }
  return setpoint;
}

void refreshZoneSetpoints() {
  for (uint8_t i = 0; i < PILOTE_ZONE_COUNT; i++) {
    zones[i].usualSetpointDeciC = SETPOINT_NORMAL_DECI_C;
    zones[i].currentSetpointDeciC = computeCurrentSetpointForZone(i + 1);
  }
}

void recomputeZoneWorkload(uint8_t zone) {
  ZoneState *state = zoneState(zone);
  if (state == nullptr) {
    return;
  }

  const ThermioHeatingRegulator::Decision decision = heatingRegulator.decide(
      zone - 1,
      measuredTempForZone(zone),
      currentSetpointForZone(zone),
      installedPowerForZone(zone),
      doorOpenForZone(zone),
      stableMode == MODE_STOP);

  state->workload = (uint8_t)decision.workload;
  state->lastRequestedBtuPerHour = decision.requestedBtuPerHour;
  state->lastMaintenanceBtuPerHour = decision.maintenanceBtuPerHour;
  state->hasRegulationDecision = true;
}

void recomputeZoneWorkloads() {
  refreshZoneSetpoints();
  for (uint8_t i = 0; i < PILOTE_ZONE_COUNT; i++) {
    recomputeZoneWorkload(i + 1);
  }
}

uint16_t generateConsoleId() {
  return ThermioRfIds::generate(PIN_MODE_DOUCHE, RF_DEFAULT_CONSOLE_ID);
}

void loadOrCreateConsoleId() {
  if (!ThermioRfIds::load(EEPROM_CONSOLE_ID, consoleId)) {
    consoleId = generateConsoleId();
    ThermioRfIds::save(EEPROM_CONSOLE_ID, consoleId);
  }
}

void initializeZoneStates() {
  for (uint8_t i = 0; i < PILOTE_ZONE_COUNT; i++) {
    zones[i].workload = 0;
    zones[i].powerVa = FALLBACK_ZONE_POWER_VA;
    zones[i].lastHeatAt = 0;
    zones[i].lastPresenceAt = 0;
    zones[i].lastSondeReportAt = 0;
    zones[i].lastDoorReportAt = 0;
    zones[i].usualSetpointDeciC = SETPOINT_NORMAL_DECI_C;
    zones[i].currentSetpointDeciC = SETPOINT_NORMAL_DECI_C;
    zones[i].measuredTempDeciC = FALLBACK_MEASURED_TEMP_DECI_C;
    zones[i].sondeSetpointDeciC = SETPOINT_NORMAL_DECI_C;
    zones[i].lastRequestedBtuPerHour = 0;
    zones[i].lastMaintenanceBtuPerHour = 0;
    zones[i].hasTemperature = false;
    zones[i].hasSondeSetpoint = false;
    zones[i].hasRegulationDecision = false;
    zones[i].learningEnabled = true;
    zones[i].heatSeen = false;
    zones[i].presenceSeen = false;
    zones[i].sondeSeen = false;
    zones[i].doorSeen = false;
    zones[i].doorOpen = false;
    zones[i].sondeLowBattery = false;
    zones[i].doorLowBattery = false;
    zones[i].sondeMissing = false;
    zones[i].doorMissing = false;
  }
}

int eepromAssocAddress(uint8_t index) {
  return EEPROM_ASSOC_FIRST + index * RF_ASSOC_ENTRY_LEN;
}

void clearAssociationTableRam() {
  for (uint8_t i = 0; i < RF_MAX_ASSOCIATED_SLAVES; i++) {
    associatedSlaves[i].nodeId = ThermioRfFrame::BroadcastId;
    associatedSlaves[i].deviceType = 0;
    associatedSlaves[i].zone = 0;
  }
}

void loadAssociationTableFromEeprom() {
  clearAssociationTableRam();
  const bool magicOk =
      EEPROM.read(EEPROM_ASSOC_MAGIC_0) == EEPROM_ASSOC_MAGIC_VALUE_0 &&
      EEPROM.read(EEPROM_ASSOC_MAGIC_1) == EEPROM_ASSOC_MAGIC_VALUE_1;
  if (!magicOk) {
    return;
  }

  for (uint8_t i = 0; i < RF_MAX_ASSOCIATED_SLAVES; i++) {
    const int address = eepromAssocAddress(i);
    const uint16_t nodeId =
        (uint16_t)EEPROM.read(address) |
        ((uint16_t)EEPROM.read(address + 1) << 8);
    const uint8_t deviceType = EEPROM.read(address + 2);
    const uint8_t zone = EEPROM.read(address + 3);
    if (ThermioRfIds::isValid(nodeId, consoleId) && zone >= 1 && zone <= RF_ASSOC_ZONE_COUNT) {
      associatedSlaves[i].nodeId = nodeId;
      associatedSlaves[i].deviceType = deviceType;
      associatedSlaves[i].zone = zone;
    }
  }
}

void saveAssociationEntry(uint8_t index) {
  const int address = eepromAssocAddress(index);
  EEPROM.update(address, associatedSlaves[index].nodeId & 0xFF);
  EEPROM.update(address + 1, associatedSlaves[index].nodeId >> 8);
  EEPROM.update(address + 2, associatedSlaves[index].deviceType);
  EEPROM.update(address + 3, associatedSlaves[index].zone);
  EEPROM.update(EEPROM_ASSOC_MAGIC_0, EEPROM_ASSOC_MAGIC_VALUE_0);
  EEPROM.update(EEPROM_ASSOC_MAGIC_1, EEPROM_ASSOC_MAGIC_VALUE_1);
}

void clearAssociationEntry(uint8_t index) {
  associatedSlaves[index].nodeId = ThermioRfFrame::BroadcastId;
  associatedSlaves[index].deviceType = 0;
  associatedSlaves[index].zone = 0;
  saveAssociationEntry(index);
}

int findAssociatedSlave(uint16_t nodeId) {
  for (uint8_t i = 0; i < RF_MAX_ASSOCIATED_SLAVES; i++) {
    if (associatedSlaves[i].nodeId == nodeId) {
      return i;
    }
  }
  return -1;
}

int findAssociationSlotForZone(uint8_t zone, int preferredIndex) {
  uint8_t zoneCount = 0;
  int firstZoneIndex = -1;
  for (uint8_t i = 0; i < RF_MAX_ASSOCIATED_SLAVES; i++) {
    if (associatedSlaves[i].zone == zone) {
      zoneCount++;
      if (firstZoneIndex < 0) {
        firstZoneIndex = i;
      }
    }
  }

  if (preferredIndex >= 0 &&
      (associatedSlaves[preferredIndex].zone == zone || zoneCount < RF_ASSOC_SLAVES_PER_ZONE)) {
    return preferredIndex;
  }
  if (zoneCount >= RF_ASSOC_SLAVES_PER_ZONE && firstZoneIndex >= 0) {
    return firstZoneIndex;
  }
  for (uint8_t i = 0; i < RF_MAX_ASSOCIATED_SLAVES; i++) {
    if (associatedSlaves[i].nodeId == ThermioRfFrame::BroadcastId) {
      return i;
    }
  }
  return 0;
}

uint8_t zoneForSlave(uint16_t nodeId) {
  if (associationActive && associationNodeId == nodeId) {
    return associationZone;
  }

  const int index = findAssociatedSlave(nodeId);
  return index >= 0 ? associatedSlaves[index].zone : 1;
}

void beginOrRefreshAssociation(uint16_t nodeId, uint8_t deviceType) {
  if (nodeId == ThermioRfFrame::BroadcastId || nodeId == consoleId) {
    return;
  }

  if (!associationActive || associationNodeId != nodeId) {
    associationActive = true;
    associationNodeId = nodeId;
    associationDeviceType = deviceType;
    associationZone = 1;
  }
  associationSaveAt = millis() + RF_ASSOCIATION_SAVE_DELAY_MS;
}

void savePendingAssociationIfDue() {
  if (!associationActive || (int32_t)(millis() - associationSaveAt) < 0) {
    return;
  }

  const int preferredIndex = findAssociatedSlave(associationNodeId);
  const int index = findAssociationSlotForZone(associationZone, preferredIndex);
  associatedSlaves[index].nodeId = associationNodeId;
  associatedSlaves[index].deviceType = associationDeviceType;
  associatedSlaves[index].zone = associationZone;
  saveAssociationEntry(index);

  for (uint8_t i = 0; i < RF_MAX_ASSOCIATED_SLAVES; i++) {
    if (i != index && associatedSlaves[i].nodeId == associationNodeId) {
      clearAssociationEntry(i);
    }
  }

  associationConfirmActive = true;
  associationConfirmZone = associationZone;
  associationConfirmUntil = millis() + 5000;
  if (DEBUG_ENABLED) {
    debugPrefix();
    Serial.print(F("ASSOC SAVED NODE="));
    Serial.print(associationNodeId, HEX);
    Serial.print(F(" TYPE="));
    Serial.print(deviceTypeName(associationDeviceType));
    Serial.print(F(" ZONE="));
    Serial.println(associationZone);
  }
  associationActive = false;
}

bool sourceAccepted(uint16_t sourceId, uint16_t targetId) {
  const bool associationWindowOpen = (uint32_t)millis() < RF_ASSOCIATION_WINDOW_MS;
  const bool sourceKnown =
      findAssociatedSlave(sourceId) >= 0 ||
      (associationActive && associationNodeId == sourceId);
  return sourceId != consoleId &&
      (sourceKnown || (targetId == ThermioRfFrame::BroadcastId && associationWindowOpen));
}

bool targetAccepted(uint16_t targetId) {
  return targetId == consoleId ||
      (targetId == ThermioRfFrame::BroadcastId && (uint32_t)millis() < RF_ASSOCIATION_WINDOW_MS);
}

bool readReport(uint8_t &sequence) {
  if (radio.rxOverflow()) {
    radio.flushRx();
    radio.strobeRx();
    return false;
  }

  const uint8_t expectedLength = ThermioRfFrame::HeaderLen + ThermioRfFrame::ReportPayloadLen;
  if (radio.rxBytes() < expectedLength + 1) {
    return false;
  }

  uint8_t packet[ThermioRfFrame::MaxPacketLen] = {0};
  const uint8_t length = radio.readPacket(packet, sizeof(packet));
  ThermioRfFrame::Header header;
  if (!ThermioRfFrame::readHeader(packet, length, header) ||
      header.frameType != ThermioRfFrame::FrameReport ||
      !sourceAccepted(header.sourceId, header.targetId) ||
      !targetAccepted(header.targetId)) {
    return false;
  }

  ThermioRfFrame::Report report;
  if (!ThermioRfFrame::decodeReport(packet, length, report, RF_ASSOC_ZONE_COUNT)) {
    return false;
  }

  lastPacketSourceId = header.sourceId;
  lastReport = report;
  sequence = header.sequence;
  return true;
}

uint8_t buildResponsePacket(uint8_t *packet, uint16_t targetId, uint8_t sequence, uint8_t ackSequence) {
  ThermioRfFrame::Header header;
  header.frameType = ThermioRfFrame::FrameResponse;
  header.sourceId = consoleId;
  header.targetId = targetId;
  header.sequence = sequence;
  header.ackSequence = ackSequence;
  header.payloadLen = ThermioRfFrame::ResponsePayloadLen;
  ThermioRfFrame::writeHeader(packet, header);

  ThermioRfFrame::Response response;
  response.assignedZone = zoneForSlave(targetId);
  response.dateTime[0] = clockSet ? (uint8_t)(year() - 2000) : 0;
  response.dateTime[1] = clockSet ? (uint8_t)month() : 0;
  response.dateTime[2] = clockSet ? (uint8_t)day() : 0;
  response.dateTime[3] = clockSet ? (uint8_t)hour() : 0;
  response.dateTime[4] = clockSet ? (uint8_t)minute() : 0;
  response.dateTime[5] = clockSet ? (uint8_t)second() : 0;
  response.globalMode = responseModeValue();
  const uint32_t now = millis();
  response.heatActive = heatSeenWithin(response.assignedZone, now, HEAT_LAST_DAY_MS);
  response.zoneDoorOpen = doorOpenForZone(response.assignedZone);
  response.usualSetpointDeciC = usualSetpointForZone(response.assignedZone);
  response.currentSetpointDeciC = currentSetpointForZone(response.assignedZone);
  response.commandFlags = heatSeenWithin(response.assignedZone, now, HEAT_LAST_HOUR_MS) ?
      ThermioRfFrame::ResponseFlagHeatLastHour : 0;
  const ZoneState *responseZone = zoneStateConst(response.assignedZone);
  if (responseZone != nullptr && !responseZone->learningEnabled) {
    response.commandFlags |= ThermioRfFrame::ResponseFlagLearningDisabled;
  }
  response.nextReportDelayS = 3600;
  response.ahtOffsetDeciC = ahtOffsetDeciC;
  ThermioRfFrame::encodeResponsePayload(packet + ThermioRfFrame::HeaderLen, response);
  return ThermioRfFrame::HeaderLen + ThermioRfFrame::ResponsePayloadLen;
}

void sendAckBurst(uint16_t targetId, uint8_t ackSequence) {
  delay(RF_ACK_REPLY_DELAY_MS);
  for (uint8_t ack = 0; ack < RF_ACK_TX_COUNT; ack++) {
    setPixel(LED_SDB, rgb(255, 110, 0));
    showLeds();
    uint8_t packet[ThermioRfFrame::MaxPacketLen] = {0};
    const uint8_t length = buildResponsePacket(packet, targetId, rfSequence++, ackSequence);
    radio.writePacket(packet, length);
    radio.waitTxComplete(RF_TX_COMPLETE_TIMEOUT_MS);
    if (ack + 1 < RF_ACK_TX_COUNT) {
      delay(RF_ACK_TX_GAP_MS);
    }
  }
  setPixel(LED_SDB, rgb(0, 255, 0));
  showLeds();
}

void startRfReceivedBlink(uint8_t zone, uint8_t deviceType) {
  rfBlinkActive = true;
  rfBlinkStep = 0;
  rfBlinkZone = zone;
  rfBlinkColor = deviceType == ThermioRfFrame::DeviceDoor ? rgb(140, 0, 255) : rgb(255, 0, 0);
  nextRfBlinkAt = 0;
}

void startUserDeltaFeedback(uint8_t zone, int8_t deltaSteps) {
  if (zone < 1 || zone > PILOTE_ZONE_COUNT || deltaSteps == 0) {
    return;
  }
  userDeltaFeedbackActive = true;
  userDeltaFeedbackZone = zone;
  userDeltaFeedbackWarm = deltaSteps > 0;
  userDeltaFeedbackStartedAt = millis();
}

uint8_t userDeltaFeedbackIntensity(uint32_t elapsedMs) {
  if (elapsedMs >= USER_DELTA_FEEDBACK_TOTAL_MS) {
    return 0;
  }
  if (elapsedMs <= USER_DELTA_FEEDBACK_RAMP_UP_MS) {
    return (uint32_t)elapsedMs * 255UL / USER_DELTA_FEEDBACK_RAMP_UP_MS;
  }

  const uint32_t fadeElapsed = elapsedMs - USER_DELTA_FEEDBACK_RAMP_UP_MS;
  const uint32_t fadeMs = USER_DELTA_FEEDBACK_TOTAL_MS - USER_DELTA_FEEDBACK_RAMP_UP_MS;
  return 255 - ((uint32_t)fadeElapsed * 255UL / fadeMs);
}

void applyUserDeltaFeedbackLed() {
  if (!userDeltaFeedbackActive) {
    return;
  }

  const uint32_t elapsed = millis() - userDeltaFeedbackStartedAt;
  if (elapsed >= USER_DELTA_FEEDBACK_TOTAL_MS) {
    userDeltaFeedbackActive = false;
    return;
  }

  const uint8_t intensity = userDeltaFeedbackIntensity(elapsed);
  const uint32_t color = userDeltaFeedbackWarm ?
      rgb(intensity, 0, 0) :
      rgb(0, 0, intensity);
  setPixel(ledForZone(userDeltaFeedbackZone), color);
}

void updateRfReceivedBlink() {
  if (!rfBlinkActive || (int32_t)(millis() - nextRfBlinkAt) < 0) {
    return;
  }
  if (rfBlinkStep >= 6) {
    rfBlinkActive = false;
    clearAllLeds();
    return;
  }

  const bool ledOn = (rfBlinkStep % 2) == 0;
  clearAllLeds();
  setPixel(ledForZone(rfBlinkZone), ledOn ? rfBlinkColor : rgb(0, 0, 0));
  rfBlinkStep++;
  nextRfBlinkAt = millis() + 100;
}

void updateAssociationLeds() {
  if (associationConfirmActive) {
    if ((int32_t)(millis() - associationConfirmUntil) >= 0) {
      associationConfirmActive = false;
      clearAllLeds();
      return;
    }

    clearAllLeds();
    setPixel(ledForZone(associationConfirmZone), rgb(255, 0, 120));
    return;
  }

  if (!associationActive) {
    return;
  }

  const bool blinkOn = (millis() % 500) < 250;
  for (uint8_t zone = 1; zone <= RF_ASSOC_ZONE_COUNT; zone++) {
    setPixel(ledForZone(zone), zone == associationZone && blinkOn ? rgb(255, 0, 120) : rgb(0, 0, 0));
  }
}

uint32_t centerStatusColor(uint32_t now) {
  if (consoleHalted) {
    if (consoleHaltBlink && (now % 1000UL) >= 500UL) {
      return rgb(0, 0, 0);
    }
    return rgb(255, 0, 0);
  }
  if ((int32_t)(now - centerBootOkUntil) < 0) {
    return rgb(0, 255, 0);
  }
  if (now < RF_ASSOCIATION_WINDOW_MS) {
    return rgb(0, 80, 255);
  }
  return rgb(0, 0, 0);
}

bool waitForPiloteAtBoot() {
  const uint32_t startedAt = millis();
  while ((uint32_t)(millis() - startedAt) < PILOTE_BOOT_TIMEOUT_MS) {
    readPiloteSerial();
    if (piloteSerialOk) {
      centerBootOkUntil = millis() + CENTER_BOOT_OK_MS;
      return true;
    }
  }
  return false;
}

ModeValue readRawMode() {
  uint8_t activeCount = 0;
  ModeValue activeMode = MODE_NONE;
  for (const ModeInput &input : modeInputs) {
    if (digitalRead(input.pin) == LOW) {
      activeCount++;
      activeMode = input.mode;
    }
  }
  if (activeCount == 0) {
    return MODE_NONE;
  }
  return activeCount > 1 ? MODE_INVALID : activeMode;
}

void initializeModeSelection() {
  const ModeValue rawMode = readRawMode();
  stableMode = (rawMode != MODE_NONE && rawMode != MODE_INVALID) ? rawMode : MODE_NORMAL;
  lastRawMode = stableMode;
  lastModeBeforeNormal = MODE_NORMAL;
  modeChangedAt = millis();
  enteredNormalAt = millis();
  plusMinusOffsetC = stableMode == MODE_MOINS ? -MODE_DELTA_STEP_C :
      stableMode == MODE_PLUS ? MODE_DELTA_STEP_C : 0;
  if (stableMode == MODE_DOUCHE) {
    doucheUntil = millis() + DOUCHE_DURATION_MS;
  }
  recomputeZoneWorkloads();
  doucheWasActive = doucheActive();
}

uint32_t colorForMode(ModeValue mode) {
  if ((mode == MODE_PLUS || mode == MODE_MOINS) && plusMinusOffsetC != 0) {
    const uint8_t pulseCount = abs(plusMinusOffsetC);
    const uint16_t phase = millis() % 2200U;
    const uint16_t pulseWindow = pulseCount * 300U;
    if (phase < pulseWindow && (phase % 300U) >= 150U) {
      return rgb(0, 0, 0);
    }
  }

  switch (mode) {
    case MODE_NORMAL:
      return rgb(0, 255, 0);
    case MODE_MOINS:
      return rgb(0, 80, 255);
    case MODE_PLUS:
      return rgb(255, 110, 0);
    case MODE_VACANCES:
      return (millis() % 10000UL) < 1000UL ? rgb(0, 80, 255) : rgb(0, 0, 0);
    case MODE_STOP:
      return rgb(0, 0, 0);
    case MODE_DOUCHE:
      return (millis() % 500UL) < 250UL ? rgb(255, 110, 0) : rgb(0, 255, 0);
    case MODE_INVALID:
    case MODE_NONE:
    default:
      return rgb(255, 255, 255);
  }
}

void sendPiloteSet() {
  Serial.print(F("SET "));
  Serial.print(PILOTE_DEFAULT_CYCLE_MINUTES);
  for (uint8_t i = 0; i < PILOTE_ZONE_COUNT; i++) {
    Serial.print(' ');
    Serial.print(workloadForZone(i + 1));
  }
  Serial.println();
}

void applyModeSelection(ModeValue mode) {
  const ModeValue previousMode = stableMode;
  stableMode = mode;
  if (DEBUG_ENABLED) {
    debugPrefix();
    Serial.print(F("MODE "));
    Serial.print(modeName(previousMode));
    Serial.print(F(" -> "));
    Serial.println(modeName(mode));
  }

  if (mode == MODE_NORMAL) {
    lastModeBeforeNormal = previousMode;
    enteredNormalAt = millis();
    if (previousMode != MODE_PLUS && previousMode != MODE_MOINS) {
      plusMinusOffsetC = 0;
    }
  } else if (mode == MODE_PLUS || mode == MODE_MOINS) {
    const int8_t direction = mode == MODE_PLUS ? MODE_DELTA_STEP_C : -MODE_DELTA_STEP_C;
    const bool additiveReturn =
        previousMode == MODE_NORMAL &&
        lastModeBeforeNormal == mode &&
        (uint32_t)(millis() - enteredNormalAt) <= PLUS_MINUS_NORMAL_RETURN_MS;
    if (additiveReturn) {
      plusMinusOffsetC += direction;
    } else {
      plusMinusOffsetC = direction;
    }
    lastModeBeforeNormal = mode;
    enteredNormalAt = millis();
  } else {
    plusMinusOffsetC = 0;
    lastModeBeforeNormal = mode;
    enteredNormalAt = millis();
  }

  if (mode == MODE_DOUCHE && previousMode != MODE_DOUCHE) {
    doucheUntil = millis() + DOUCHE_DURATION_MS;
  }

  recomputeZoneWorkloads();
  doucheWasActive = doucheActive();
  sendPiloteSet();
}

void refreshTimedModeEffects() {
  if (stableMode != MODE_DOUCHE) {
    doucheWasActive = false;
    return;
  }
  const bool active = doucheActive();
  if (active != doucheWasActive) {
    doucheWasActive = active;
    recomputeZoneWorkloads();
    sendPiloteSet();
  }
}

void updateModeInput() {
  const ModeValue rawMode = readRawMode();
  const unsigned long now = millis();
  if (rawMode == MODE_NONE || rawMode == MODE_INVALID) {
    return;
  }
  if (rawMode != lastRawMode) {
    lastRawMode = rawMode;
    modeChangedAt = now;
  }
  if ((uint32_t)(now - modeChangedAt) < MODE_DEBOUNCE_MS || rawMode == stableMode) {
    return;
  }

  applyModeSelection(rawMode);
}

bool isDigitAt(const char *text, uint8_t index) {
  return text[index] >= '0' && text[index] <= '9';
}

uint8_t twoDigitsAt(const char *text, uint8_t index) {
  return (text[index] - '0') * 10 + (text[index + 1] - '0');
}

bool timestampFormatLooksValid(const char *text) {
  return isDigitAt(text, 0) &&
      isDigitAt(text, 1) &&
      isDigitAt(text, 2) &&
      isDigitAt(text, 3) &&
      text[4] == '-' &&
      isDigitAt(text, 5) &&
      isDigitAt(text, 6) &&
      text[7] == '-' &&
      isDigitAt(text, 8) &&
      isDigitAt(text, 9) &&
      text[10] == ' ' &&
      isDigitAt(text, 11) &&
      isDigitAt(text, 12) &&
      text[13] == ':' &&
      isDigitAt(text, 14) &&
      isDigitAt(text, 15) &&
      text[16] == ':' &&
      isDigitAt(text, 17) &&
      isDigitAt(text, 18);
}

bool timestampValuesLookValid(const char *text) {
  if (!timestampFormatLooksValid(text)) {
    return false;
  }

  const uint8_t month = twoDigitsAt(text, 5);
  const uint8_t day = twoDigitsAt(text, 8);
  const uint8_t hour = twoDigitsAt(text, 11);
  const uint8_t minute = twoDigitsAt(text, 14);
  const uint8_t second = twoDigitsAt(text, 17);
  return month >= 1 && month <= 12 &&
      day >= 1 && day <= 31 &&
      hour <= 23 &&
      minute <= 59 &&
      second <= 59;
}

uint16_t timestampDayKey(const char *text) {
  return ((uint16_t)twoDigitsAt(text, 2) << 9) |
      ((uint16_t)twoDigitsAt(text, 5) << 5) |
      twoDigitsAt(text, 8);
}

bool timestampForcedSyncAllowed(const char *text) {
  if (!timestampValuesLookValid(text) ||
      twoDigitsAt(text, 11) != CLOCK_FORCED_RESYNC_HOUR) {
    return false;
  }

  return timestampDayKey(text) != lastForcedClockSyncDayKey;
}

bool parseTimestamp(const char *text) {
  if (!timestampValuesLookValid(text)) {
    return false;
  }

  const uint8_t month = twoDigitsAt(text, 5);
  const uint8_t day = twoDigitsAt(text, 8);
  const uint8_t hour = twoDigitsAt(text, 11);
  const uint8_t minute = twoDigitsAt(text, 14);
  const uint8_t second = twoDigitsAt(text, 17);

  setTime(hour, minute, second, day, month, 2000 + twoDigitsAt(text, 2));
  clockSet = true;
  lastClockSyncAt = millis();
  if (hour == CLOCK_FORCED_RESYNC_HOUR) {
    lastForcedClockSyncDayKey = timestampDayKey(text);
  }
  applyCurrentLedBrightness();
  return true;
}

bool clockResyncAllowed() {
  return !clockSet || (uint32_t)(millis() - lastClockSyncAt) >= CLOCK_RESYNC_INTERVAL_MS;
}

uint8_t debugZoneFromText(const char *text) {
  if (text == nullptr || text[0] != 'Z' || text[1] < '1' || text[1] > '4') {
    return 0;
  }
  return text[1] - '0';
}

bool parseDebugSetpointDeciC(const char *text, int16_t &value) {
  if (text == nullptr || text[0] == '\0') {
    return false;
  }

  bool negative = false;
  if (*text == '-') {
    negative = true;
    text++;
  }
  if (*text < '0' || *text > '9') {
    return false;
  }

  int16_t whole = 0;
  while (*text >= '0' && *text <= '9') {
    whole = whole * 10 + (*text - '0');
    text++;
  }

  int16_t deci = 0;
  if (*text == '.' || *text == ',') {
    text++;
    if (*text < '0' || *text > '9') {
      return false;
    }
    deci = *text - '0';
    text++;
  }
  if (*text == 'C' || *text == 'c') {
    text++;
  }
  if (*text != '\0') {
    return false;
  }

  value = whole * 10 + deci;
  if (negative) {
    value = -value;
  }
  return value >= 50 && value <= 350;
}

void applyDebugSetpoint(uint8_t zone, bool active, int16_t setpointDeciC) {
  if (zone == 0) {
    for (uint8_t i = 0; i < PILOTE_ZONE_COUNT; i++) {
      debugForcedSetpointActive[i] = active;
      debugForcedSetpointDeciC[i] = setpointDeciC;
    }
  } else {
    debugForcedSetpointActive[zone - 1] = active;
    debugForcedSetpointDeciC[zone - 1] = setpointDeciC;
  }

  recomputeZoneWorkloads();
  sendPiloteSet();
  if (zone == 0) {
    debugPrintOverview();
  } else {
    debugPrintZoneState(zone);
  }
}

void debugPrintTime() {
  debugPrefix();
  Serial.print(F("TIME "));
  if (!clockSet) {
    Serial.println(F("NA"));
    return;
  }
  Serial.print(year());
  Serial.print('-');
  if (month() < 10) {
    Serial.print('0');
  }
  Serial.print(month());
  Serial.print('-');
  if (day() < 10) {
    Serial.print('0');
  }
  Serial.print(day());
  Serial.print(' ');
  if (hour() < 10) {
    Serial.print('0');
  }
  Serial.print(hour());
  Serial.print(':');
  if (minute() < 10) {
    Serial.print('0');
  }
  Serial.print(minute());
  Serial.print(':');
  if (second() < 10) {
    Serial.print('0');
  }
  Serial.print(second());
  Serial.print(F(" BRIGHTNESS="));
  Serial.println(currentLedBrightness());
}

bool handleDebugCommand(const char *line) {
  if (strcmp(line, "DBG?") == 0 || strcmp(line, "DBG") == 0) {
    debugPrintOverview();
    return true;
  }
  if (strcmp(line, "DBG HELP") == 0) {
    debugPrintHelp();
    return true;
  }
  if (strcmp(line, "DBG ASSOC") == 0) {
    debugPrintAssociations();
    return true;
  }
  if (strcmp(line, "DBG TIME?") == 0 || strcmp(line, "DBG TIME") == 0) {
    debugPrintTime();
    return true;
  }
  if (strncmp(line, "DBG TIME ", 9) == 0) {
    if (parseTimestamp(line + 9)) {
      debugPrintTime();
    } else {
      debugPrintHelp();
    }
    return true;
  }
  if (strncmp(line, "DBG SET ", 8) == 0) {
    const char *arg = line + 8;
    uint8_t zone = 0;
    if (arg[0] == 'Z' && arg[1] >= '1' && arg[1] <= '4' && arg[2] == ' ') {
      zone = arg[1] - '0';
      arg += 3;
    }
    if (strcmp(arg, "OFF") == 0 || strcmp(arg, "off") == 0) {
      applyDebugSetpoint(zone, false, SETPOINT_NORMAL_DECI_C);
      return true;
    }
    int16_t setpointDeciC = 0;
    if (parseDebugSetpointDeciC(arg, setpointDeciC)) {
      applyDebugSetpoint(zone, true, setpointDeciC);
    } else {
      debugPrintHelp();
    }
    return true;
  }
  if (strncmp(line, "DBG PROG ", 9) == 0) {
    const uint8_t zone = debugZoneFromText(line + 9);
    if (zone > 0) {
      debugPrintProgram(zone);
    } else {
      debugPrintHelp();
    }
    return true;
  }
  if (strncmp(line, "DBG Z", 5) == 0) {
    const uint8_t zone = debugZoneFromText(line + 4);
    if (zone > 0) {
      debugPrintZoneState(zone);
    } else {
      debugPrintHelp();
    }
    return true;
  }
  return false;
}

void handlePiloteLine(char *line) {
  if (handleDebugCommand(line)) {
    return;
  }

  if (strncmp(line, "PIL> ", 5) == 0) {
    line += 5;
  }

  if (strncmp(line, "#DBG", 4) == 0 ||
      strncmp(line, "CON> #DBG", 9) == 0 ||
      strncmp(line, "ERR commande inconnue: #DBG", 27) == 0 ||
      strncmp(line, "ERR commande inconnue: DBG", 26) == 0) {
    return;
  }

  if (strncmp(line, "TIMESTAMP=", 10) == 0) {
    piloteSerialOk = true;
    if (strcmp(line + 10, "NA") != 0 &&
        (clockResyncAllowed() || timestampForcedSyncAllowed(line + 10))) {
      parseTimestamp(line + 10);
    }
    return;
  }

  if (strncmp(line, "PAPP=", 5) == 0 ||
      strncmp(line, "ACK ", 4) == 0 ||
      strncmp(line, "PONG ", 5) == 0) {
    piloteSerialOk = true;
    return;
  }

  if (line[0] == 'Z' &&
      line[1] >= '1' &&
      line[1] <= '4' &&
      strncmp(line + 2, "_PUISSANCE=", 11) == 0) {
    piloteSerialOk = true;
    ZoneState *state = zoneState(line[1] - '0');
    if (state != nullptr) {
      state->powerVa = (uint16_t)atoi(line + 13);
      if (DEBUG_ENABLED) {
        debugPrefix();
        Serial.print(F("PILOTE Z"));
        Serial.print(line[1]);
        Serial.print(F(" POWER="));
        Serial.println(state->powerVa);
      }
    }
  }
}

uint32_t colorForZoneState(uint8_t zoneIndex) {
  const uint32_t phaseMs = millis() % 7000UL;
  if (zones[zoneIndex].sondeMissing) {
    return rgb(255, 0, 0);
  }
  if (zones[zoneIndex].doorMissing) {
    return rgb(140, 0, 255);
  }
  if (zones[zoneIndex].sondeLowBattery && phaseMs >= 6000UL) {
    return rgb(255, 0, 0);
  }
  if (zones[zoneIndex].doorLowBattery && phaseMs >= 6000UL) {
    return rgb(140, 0, 255);
  }
  if (zones[zoneIndex].doorOpen) {
    return rgb(0, 80, 255);
  }
  if (zones[zoneIndex].workload > 0) {
    return rgb(255, 180, 0);
  }
  return rgb(0, 0, 0);
}

void renderHeatingStateLeds() {
  for (uint8_t i = 0; i < PILOTE_ZONE_COUNT; i++) {
    setPixel(i, colorForZoneState(i));
  }
}

void readPiloteSerial() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      piloteLine[piloteLineLen] = '\0';
      handlePiloteLine(piloteLine);
      piloteLineLen = 0;
    } else if (piloteLineLen < sizeof(piloteLine) - 1) {
      piloteLine[piloteLineLen++] = c;
    } else {
      piloteLineLen = 0;
    }
  }
}

void applyDoorReportToZone(ZoneState &state, const ThermioRfFrame::Report &report, uint8_t zone) {
  const bool previousDoorOpen = state.doorOpen;
  state.doorSeen = true;
  state.doorMissing = false;
  state.lastDoorReportAt = millis();
  state.doorLowBattery = lowBatteryForType(report.deviceType, report.batteryMv);
  state.doorOpen = report.doorOpen;
  if (DEBUG_ENABLED && previousDoorOpen != state.doorOpen) {
    debugPrefix();
    Serial.print(F("DOOR Z"));
    Serial.print(zone);
    Serial.print(' ');
    Serial.println(state.doorOpen ? F("OPEN") : F("CLOSED"));
  }
}

bool applySondeReportToZone(ZoneState &state,
                            const ThermioRfFrame::Report &report,
                            uint8_t zone,
                            uint32_t now) {
  bool regulationChanged = false;
  state.sondeSeen = true;
  state.sondeMissing = false;
  state.lastSondeReportAt = now;
  state.sondeLowBattery = lowBatteryForType(report.deviceType, report.batteryMv);

  if (report.presenceCount > 0) {
    state.presenceSeen = true;
    state.lastPresenceAt = now;
  }

  if (report.hasSetpoint) {
    if (!state.hasSondeSetpoint || state.sondeSetpointDeciC != report.setpointDeciC) {
      state.sondeSetpointDeciC = report.setpointDeciC;
      state.hasSondeSetpoint = true;
      regulationChanged = true;
    }
    if (state.learningEnabled != report.learningEnabled) {
      state.learningEnabled = report.learningEnabled;
      saveLearningEnabledToEeprom();
      regulationChanged = true;
    }
  }

  if (report.tempCount > 0) {
    const int16_t nextTempDeciC = report.temperaturesDeciC[report.tempCount - 1];
    if (state.hasTemperature && state.hasRegulationDecision) {
      heatingRegulator.observe(
          zone - 1,
          state.measuredTempDeciC,
          nextTempDeciC,
          state.lastRequestedBtuPerHour,
          state.lastMaintenanceBtuPerHour,
          state.learningEnabled);
    }
    state.measuredTempDeciC = nextTempDeciC;
    state.hasTemperature = true;
    regulationChanged = true;
  }

  return regulationChanged;
}

void resetZoneLearningState(uint8_t zone) {
  ZoneState *state = zoneState(zone);
  if (state == nullptr) {
    return;
  }

  state->usualSetpointDeciC = SETPOINT_NORMAL_DECI_C;
  state->currentSetpointDeciC = SETPOINT_NORMAL_DECI_C;
  state->hasRegulationDecision = false;
  state->learningEnabled = true;
  heatingRegulator.resetZone(zone - 1);
  saveLearningEnabledToEeprom();
  recomputeZoneWorkloads();
  sendPiloteSet();
}

void resetAllLearningState() {
  for (uint8_t zone = 1; zone <= PILOTE_ZONE_COUNT; zone++) {
    ZoneState *state = zoneState(zone);
    if (state != nullptr) {
      state->usualSetpointDeciC = SETPOINT_NORMAL_DECI_C;
      state->currentSetpointDeciC = SETPOINT_NORMAL_DECI_C;
      state->hasRegulationDecision = false;
      state->learningEnabled = true;
    }
  }
  heatingRegulator.reset();
  saveLearningEnabledToEeprom();
  recomputeZoneWorkloads();
  sendPiloteSet();
}

void handleAdminRequest(uint8_t zone, const ThermioRfFrame::Report &report) {
  if (report.deviceType != ThermioRfFrame::DeviceSonde) {
    return;
  }

  if (report.adminRequest == ThermioRfFrame::AdminClearZoneLearning) {
    resetZoneLearningState(zone);
  } else if (report.adminRequest == ThermioRfFrame::AdminClearAllLearning) {
    resetAllLearningState();
  }
}

void updateZoneStateFromReport(uint16_t sourceId, const ThermioRfFrame::Report &report) {
  const uint8_t zone = zoneForSlave(sourceId);
  ZoneState *state = zoneState(zone);
  if (state == nullptr) {
    return;
  }

  if (DEBUG_ENABLED) {
    debugPrefix();
    Serial.print(F("RF RX NODE="));
    Serial.print(sourceId, HEX);
    Serial.print(F(" TYPE="));
    Serial.print(deviceTypeName(report.deviceType));
    Serial.print(F(" ZONE="));
    Serial.print(zone);
    Serial.print(F(" BAT="));
    Serial.print(report.batteryMv);
    Serial.print(F("MV PRES="));
    Serial.print(report.presenceCount);
    Serial.print(F(" DOOR_TOGGLE="));
    Serial.print(report.doorToggleCount);
    Serial.print(F(" USER_DELTA="));
    Serial.println(report.userDeltaSteps);
  }

  updateAhtOffsetFromReport(report);
  handleAdminRequest(zone, report);
  if (report.deviceType == ThermioRfFrame::DeviceDoor) {
    applyDoorReportToZone(*state, report, zone);
    recomputeZoneWorkloads();
    sendPiloteSet();
    debugPrintZoneState(zone);
  } else if (report.deviceType == ThermioRfFrame::DeviceSonde) {
    const bool regulationChanged = applySondeReportToZone(*state, report, zone, millis());
    if (report.userDeltaSteps != 0) {
      startUserDeltaFeedback(zone, report.userDeltaSteps);
    }
    if (regulationChanged) {
      recomputeZoneWorkloads();
      sendPiloteSet();
    }
    debugPrintZoneState(zone);
  }
}

void updateRf() {
  if ((uint32_t)(millis() - lastRfRxRefreshAt) >= RF_RX_REFRESH_INTERVAL_MS) {
    lastRfRxRefreshAt = millis();
    radio.strobeRx();
  }

  uint8_t reportSequence = 0;
  if (!readReport(reportSequence)) {
    return;
  }

  const bool duplicate =
      hasLastReportSequence &&
      reportSequence == lastReportSequence &&
      lastPacketSourceId == lastReportSourceId;

  if (!duplicate) {
    hasLastReportSequence = true;
    lastReportSequence = reportSequence;
    lastReportSourceId = lastPacketSourceId;

    if (findAssociatedSlave(lastPacketSourceId) < 0 &&
        (uint32_t)millis() < RF_ASSOCIATION_WINDOW_MS) {
      beginOrRefreshAssociation(lastPacketSourceId, lastReport.deviceType);
    }
    if (!associationActive &&
        lastReport.adminRequest == ThermioRfFrame::AdminPair &&
        (findAssociatedSlave(lastPacketSourceId) >= 0 ||
         (uint32_t)millis() < RF_ASSOCIATION_WINDOW_MS)) {
      beginOrRefreshAssociation(lastPacketSourceId, lastReport.deviceType);
    }
    if (associationActive &&
        associationNodeId == lastPacketSourceId &&
        lastReport.adminRequest == ThermioRfFrame::AdminPair) {
      if (lastReport.pairZoneRequest >= 1 && lastReport.pairZoneRequest <= RF_ASSOC_ZONE_COUNT) {
        associationZone = lastReport.pairZoneRequest;
        associationSaveAt = millis() + RF_ASSOCIATION_SAVE_DELAY_MS;
      }
    }
    updateZoneStateFromReport(lastPacketSourceId, lastReport);
  }

  sendAckBurst(lastPacketSourceId, reportSequence);
  if (!duplicate) {
    startRfReceivedBlink(zoneForSlave(lastPacketSourceId), lastReport.deviceType);
  }
  radio.strobeRx();
}

void setup() {
  for (const ModeInput &input : modeInputs) {
    pinMode(input.pin, INPUT);
  }

  Serial.begin(PILOTE_SERIAL_BAUD);
  leds.setBrightness(LED_BRIGHTNESS_MIN);
  leds.begin();
  leds.clear();
  setPixel(LED_MODE, rgb(255, 255, 255));
  showLeds();

  loadOrCreateConsoleId();
  loadAssociationTableFromEeprom();
  loadAhtOffsetFromEeprom();
  heatingRegulator.reset();
  initializeZoneStates();
  loadLearningEnabledFromEeprom();
  Wire.begin();
  updateConsoleTemperature(millis(), true);
  initializeModeSelection();

  radio.beginPins();
  SPI.begin();
  radio.wake();
  rfOk = radio.testSpi();
  if (!rfOk) {
    consoleHalted = true;
    consoleHaltBlink = true;
    return;
  }
  SPI.beginTransaction(RF_SPI_SETTINGS);
  radio.configureTestRadio(ThermioRfFrame::MaxPacketLen);
  radio.strobeRx();
  lastRfRxRefreshAt = millis();
  sendPiloteSet();
  if (!waitForPiloteAtBoot()) {
    consoleHalted = true;
    consoleHaltBlink = false;
    return;
  }
  if (DEBUG_ENABLED) {
    debugPrefix();
    Serial.print(F("BOOT ID="));
    Serial.print(consoleId, HEX);
    Serial.print(F(" MODE="));
    Serial.print(modeName(stableMode));
    Serial.print(F(" RF="));
    Serial.println(rfOk ? F("OK") : F("KO"));
    debugPrintAssociations();
    debugPrintOverview();
  }
  showLeds();
}

void loop() {
  if (consoleHalted) {
    clearAllLeds();
    setPixel(LED_CENTRE, centerStatusColor(millis()));
    showLeds();
    delay(50);
    return;
  }

  readPiloteSerial();
  const uint32_t now = millis();
  updateConsoleTemperature(now, false);
  updateHeatingHistory(now);
  updateMissingDeviceStates(now);
  updateRf();
  savePendingAssociationIfDue();

  refreshTimedModeEffects();
  updateModeInput();
  renderHeatingStateLeds();
  setPixel(LED_MODE, colorForMode(stableMode));
  setPixel(LED_CENTRE, centerStatusColor(now));
  updateRfReceivedBlink();
  applyUserDeltaFeedbackLed();
  updateAssociationLeds();
  showLeds();
  delay(10);
}
