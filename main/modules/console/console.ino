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
#include <avr/wdt.h>
#include <stdlib.h>

#include <ThermioRfCc1101.h>
#include <ThermioRfFrame.h>
#include <ThermioRfIds.h>
#include <ThermioHeatingRegulator.h>
#include <ThermioSetpointPolicy.h>
#include <ThermioStorageLayout.h>
#include <ThermioWeeklyProgram.h>

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
constexpr uint16_t PLUS_MINUS_APPLY_DELAY_MS = 3000;
constexpr uint16_t PLUS_MINUS_FEEDBACK_MS = 1000;
constexpr uint32_t DOUCHE_DURATION_MS = 30UL * 60UL * 1000UL;
constexpr uint8_t ZONE_SDB = 4;
constexpr int16_t SETPOINT_NORMAL_DECI_C = 190;
constexpr int16_t SETPOINT_VACANCE_DEFAULT_DECI_C = 170;
constexpr int16_t FALLBACK_MEASURED_TEMP_DECI_C = 180;
constexpr uint16_t MIN_VALID_ZONE_POWER_VA = 700;
constexpr uint16_t MAX_VALID_ZONE_POWER_VA = 3000;
constexpr uint16_t FALLBACK_ZONE_POWER_VA = 1200;
constexpr uint16_t SONDE_LOW_BATTERY_MV = 2000;
constexpr uint16_t DOOR_LOW_BATTERY_MV = 1000;
constexpr uint16_t BATTERY_NO_BATTERY_MV = 500;
constexpr int8_t MODE_DELTA_STEP_C = 1;
constexpr int8_t DOUCHE_SDB_DELTA_C = 2;
constexpr int8_t DOUCHE_OTHER_DELTA_C = -1;
constexpr uint32_t HEAT_LAST_HOUR_MS = 60UL * 60UL * 1000UL;
constexpr uint32_t HEAT_LAST_DAY_MS = 24UL * 60UL * 60UL * 1000UL;
constexpr uint32_t DEVICE_MISSING_TIMEOUT_MS = 2UL * 60UL * 60UL * 1000UL;
constexpr uint32_t AUTO_VACANCE_NO_PRESENCE_S = 48UL * 60UL * 60UL;
constexpr uint32_t SONDE_OVERRIDE_DURATION_S = 2UL * 60UL * 60UL;
constexpr uint16_t NO_PROGRAM_SOURCE = 0xFFFF;
constexpr uint16_t USER_DELTA_FEEDBACK_RAMP_UP_MS = 500;
constexpr uint16_t USER_DELTA_FEEDBACK_TOTAL_MS = 3000;
constexpr uint8_t LED_BRIGHTNESS_MIN = 3;
constexpr uint8_t LED_BRIGHTNESS_MAX = 49;
constexpr uint16_t LED_BRIGHTNESS_MORNING_RAMP_START_MIN = 7 * 60;
constexpr uint16_t LED_BRIGHTNESS_MAX_START_MIN = 9 * 60;
constexpr uint16_t LED_BRIGHTNESS_EVENING_RAMP_START_MIN = 17 * 60;
constexpr uint16_t LED_BRIGHTNESS_MIN_START_MIN = 21 * 60;
constexpr uint32_t CLOCK_RESYNC_INTERVAL_MS = 12UL * 60UL * 60UL * 1000UL;
constexpr uint32_t CLOCK_STARTUP_SYNC_WINDOW_MS = 2UL * 60UL * 1000UL;
constexpr uint8_t CLOCK_FORCED_RESYNC_HOUR = 3;
constexpr uint8_t AHT_ADDR = 0x38;
constexpr uint8_t EXT_EEPROM_ADDR_FIRST = 0x50;
constexpr uint8_t EXT_EEPROM_ADDR_LAST = 0x57;
constexpr uint32_t EXT_EEPROM_SIZE_BYTES = 65536UL;
constexpr uint8_t EXT_EEPROM_WRITE_CHUNK = 16;
constexpr uint32_t CONSOLE_TEMP_REFRESH_MS = 60000UL;
constexpr uint32_t PILOTE_BOOT_TIMEOUT_MS = 8000UL;
constexpr uint32_t CENTER_BOOT_OK_MS = 3000UL;
constexpr uint8_t FAILSAFE_WORKLOAD = 192;
constexpr int16_t FAILSAFE_PLUS_LIMIT_DECI_C = 230;
constexpr int16_t FAILSAFE_HYSTERESIS_DECI_C = 1;
constexpr bool DEBUG_ENABLED = false;

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
constexpr int EEPROM_VACATION_MAGIC = EEPROM_LEARNING_MASK + 1;
constexpr int EEPROM_VACATION_VALUE_LOW = EEPROM_VACATION_MAGIC + 1;
constexpr int EEPROM_VACATION_VALUE_HIGH = EEPROM_VACATION_VALUE_LOW + 1;
constexpr uint8_t EEPROM_VACATION_MAGIC_VALUE = 0x56;
// EEPROM_VACATION_VALUE_HIGH + 1/+2 and +17/+18 were used by earlier factory
// reset counters. Keep them reserved: do not write them anymore.
constexpr int EEPROM_FACTORY_BOOT_MAGIC_0 = EEPROM_VACATION_VALUE_HIGH + 33;
constexpr int EEPROM_FACTORY_BOOT_MAGIC_1 = EEPROM_FACTORY_BOOT_MAGIC_0 + 1;
constexpr int EEPROM_FACTORY_BOOT_COUNT = EEPROM_FACTORY_BOOT_MAGIC_1 + 1;
constexpr int EEPROM_FACTORY_BOOT_COUNT_INV = EEPROM_FACTORY_BOOT_COUNT + 1;
constexpr uint8_t EEPROM_FACTORY_BOOT_MAGIC_VALUE_0 = 0x3A;
constexpr uint8_t EEPROM_FACTORY_BOOT_MAGIC_VALUE_1 = 0xC5;
constexpr uint8_t FACTORY_RESET_BOOT_THRESHOLD = 6;
constexpr uint32_t FACTORY_RESET_BOOT_WINDOW_MS = 5000UL;
constexpr uint32_t FACTORY_RESET_BOOT_BLINK_MS = 1500UL;
constexpr uint32_t FACTORY_RESET_ERASE_BLINK_MS = 150UL;
constexpr uint32_t FACTORY_RESET_DONE_BLINK_MS = 5000UL;
constexpr uint8_t FACTORY_RESET_EXT_WRITE_DELAY_MS = 5;
constexpr uint8_t SCHEDULE_SLOT_0630 = 6 * 4 + 2;
constexpr uint8_t SCHEDULE_SLOT_1630 = 16 * 4 + 2;
constexpr uint8_t SCHEDULE_SLOT_2100 = 21 * 4;
constexpr int16_t SCHEDULE_SETPOINT_0630_DECI_C = 195;
constexpr int16_t SCHEDULE_SETPOINT_1630_DECI_C = 205;
constexpr int16_t SCHEDULE_SETPOINT_2100_DECI_C = 175;

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

enum ConsoleRunMode : uint8_t {
  RUN_NORMAL,
  RUN_FAILSAFE
};

enum FailsafeCommand : uint8_t {
  FAILSAFE_HOLD,
  FAILSAFE_COOL_DOWN,
  FAILSAFE_HEAT,
  FAILSAFE_STOP
};

struct ModeInput {
  uint8_t pin;
  ModeValue mode;
};

struct ZoneState {
  uint32_t lastHeatAt;
  uint32_t lastPresenceAt;
  uint32_t lastSondeReportAt;
  uint32_t lastDoorReportAt;
  uint32_t sondeSetpointStartedAt;
  int16_t usualSetpointDeciC;
  int16_t currentSetpointDeciC;
  int16_t workSetpointDeciC;
  int16_t measuredTempDeciC;
  int16_t sondeSetpointDeciC;
  uint16_t usualProgramSource;
  uint16_t sondeProgramSourceAtStart;
  uint16_t powerVa;
  uint16_t humidityDeciPct;
  uint16_t lastRequestedBtuPerHour;
  uint16_t lastMaintenanceBtuPerHour;
  uint16_t lastEffectiveHoldBtuPerHour;
  uint8_t workload;
  uint16_t hasTemperature : 1;
  uint16_t hasSondeSetpoint : 1;
  uint16_t hasUsualProgram : 1;
  uint16_t hasHumidity : 1;
  uint16_t hasRegulationDecision : 1;
  uint16_t learningEnabled : 1;
  uint16_t heatSeen : 1;
  uint16_t presenceSeen : 1;
  uint16_t sondeSeen : 1;
  uint16_t doorSeen : 1;
  uint16_t doorOpen : 1;
  uint16_t sondeLowBattery : 1;
  uint16_t doorLowBattery : 1;
  uint16_t sondeMissing : 1;
  uint16_t doorMissing : 1;
  uint16_t doorBypassed : 1;
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
ThermioByteStorage externalStorage;
ThermioWeeklyProgram weeklyProgram;
uint8_t extEepromAddress = 0;
AssociatedSlave associatedSlaves[RF_MAX_ASSOCIATED_SLAVES] = {};

uint16_t consoleId = RF_DEFAULT_CONSOLE_ID;
uint8_t rfSequence = 0;
uint32_t lastRfRxRefreshAt = 0;
bool associationActive = false;
uint16_t associationNodeId = ThermioRfFrame::BroadcastId;
uint8_t associationDeviceType = 0;
uint8_t associationZone = 1;
uint32_t associationSaveAt = 0;
uint32_t associationWindowUntil = 0;
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
bool autoVacanceActive = false;
bool presenceReferenceStarted = false;
uint32_t lastApartmentPresenceOrReferenceAt = 0;
int8_t plusMinusOffsetC = 0;
int8_t plusMinusAppliedOffsetC = 0;
bool plusMinusApplyPending = false;
uint32_t plusMinusApplyAt = 0;
uint32_t plusMinusFeedbackUntil = 0;
int8_t ahtOffsetDeciC = 0;
int16_t consoleTempDeciC = FALLBACK_MEASURED_TEMP_DECI_C;
bool consoleTempKnown = false;
int16_t vacationSetpointDeciC = SETPOINT_VACANCE_DEFAULT_DECI_C;
uint32_t nextConsoleTempRefreshAt = 0;
bool clockSet = false;
uint32_t lastClockSyncAt = 0;
uint16_t lastForcedClockSyncDayKey = 0;
uint16_t debugTimeSpeed = 1;
uint32_t debugTimeSpeedLastMs = 0;
uint32_t debugTimeSpeedExtraMs = 0;
ZoneState zones[PILOTE_ZONE_COUNT] = {};
char piloteLine[96];
uint8_t piloteLineLen = 0;
bool piloteSerialOk = false;
bool rfOk = false;
bool consoleHalted = false;
bool consoleHaltBlink = false;
ConsoleRunMode runMode = RUN_NORMAL;
FailsafeCommand failsafeCommand = FAILSAFE_HOLD;
bool rfStarted = false;
int16_t failsafeTargetDeciC = FALLBACK_MEASURED_TEMP_DECI_C;
bool failsafeHeating = false;
uint32_t centerBootOkUntil = 0;
uint8_t debugForcedSetpointMask = 0;
int16_t debugForcedSetpointDeciC[PILOTE_ZONE_COUNT] = {
  SETPOINT_NORMAL_DECI_C,
  SETPOINT_NORMAL_DECI_C,
  SETPOINT_NORMAL_DECI_C,
  SETPOINT_NORMAL_DECI_C
};
uint8_t debugForcedTempMask = 0;
int16_t debugForcedTempDeciC[PILOTE_ZONE_COUNT] = {
  FALLBACK_MEASURED_TEMP_DECI_C,
  FALLBACK_MEASURED_TEMP_DECI_C,
  FALLBACK_MEASURED_TEMP_DECI_C,
  FALLBACK_MEASURED_TEMP_DECI_C
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

void showFactoryResetCenter(bool white) {
  leds.setBrightness(LED_BRIGHTNESS_MIN);
  clearAllLeds();
  setPixel(LED_CENTRE, white ? rgb(255, 255, 255) : rgb(255, 0, 0));
  leds.show();
}

void clearFactoryResetBootCounter() {
  EEPROM.update(EEPROM_FACTORY_BOOT_MAGIC_0, EEPROM_FACTORY_BOOT_MAGIC_VALUE_0);
  EEPROM.update(EEPROM_FACTORY_BOOT_MAGIC_1, EEPROM_FACTORY_BOOT_MAGIC_VALUE_1);
  EEPROM.update(EEPROM_FACTORY_BOOT_COUNT, 0);
  EEPROM.update(EEPROM_FACTORY_BOOT_COUNT_INV, 0xFF);
}

uint8_t incrementFactoryResetBootCounter() {
  uint8_t count = 1;
  const bool bootCounterValid =
      EEPROM.read(EEPROM_FACTORY_BOOT_MAGIC_0) == EEPROM_FACTORY_BOOT_MAGIC_VALUE_0 &&
      EEPROM.read(EEPROM_FACTORY_BOOT_MAGIC_1) == EEPROM_FACTORY_BOOT_MAGIC_VALUE_1;
  if (bootCounterValid) {
    const uint8_t stored = EEPROM.read(EEPROM_FACTORY_BOOT_COUNT);
    const uint8_t storedInv = EEPROM.read(EEPROM_FACTORY_BOOT_COUNT_INV);
    if ((uint8_t)~stored == storedInv) {
      count = stored == 0xFF ? 1 : stored + 1;
      if (count == 0) {
        count = 0xFF;
      }
    }
  } else {
    EEPROM.update(EEPROM_FACTORY_BOOT_MAGIC_0, EEPROM_FACTORY_BOOT_MAGIC_VALUE_0);
    EEPROM.update(EEPROM_FACTORY_BOOT_MAGIC_1, EEPROM_FACTORY_BOOT_MAGIC_VALUE_1);
  }
  EEPROM.update(EEPROM_FACTORY_BOOT_COUNT, count);
  EEPROM.update(EEPROM_FACTORY_BOOT_COUNT_INV, (uint8_t)~count);
  return count;
}

void waitFactoryResetBootWindow() {
  const uint32_t startedAt = millis();
  uint32_t nextBlinkAt = 0;
  bool white = false;
  while (millis() - startedAt < FACTORY_RESET_BOOT_WINDOW_MS) {
    const uint32_t now = millis();
    if (nextBlinkAt == 0 || now >= nextBlinkAt) {
      white = !white;
      showFactoryResetCenter(white);
      nextBlinkAt = now + FACTORY_RESET_BOOT_BLINK_MS;
    }
    delay(20);
  }
  clearFactoryResetBootCounter();
  clearAllLeds();
  leds.show();
}

void updateFactoryBlinkIfDue(uint32_t &nextBlinkAt, bool &white, uint32_t intervalMs) {
  const uint32_t now = millis();
  if (nextBlinkAt == 0 || now >= nextBlinkAt) {
    white = !white;
    showFactoryResetCenter(white);
    nextBlinkAt = now + intervalMs;
  }
}

void eraseInternalEepromForFactoryReset(uint32_t &nextBlinkAt, bool &white) {
  for (uint16_t address = 0; address < EEPROM.length(); address++) {
    EEPROM.update(address, 0xFF);
    updateFactoryBlinkIfDue(nextBlinkAt, white, FACTORY_RESET_ERASE_BLINK_MS);
  }
}

void eraseExternalEepromForFactoryReset(uint32_t &nextBlinkAt, bool &white) {
  if (extEepromAddress == 0) {
    return;
  }
  for (uint32_t address = 0; address < EXT_EEPROM_SIZE_BYTES; address += EXT_EEPROM_WRITE_CHUNK) {
    Wire.beginTransmission(extEepromAddress);
    Wire.write((uint8_t)(address >> 8));
    Wire.write((uint8_t)address);
    for (uint8_t i = 0; i < EXT_EEPROM_WRITE_CHUNK; i++) {
      Wire.write((uint8_t)0xFF);
    }
    Wire.endTransmission();
    updateFactoryBlinkIfDue(nextBlinkAt, white, FACTORY_RESET_ERASE_BLINK_MS);
    delay(FACTORY_RESET_EXT_WRITE_DELAY_MS);
  }
}

void waitAfterFactoryResetComplete() {
  uint32_t nextBlinkAt = 0;
  bool white = false;
  while (true) {
    updateFactoryBlinkIfDue(nextBlinkAt, white, FACTORY_RESET_DONE_BLINK_MS);
    delay(50);
  }
}

void runFactoryReset() {
  uint32_t nextBlinkAt = 0;
  bool white = false;
  eraseInternalEepromForFactoryReset(nextBlinkAt, white);
  eraseExternalEepromForFactoryReset(nextBlinkAt, white);
  waitAfterFactoryResetComplete();
}

void handleFactoryResetBootSequence() {
  const uint8_t bootCount = incrementFactoryResetBootCounter();
  if (bootCount >= FACTORY_RESET_BOOT_THRESHOLD) {
    runFactoryReset();
  }
  waitFactoryResetBootWindow();
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

ModeValue effectiveMode() {
  if (stableMode == MODE_NORMAL && autoVacanceActive) {
    return MODE_VACANCES;
  }
  return stableMode;
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
  if (debugForcedTempIsActive(zone)) {
    debugPrintDeciC(debugForcedTempDeciC[zone - 1]);
    Serial.print(F("(FORCED)"));
  } else if (state->hasTemperature) {
    debugPrintDeciC(state->measuredTempDeciC);
    Serial.print(F("(SONDE)"));
  } else if (consoleTempKnown) {
    debugPrintDeciC(consoleTempDeciC);
    Serial.print(F("(CONSOLE)"));
  } else {
    debugPrintDeciC(FALLBACK_MEASURED_TEMP_DECI_C);
    Serial.print(F("(FALLBACK)"));
  }
  Serial.print(F(" [PROG="));
  debugPrintDeciC(state->usualSetpointDeciC);
  Serial.print(F(" SONDE_REQUEST="));
  if (state->hasSondeSetpoint) {
    debugPrintDeciC(state->sondeSetpointDeciC);
  } else {
    Serial.print(F("NA"));
  }
  Serial.print(F(" WORK_TRG="));
  debugPrintDeciC(regulationSetpointForZone(zone));
  if (debugForcedSetpointIsActive(zone)) {
    Serial.print(F("(FORCED)"));
  }
  Serial.print(']');
  Serial.print(F(" HUM="));
  if (state->hasHumidity) {
    Serial.print((state->humidityDeciPct + 5) / 10);
    Serial.print(F("PCT"));
  } else {
    Serial.print(F("NA"));
  }
  Serial.print(F(" [HOLD="));
  Serial.print(state->lastEffectiveHoldBtuPerHour);
  Serial.print(F("BTU/H HOLD_CONF="));
  Serial.print(heatingRegulator.holdConfidence(zone - 1));
  Serial.print(F(" MAINT="));
  Serial.print(state->lastMaintenanceBtuPerHour);
  Serial.print(F("BTU/H REQ="));
  Serial.print(state->lastRequestedBtuPerHour);
  Serial.print(F("BTU/H"));
  Serial.print(F(" RESP="));
  Serial.print(heatingRegulator.learnedResponseBtuPerC());
  Serial.print(F("BTU/C] [WORKLOAD="));
  Serial.print(workloadForZone(zone));
  Serial.print(F("/255 POWER="));
  Serial.print(installedPowerForZone(zone));
  Serial.print(F("VA] DOOR="));
  if (state->doorBypassed) {
    Serial.print(F("BYPASSED"));
  } else {
    Serial.print(state->doorOpen ? F("OPEN") : F("CLOSED"));
  }
  Serial.print(F(" SONDE_BAT="));
  Serial.print(state->sondeLowBattery ? F("LOW") : F("OK"));
  Serial.print(F(" DOOR_BAT="));
  Serial.print(state->doorLowBattery ? F("LOW") : F("OK"));
  Serial.print(F(" PRESENCE_AGE="));
  if (!state->presenceSeen) {
    Serial.print(F("NA"));
  } else {
    const uint32_t ageS = (uint32_t)::now() - state->lastPresenceAt;
    Serial.print(ageS / 60UL);
    Serial.print(F("MIN"));
  }
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
  Serial.print(F(" PROG="));
  debugPrintDeciC(state->usualSetpointDeciC);
  Serial.print(F(" WORK_TRG="));
  debugPrintDeciC(regulationSetpointForZone(zone));
  Serial.print(F(" MODE="));
  Serial.print(modeName(stableMode));
  if (autoVacanceActive) {
    Serial.print(F("(AUTO_VACANCES)"));
  }
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
  if (autoVacanceActive) {
    Serial.print(F("(AUTO_VACANCES)"));
  }
  Serial.print(F(" CLOCK="));
  Serial.print(F("OK"));
  Serial.print(F(" AHT_OFFSET="));
  debugPrintDeciC(ahtOffsetDeciC);
  Serial.print(F(" VAC_SET="));
  debugPrintDeciC(vacationSetpointDeciC);
  Serial.print(F(" RFSEQ="));
  Serial.println(rfSequence);
  for (uint8_t zone = 1; zone <= PILOTE_ZONE_COUNT; zone++) {
    debugPrintZoneState(zone);
  }
}

void debugPrintDayName() {
  switch (weekday()) {
    case 1:
      Serial.print(F("DIM"));
      break;
    case 2:
      Serial.print(F("LUN"));
      break;
    case 3:
      Serial.print(F("MAR"));
      break;
    case 4:
      Serial.print(F("MER"));
      break;
    case 5:
      Serial.print(F("JEU"));
      break;
    case 6:
      Serial.print(F("VEN"));
      break;
    case 7:
      Serial.print(F("SAM"));
      break;
    default:
      Serial.print(F("NA"));
      break;
  }
}

void debugPrintTimeHm() {
  if (hour() < 10) {
    Serial.print('0');
  }
  Serial.print(hour());
  Serial.print(':');
  if (minute() < 10) {
    Serial.print('0');
  }
  Serial.print(minute());
}

const __FlashStringHelper *debugZoneName(uint8_t zone) {
  switch (zone) {
    case 1:
      return F("SALON");
    case 2:
      return F("CHAMBRE");
    case 3:
      return F("BUREAU");
    case 4:
      return F("SDB");
    default:
      return F("ZONE");
  }
}

void debugPrintWatch() {
  if (!DEBUG_ENABLED) {
    return;
  }

  debugPrefix();
  Serial.print(F("WATCH "));
  debugPrintDayName();
  Serial.print(' ');
  debugPrintTimeHm();
  Serial.print(F(" MODE="));
  Serial.print(modeName(stableMode));
  for (uint8_t zone = 1; zone <= PILOTE_ZONE_COUNT; zone++) {
    const ZoneState *state = zoneStateConst(zone);
    if (state == nullptr) {
      continue;
    }
    Serial.print(' ');
    Serial.print(debugZoneName(zone));
    Serial.print('=');
    debugPrintDeciC(measuredTempForZone(zone));
    Serial.print('/');
    debugPrintDeciC(state->currentSetpointDeciC);
    Serial.print('>');
    debugPrintDeciC(regulationSetpointForZone(zone));
    Serial.print('/');
    Serial.print(state->doorBypassed ? 'B' : doorOpenForZone(zone) ? 'O' : 'F');
    Serial.print('/');
    Serial.print(state->workload);
    Serial.print(F("/C"));
    Serial.print(heatingRegulator.holdConfidence(zone - 1));
    Serial.print(F("/L"));
    Serial.print(state->learningEnabled ? '1' : '0');
  }
  Serial.println();
}

bool zoneIsValid(uint8_t zone) {
  return zone >= 1 && zone <= PILOTE_ZONE_COUNT;
}

static inline uint8_t zoneBit(uint8_t zone) {
  return zoneIsValid(zone) ? (uint8_t)(1U << (zone - 1)) : 0;
}

static inline bool debugForcedSetpointIsActive(uint8_t zone) {
  return (debugForcedSetpointMask & zoneBit(zone)) != 0;
}

static inline bool debugForcedTempIsActive(uint8_t zone) {
  return (debugForcedTempMask & zoneBit(zone)) != 0;
}

static inline void setDebugForcedSetpointActive(uint8_t zone, bool active) {
  const uint8_t bit = zoneBit(zone);
  if (active) {
    debugForcedSetpointMask |= bit;
  } else {
    debugForcedSetpointMask &= (uint8_t)~bit;
  }
}

static inline void setDebugForcedTempActive(uint8_t zone, bool active) {
  const uint8_t bit = zoneBit(zone);
  if (active) {
    debugForcedTempMask |= bit;
  } else {
    debugForcedTempMask &= (uint8_t)~bit;
  }
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
  return doorOpenForZone(zone) ? 0 : state->workload;
}

uint8_t logicalZoneForPiloteZone(uint8_t piloteZone) {
  return piloteZone >= 1 && piloteZone <= PILOTE_ZONE_COUNT ?
      PILOTE_ZONE_COUNT + 1 - piloteZone : 0;
}

bool doorOpenForZone(uint8_t zone) {
  const ZoneState *state = zoneStateConst(zone);
  if (state == nullptr) {
    return false;
  }
  return state->doorOpen && !state->doorBypassed;
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

bool presenceTrackingAvailable() {
  for (uint8_t i = 0; i < PILOTE_ZONE_COUNT; i++) {
    if (zones[i].sondeSeen && !zones[i].sondeMissing) {
      return true;
    }
  }
  return false;
}

bool presenceRecent(uint32_t lastPresenceEpoch, uint32_t currentEpoch) {
  return currentEpoch >= lastPresenceEpoch &&
      currentEpoch - lastPresenceEpoch < AUTO_VACANCE_NO_PRESENCE_S;
}

void resetAutoVacanceReference(uint32_t currentEpoch) {
  presenceReferenceStarted = true;
  lastApartmentPresenceOrReferenceAt = currentEpoch;
}

void noteApartmentPresence() {
  resetAutoVacanceReference((uint32_t)::now());
  if (autoVacanceActive) {
    autoVacanceActive = false;
    refreshZoneSetpoints();
    recomputeZoneWorkloads();
    if (DEBUG_ENABLED) {
      debugPrefix();
      Serial.println(F("AUTO_VACANCE OFF PRESENCE"));
    }
  }
}

void updateAutoVacance(uint32_t now) {
  if (now < CLOCK_STARTUP_SYNC_WINDOW_MS) {
    return;
  }
  if (stableMode != MODE_NORMAL || !presenceTrackingAvailable()) {
    presenceReferenceStarted = false;
    if (autoVacanceActive) {
      autoVacanceActive = false;
      refreshZoneSetpoints();
      recomputeZoneWorkloads();
      if (DEBUG_ENABLED) {
        debugPrefix();
        Serial.println(F("AUTO_VACANCE OFF MODE"));
      }
    }
    return;
  }

  const uint32_t currentEpoch = (uint32_t)::now();
  if (!presenceReferenceStarted ||
      lastApartmentPresenceOrReferenceAt == 0 ||
      currentEpoch < lastApartmentPresenceOrReferenceAt) {
    resetAutoVacanceReference(currentEpoch);
    return;
  }

  if (!autoVacanceActive &&
      currentEpoch - lastApartmentPresenceOrReferenceAt >= AUTO_VACANCE_NO_PRESENCE_S) {
    autoVacanceActive = true;
    refreshZoneSetpoints();
    recomputeZoneWorkloads();
    if (DEBUG_ENABLED) {
      debugPrefix();
      Serial.println(F("AUTO_VACANCE ON NO_PRESENCE_48H"));
    }
  }
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

void saveVacationSetpointToEeprom() {
  EEPROM.update(EEPROM_VACATION_MAGIC, EEPROM_VACATION_MAGIC_VALUE);
  EEPROM.update(EEPROM_VACATION_VALUE_LOW, vacationSetpointDeciC & 0xFF);
  EEPROM.update(EEPROM_VACATION_VALUE_HIGH, (uint16_t)vacationSetpointDeciC >> 8);
}

void loadVacationSetpointFromEeprom() {
  if (EEPROM.read(EEPROM_VACATION_MAGIC) != EEPROM_VACATION_MAGIC_VALUE) {
    vacationSetpointDeciC = SETPOINT_VACANCE_DEFAULT_DECI_C;
    saveVacationSetpointToEeprom();
    return;
  }

  const int16_t stored =
      (int16_t)((uint16_t)EEPROM.read(EEPROM_VACATION_VALUE_LOW) |
                ((uint16_t)EEPROM.read(EEPROM_VACATION_VALUE_HIGH) << 8));
  vacationSetpointDeciC =
      (stored >= 50 && stored <= 300) ? stored : SETPOINT_VACANCE_DEFAULT_DECI_C;
}

void updateVacationSetpointFromReport(const ThermioRfFrame::Report &report) {
  if (report.adminRequest != ThermioRfFrame::AdminSetVacationSetpoint ||
      !report.hasVacationSetpoint ||
      report.vacationSetpointDeciC == vacationSetpointDeciC) {
    return;
  }

  vacationSetpointDeciC = report.vacationSetpointDeciC;
  saveVacationSetpointToEeprom();
  refreshZoneSetpoints();
  recomputeZoneWorkloads();
  sendPiloteSet();
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

int8_t deciCToHalfC(int16_t deciC) {
  if (deciC >= 0) {
    return (int8_t)((deciC + 2) / 5);
  }
  return (int8_t)((deciC - 2) / 5);
}

int16_t halfCToDeciC(int8_t halfC) {
  return (int16_t)halfC * 5;
}

uint8_t extEepromRead(uint16_t address) {
  if (extEepromAddress == 0) {
    return 0xFF;
  }
  Wire.beginTransmission(extEepromAddress);
  Wire.write((uint8_t)(address >> 8));
  Wire.write((uint8_t)address);
  if (Wire.endTransmission() != 0) {
    return 0xFF;
  }
  Wire.requestFrom(extEepromAddress, (uint8_t)1);
  return Wire.available() == 1 ? Wire.read() : 0xFF;
}

bool extEepromWrite(uint16_t address, uint8_t value) {
  if (extEepromAddress == 0) {
    return false;
  }
  Wire.beginTransmission(extEepromAddress);
  Wire.write((uint8_t)(address >> 8));
  Wire.write((uint8_t)address);
  Wire.write(value);
  if (Wire.endTransmission() != 0) {
    return false;
  }
  delay(6);
  return true;
}

void detectExternalEeprom() {
  for (uint8_t address = EXT_EEPROM_ADDR_FIRST;
       address <= EXT_EEPROM_ADDR_LAST;
       address++) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      extEepromAddress = address;
      return;
    }
  }
}

bool extEepromUpdate(uint16_t address, uint8_t value) {
  if (extEepromRead(address) != value) {
    return extEepromWrite(address, value);
  }
  return true;
}

uint8_t readExternalStorageByte(void *, uint16_t address) {
  return extEepromRead(address);
}

bool updateExternalStorageByte(void *, uint16_t address, uint8_t value) {
  return extEepromUpdate(address, value);
}

bool scheduleStorageReady() {
  return extEepromRead(ThermioStorageLayout::ScheduleMagicAddress) ==
      ThermioStorageLayout::ScheduleMagicValue;
}

void loadScheduleFromEeprom() {
  externalStorage.begin(nullptr, readExternalStorageByte, updateExternalStorageByte);
  weeklyProgram.begin(externalStorage,
                      ThermioStorageLayout::ScheduleFirstPointAddress,
                      PILOTE_ZONE_COUNT,
                      ThermioStorageLayout::SchedulePointsPerDay);
  if (!scheduleStorageReady()) {
    weeklyProgram.clearAll();
    extEepromUpdate(ThermioStorageLayout::ScheduleMagicAddress,
                    ThermioStorageLayout::ScheduleMagicValue);
  }
}

void initializeHeatingState() {
  heatingRegulator.reset();
}

void loadTypicalWeekSchedule() {
  weeklyProgram.clearAll();
  for (uint8_t zone = 0; zone < PILOTE_ZONE_COUNT; zone++) {
    for (uint8_t day = 0; day < ThermioWeeklyProgram::DaysPerWeek; day++) {
      weeklyProgram.save(zone, day, SCHEDULE_SLOT_0630,
                         deciCToHalfC(SCHEDULE_SETPOINT_0630_DECI_C));
      weeklyProgram.save(zone, day, SCHEDULE_SLOT_1630,
                         deciCToHalfC(SCHEDULE_SETPOINT_1630_DECI_C));
      weeklyProgram.save(zone, day, SCHEDULE_SLOT_2100,
                         deciCToHalfC(SCHEDULE_SETPOINT_2100_DECI_C));
    }
  }
  extEepromUpdate(ThermioStorageLayout::ScheduleMagicAddress,
                  ThermioStorageLayout::ScheduleMagicValue);
  recomputeZoneWorkloads();
  sendPiloteSet();
}

uint8_t scheduleDayIndex() {
  const uint8_t day = weekday();
  return day == 1 ? 6 : (uint8_t)(day - 2);
}

int16_t scheduledSetpointForZone(uint8_t zone,
                                 bool &found,
                                 uint16_t &source) {
  found = false;
  source = NO_PROGRAM_SOURCE;
  if (zone < 1 || zone > PILOTE_ZONE_COUNT) {
    return SETPOINT_NORMAL_DECI_C;
  }
  const uint8_t day = scheduleDayIndex();
  const uint8_t slotOfDay = (uint8_t)((hour() * 60 + minute()) / 15);
  const ThermioWeeklyProgram::Selection selection = weeklyProgram.current(
      zone - 1,
      day,
      slotOfDay,
      deciCToHalfC(SETPOINT_NORMAL_DECI_C),
      true);
  found = selection.found;
  if (selection.found) {
    source = (uint16_t)selection.sourceDay * ThermioWeeklyProgram::SlotsPerDay +
        selection.sourceSlot;
  }
  return halfCToDeciC(selection.targetHalfC);
}

bool nextScheduledSetpointForZone(uint8_t zone,
                                  int16_t &setpointDeciC,
                                  uint16_t &delayMin) {
  if (zone < 1 || zone > PILOTE_ZONE_COUNT) {
    return false;
  }

  ThermioWeeklyProgram::Selection selection;
  uint16_t delaySlots = 0;
  if (!weeklyProgram.next(zone - 1,
                          scheduleDayIndex(),
                          (uint8_t)((hour() * 60 + minute()) / 15),
                          deciCToHalfC(SETPOINT_NORMAL_DECI_C),
                          selection,
                          delaySlots)) {
    return false;
  }
  setpointDeciC = halfCToDeciC(selection.targetHalfC);
  delayMin = delaySlots * 15U;
  return true;
}

bool saveSetpointToSchedule(uint8_t zone, int16_t setpointDeciC) {
  if (zone < 1 || zone > PILOTE_ZONE_COUNT) {
    return false;
  }
  const uint8_t day = scheduleDayIndex();
  const uint8_t slotOfDay = (uint8_t)((hour() * 60 + minute()) / 15);
  const int8_t targetHalfC = deciCToHalfC(setpointDeciC);
  weeklyProgram.save(zone - 1, day, slotOfDay, targetHalfC);
  const bool magicOk = extEepromUpdate(ThermioStorageLayout::ScheduleMagicAddress,
                                       ThermioStorageLayout::ScheduleMagicValue);
  const ThermioWeeklyProgram::Selection stored = weeklyProgram.current(
      zone - 1, day, slotOfDay, targetHalfC, false);
  return magicOk && stored.found && stored.sourceDay == day &&
      stored.sourceSlot == slotOfDay && stored.targetHalfC == targetHalfC;
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

bool updateConsoleTemperature(uint32_t now, bool force) {
  if (!force && (int32_t)(now - nextConsoleTempRefreshAt) < 0) {
    return false;
  }
  nextConsoleTempRefreshAt = now + CONSOLE_TEMP_REFRESH_MS;

  int16_t measured = consoleTempDeciC;
  const bool ok = readAhtTemperatureDeciC(measured);
  consoleTempKnown = ok;
  if (ok) {
    consoleTempDeciC = measured + ahtOffsetDeciC;
  }
  return true;
}

bool doucheActive() {
  return stableMode == MODE_DOUCHE && (int32_t)(millis() - doucheUntil) < 0;
}

int16_t measuredTempForZone(uint8_t zone) {
  const ZoneState *state = zoneStateConst(zone);
  if (zone >= 1 && zone <= PILOTE_ZONE_COUNT && debugForcedTempIsActive(zone)) {
    return debugForcedTempDeciC[zone - 1];
  }
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

uint16_t validatedZonePowerVa(uint16_t powerVa) {
  return powerVa >= MIN_VALID_ZONE_POWER_VA && powerVa <= MAX_VALID_ZONE_POWER_VA ?
      powerVa : FALLBACK_ZONE_POWER_VA;
}

uint8_t responseModeValue() {
  switch (effectiveMode()) {
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

int16_t humidityComfortCorrectionForZone(uint8_t zone) {
  const ZoneState *state = zoneStateConst(zone);
  if (state == nullptr) {
    return 0;
  }
  return ThermioHeatingRegulator::humidityComfortCorrectionDeciC(
      state->hasHumidity,
      state->humidityDeciPct);
}

int16_t regulationSetpointForZone(uint8_t zone) {
  const ZoneState *state = zoneStateConst(zone);
  if (state == nullptr) {
    return 0;
  }
  const ModeValue mode = effectiveMode();
  if (mode != MODE_NORMAL && mode != MODE_PLUS && mode != MODE_MOINS) {
    return state->workSetpointDeciC;
  }
  return state->workSetpointDeciC + humidityComfortCorrectionForZone(zone);
}

bool sondeSetpointApplies(ZoneState &state) {
  const uint32_t currentEpoch = (uint32_t)::now();
  const ThermioSetpointPolicy::Decision decision = ThermioSetpointPolicy::evaluate({
      state.usualSetpointDeciC,
      state.sondeSetpointDeciC,
      state.sondeSetpointStartedAt,
      currentEpoch,
      SONDE_OVERRIDE_DURATION_S,
      state.hasSondeSetpoint,
      state.learningEnabled,
      state.hasUsualProgram,
      state.hasUsualProgram &&
          state.usualProgramSource != state.sondeProgramSourceAtStart});
  if (decision.clearOverride) {
    state.hasSondeSetpoint = false;
  }
  return decision.overrideApplied;
}

int16_t computeCurrentSetpointForZone(uint8_t zone) {
  ZoneState *state = zoneState(zone);
  if (state == nullptr) {
    return 0;
  }
  const ModeValue mode = effectiveMode();
  if (mode == MODE_STOP) {
    return 0;
  }
  if (debugForcedSetpointIsActive(zone)) {
    return debugForcedSetpointDeciC[zone - 1];
  }
  if (mode == MODE_VACANCES) {
    return vacationSetpointDeciC;
  }
  int16_t setpoint = sondeSetpointApplies(*state) ?
      state->sondeSetpointDeciC :
      state->usualSetpointDeciC;
  if (mode == MODE_PLUS || mode == MODE_MOINS) {
    setpoint += (int16_t)plusMinusAppliedOffsetC * 10;
  } else if (doucheActive()) {
    setpoint += (zone == ZONE_SDB ? DOUCHE_SDB_DELTA_C : DOUCHE_OTHER_DELTA_C) * 10;
  }
  return setpoint;
}

void refreshZoneSetpoints() {
  for (uint8_t i = 0; i < PILOTE_ZONE_COUNT; i++) {
    bool programFound = false;
    uint16_t programSource = NO_PROGRAM_SOURCE;
    zones[i].usualSetpointDeciC = scheduledSetpointForZone(
        i + 1, programFound, programSource);
    zones[i].hasUsualProgram = programFound;
    zones[i].usualProgramSource = programSource;
    zones[i].currentSetpointDeciC = computeCurrentSetpointForZone(i + 1);
    zones[i].workSetpointDeciC = zones[i].currentSetpointDeciC;

    int16_t nextSetpointDeciC;
    uint16_t nextDelayMin;
    if (effectiveMode() == MODE_NORMAL &&
        zones[i].currentSetpointDeciC == zones[i].usualSetpointDeciC &&
        nextScheduledSetpointForZone(i + 1, nextSetpointDeciC, nextDelayMin)) {
      zones[i].workSetpointDeciC = heatingRegulator.planPreheat(
          measuredTempForZone(i + 1),
          zones[i].currentSetpointDeciC,
          nextSetpointDeciC,
          nextDelayMin).workTargetDeciC;
    }
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
      regulationSetpointForZone(zone),
      installedPowerForZone(zone),
      doorOpenForZone(zone),
      effectiveMode() == MODE_STOP);

  state->workload = (uint8_t)decision.workload;
  state->lastEffectiveHoldBtuPerHour = decision.learnedHoldBtuPerHour;
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
    zones[i].sondeSetpointStartedAt = 0;
    zones[i].usualSetpointDeciC = SETPOINT_NORMAL_DECI_C;
    zones[i].currentSetpointDeciC = SETPOINT_NORMAL_DECI_C;
    zones[i].workSetpointDeciC = SETPOINT_NORMAL_DECI_C;
    zones[i].measuredTempDeciC = FALLBACK_MEASURED_TEMP_DECI_C;
    zones[i].sondeSetpointDeciC = SETPOINT_NORMAL_DECI_C;
    zones[i].usualProgramSource = NO_PROGRAM_SOURCE;
    zones[i].sondeProgramSourceAtStart = NO_PROGRAM_SOURCE;
    zones[i].humidityDeciPct = 0;
    zones[i].lastRequestedBtuPerHour = 0;
    zones[i].lastMaintenanceBtuPerHour = 0;
    zones[i].lastEffectiveHoldBtuPerHour = 0;
    zones[i].hasTemperature = false;
    zones[i].hasSondeSetpoint = false;
    zones[i].hasUsualProgram = false;
    zones[i].hasHumidity = false;
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
    zones[i].doorBypassed = false;
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

void clearAllAssociationEntries() {
  associationActive = false;
  associationConfirmActive = false;
  for (uint8_t i = 0; i < RF_MAX_ASSOCIATED_SLAVES; i++) {
    clearAssociationEntry(i);
  }
  for (uint8_t i = 0; i < PILOTE_ZONE_COUNT; i++) {
    zones[i].sondeSeen = false;
    zones[i].doorSeen = false;
    zones[i].doorOpen = false;
    zones[i].sondeLowBattery = false;
    zones[i].doorLowBattery = false;
    zones[i].sondeMissing = false;
    zones[i].doorMissing = false;
  }
}

int findAssociatedSlave(uint16_t nodeId) {
  for (uint8_t i = 0; i < RF_MAX_ASSOCIATED_SLAVES; i++) {
    if (associatedSlaves[i].nodeId == nodeId) {
      return i;
    }
  }
  return -1;
}

int findAssociationSlotForZone(uint8_t zone, uint8_t deviceType, int preferredIndex) {
  uint8_t zoneCount = 0;
  int firstZoneIndex = -1;
  int sameTypeZoneIndex = -1;
  for (uint8_t i = 0; i < RF_MAX_ASSOCIATED_SLAVES; i++) {
    if (associatedSlaves[i].zone == zone) {
      zoneCount++;
      if (firstZoneIndex < 0) {
        firstZoneIndex = i;
      }
      if (sameTypeZoneIndex < 0 && associatedSlaves[i].deviceType == deviceType) {
        sameTypeZoneIndex = i;
      }
    }
  }

  if (preferredIndex >= 0 &&
      (associatedSlaves[preferredIndex].zone == zone || zoneCount < RF_ASSOC_SLAVES_PER_ZONE)) {
    return preferredIndex;
  }
  if (sameTypeZoneIndex >= 0) {
    return sameTypeZoneIndex;
  }
  for (uint8_t i = 0; i < RF_MAX_ASSOCIATED_SLAVES; i++) {
    if (associatedSlaves[i].nodeId == ThermioRfFrame::BroadcastId) {
      return i;
    }
  }
  if (zoneCount >= RF_ASSOC_SLAVES_PER_ZONE && firstZoneIndex >= 0) {
    return firstZoneIndex;
  }
  return 0;
}

uint8_t zoneForSlave(uint16_t nodeId) {
  if (associationActive && associationNodeId == nodeId) {
    return associationZone;
  }

  const int index = findAssociatedSlave(nodeId);
  return index >= 0 ? associatedSlaves[index].zone : 0;
}

uint8_t persistedZoneForSlave(uint16_t nodeId) {
  const int index = findAssociatedSlave(nodeId);
  return index >= 0 ? associatedSlaves[index].zone : 0;
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
  const int index = findAssociationSlotForZone(associationZone, associationDeviceType, preferredIndex);
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

bool associationWindowOpen(uint32_t now) {
  return now < RF_ASSOCIATION_WINDOW_MS ||
      (int32_t)(now - associationWindowUntil) < 0;
}

bool sourceAccepted(uint16_t sourceId) {
  const bool assocWindowOpen = associationWindowOpen(millis());
  const bool sourceKnown =
      findAssociatedSlave(sourceId) >= 0 ||
      (associationActive && associationNodeId == sourceId);
  return sourceId != consoleId &&
      (sourceKnown || assocWindowOpen);
}

bool targetAccepted(uint16_t targetId) {
  return targetId == consoleId || associationWindowOpen(millis());
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
      !sourceAccepted(header.sourceId) ||
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
  response.assignedZone = persistedZoneForSlave(targetId);
  response.dateTime[0] = (uint8_t)(year() - 2000);
  response.dateTime[1] = (uint8_t)month();
  response.dateTime[2] = (uint8_t)day();
  response.dateTime[3] = (uint8_t)hour();
  response.dateTime[4] = (uint8_t)minute();
  response.dateTime[5] = (uint8_t)second();
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
  response.vacationSetpointDeciC = vacationSetpointDeciC;
  response.hasNextSetpoint = responseZone != nullptr &&
      responseZone->learningEnabled &&
      nextScheduledSetpointForZone(
          response.assignedZone,
          response.nextSetpointDeciC,
          response.nextSetpointDelayMin);
  if (!response.hasNextSetpoint) {
    response.nextSetpointDeciC = response.currentSetpointDeciC;
    response.nextSetpointDelayMin = 0;
  }
  ThermioRfFrame::encodeResponsePayload(packet + ThermioRfFrame::HeaderLen, response);
  return ThermioRfFrame::HeaderLen + ThermioRfFrame::ResponsePayloadLen;
}

void sendAckBurst(uint16_t targetId, uint8_t ackSequence) {
  delay(RF_ACK_REPLY_DELAY_MS);
  for (uint8_t ack = 0; ack < RF_ACK_TX_COUNT; ack++) {
    setPixel(LED_CENTRE, rgb(255, 110, 0));
    showLeds();
    uint8_t packet[ThermioRfFrame::MaxPacketLen] = {0};
    const uint8_t length = buildResponsePacket(packet, targetId, rfSequence++, ackSequence);
    radio.writePacket(packet, length);
    radio.waitTxComplete(RF_TX_COMPLETE_TIMEOUT_MS);
    if (ack + 1 < RF_ACK_TX_COUNT) {
      delay(RF_ACK_TX_GAP_MS);
    }
  }
  setPixel(LED_CENTRE, centerStatusColor(millis()));
  showLeds();
}

void startRfReceivedBlink(uint8_t zone, uint8_t deviceType) {
  (void)zone;
  rfBlinkActive = true;
  rfBlinkStep = 0;
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
    setPixel(LED_CENTRE, centerStatusColor(millis()));
    return;
  }

  const bool ledOn = (rfBlinkStep % 2) == 0;
  setPixel(LED_CENTRE, ledOn ? rfBlinkColor : centerStatusColor(millis()));
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
    setPixel(ledForZone(associationConfirmZone), rgb(255, 255, 255));
    return;
  }

  if (!associationActive) {
    return;
  }

  const bool blinkOn = (millis() % 500) < 250;
  for (uint8_t zone = 1; zone <= RF_ASSOC_ZONE_COUNT; zone++) {
    setPixel(ledForZone(zone), zone == associationZone && blinkOn ? rgb(255, 255, 255) : rgb(0, 0, 0));
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
  if (runMode == RUN_FAILSAFE) {
    return rgb(0, 0, 0);
  }
  if (associationWindowOpen(now)) {
    return rgb(255, 255, 255);
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
  plusMinusAppliedOffsetC = plusMinusOffsetC;
  plusMinusApplyPending = false;
  plusMinusApplyAt = 0;
  plusMinusFeedbackUntil = stableMode == MODE_PLUS || stableMode == MODE_MOINS ?
      millis() + PLUS_MINUS_FEEDBACK_MS : 0;
  if (stableMode == MODE_DOUCHE) {
    doucheUntil = millis() + DOUCHE_DURATION_MS;
  }
  recomputeZoneWorkloads();
  doucheWasActive = doucheActive();
}

uint32_t colorForMode(ModeValue mode) {
  if ((mode == MODE_PLUS || mode == MODE_MOINS) &&
      plusMinusOffsetC != 0 &&
      (int32_t)(millis() - plusMinusFeedbackUntil) < 0) {
    if (((millis() / 250UL) % 2UL) != 0) {
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
    Serial.print(workloadForZone(logicalZoneForPiloteZone(i + 1)));
  }
  Serial.println();
}

void setFailsafeWorkloads(uint8_t value) {
  for (uint8_t i = 0; i < PILOTE_ZONE_COUNT; i++) {
    zones[i].workload = value;
  }
}

void sendFailsafeWorkloads(bool heating) {
  setFailsafeWorkloads(heating ? FAILSAFE_WORKLOAD : 0);
  sendPiloteSet();
}

void enterFailsafeMode(bool stopRf) {
  runMode = RUN_FAILSAFE;
  associationActive = false;
  associationConfirmActive = false;
  rfBlinkActive = false;
  userDeltaFeedbackActive = false;
  failsafeCommand = FAILSAFE_HOLD;
  failsafeTargetDeciC = consoleTempKnown ? consoleTempDeciC : FALLBACK_MEASURED_TEMP_DECI_C;
  failsafeHeating = false;
  if (stopRf && rfStarted) {
    SPI.endTransaction();
    radio.sleep();
    rfStarted = false;
  }
  sendFailsafeWorkloads(false);
  if (DEBUG_ENABLED) {
    debugPrefix();
    Serial.print(F("FAILSAFE TEMP="));
    debugPrintDeciC(failsafeTargetDeciC);
    Serial.println();
  }
}

void refreshFailsafeHeating() {
  bool nextHeating = false;
  const int16_t measured = consoleTempKnown ? consoleTempDeciC : FALLBACK_MEASURED_TEMP_DECI_C;

  if (failsafeCommand == FAILSAFE_HEAT) {
    nextHeating = measured < FAILSAFE_PLUS_LIMIT_DECI_C;
  } else if (failsafeCommand == FAILSAFE_HOLD && failsafeHeating) {
    nextHeating = measured < failsafeTargetDeciC + FAILSAFE_HYSTERESIS_DECI_C;
  } else if (failsafeCommand == FAILSAFE_HOLD) {
    nextHeating = measured < failsafeTargetDeciC - FAILSAFE_HYSTERESIS_DECI_C;
  }

  if (nextHeating != failsafeHeating) {
    failsafeHeating = nextHeating;
    sendFailsafeWorkloads(failsafeHeating);
  }
}

bool isPlusMinusSelectionMode(ModeValue mode) {
  return mode == MODE_NORMAL || mode == MODE_PLUS || mode == MODE_MOINS;
}

bool isFailsafeControlMode(ModeValue mode) {
  return mode == MODE_NORMAL || mode == MODE_PLUS || mode == MODE_MOINS || mode == MODE_STOP;
}

FailsafeCommand failsafeCommandFromMode(ModeValue mode) {
  if (mode == MODE_PLUS) {
    return FAILSAFE_HEAT;
  }
  if (mode == MODE_STOP) {
    return FAILSAFE_STOP;
  }
  if (mode == MODE_MOINS) {
    return FAILSAFE_COOL_DOWN;
  }
  return FAILSAFE_HOLD;
}

ModeValue modeForFailsafeCommand() {
  if (failsafeCommand == FAILSAFE_HEAT) {
    return MODE_PLUS;
  }
  if (failsafeCommand == FAILSAFE_COOL_DOWN) {
    return MODE_MOINS;
  }
  if (failsafeCommand == FAILSAFE_STOP) {
    return MODE_STOP;
  }
  return MODE_NORMAL;
}

void applyFailsafeModeSelection(ModeValue mode) {
  if (!isFailsafeControlMode(mode)) {
    return;
  }

  const FailsafeCommand nextCommand = failsafeCommandFromMode(mode);
  if (nextCommand == failsafeCommand) {
    return;
  }

  failsafeCommand = nextCommand;
  if (failsafeCommand == FAILSAFE_HOLD) {
    failsafeTargetDeciC = consoleTempKnown ? consoleTempDeciC : FALLBACK_MEASURED_TEMP_DECI_C;
  }
  refreshFailsafeHeating();
}

void schedulePlusMinusApply(uint32_t now) {
  plusMinusApplyPending = true;
  plusMinusApplyAt = now + PLUS_MINUS_APPLY_DELAY_MS;
}

void applyPlusMinusIfDue(uint32_t now) {
  if (!plusMinusApplyPending || (int32_t)(now - plusMinusApplyAt) < 0) {
    return;
  }

  plusMinusApplyPending = false;
  plusMinusAppliedOffsetC =
      (stableMode == MODE_PLUS || stableMode == MODE_MOINS) ? plusMinusOffsetC : 0;
  recomputeZoneWorkloads();
  sendPiloteSet();
}

void applyModeSelection(ModeValue mode) {
  const ModeValue previousMode = stableMode;
  const uint32_t now = millis();

  if (runMode == RUN_NORMAL &&
      mode == MODE_DOUCHE &&
      associationWindowOpen(now)) {
    if (DEBUG_ENABLED) {
      debugPrefix();
      Serial.println(F("MODE DOUCHE -> FAILSAFE"));
    }
    enterFailsafeMode(true);
    return;
  }

  if (runMode == RUN_FAILSAFE) {
    applyFailsafeModeSelection(mode);
    return;
  }

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
    enteredNormalAt = now;
    if (previousMode != MODE_PLUS && previousMode != MODE_MOINS) {
      plusMinusOffsetC = 0;
    }
  } else if (mode == MODE_PLUS || mode == MODE_MOINS) {
    const int8_t direction = mode == MODE_PLUS ? MODE_DELTA_STEP_C : -MODE_DELTA_STEP_C;
    const bool additiveReturn =
        previousMode == MODE_NORMAL &&
        lastModeBeforeNormal == mode &&
        (uint32_t)(now - enteredNormalAt) <= PLUS_MINUS_NORMAL_RETURN_MS;
    if (additiveReturn) {
      plusMinusOffsetC += direction;
    } else {
      plusMinusOffsetC = direction;
    }
    lastModeBeforeNormal = mode;
    enteredNormalAt = now;
    plusMinusFeedbackUntil = now + PLUS_MINUS_FEEDBACK_MS;
  } else {
    plusMinusOffsetC = 0;
    plusMinusAppliedOffsetC = 0;
    plusMinusApplyPending = false;
    lastModeBeforeNormal = mode;
    enteredNormalAt = now;
  }

  if (mode == MODE_DOUCHE && previousMode != MODE_DOUCHE) {
    doucheUntil = now + DOUCHE_DURATION_MS;
  }

  if (isPlusMinusSelectionMode(previousMode) && isPlusMinusSelectionMode(mode)) {
    schedulePlusMinusApply(now);
    return;
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
  if ((uint32_t)(now - modeChangedAt) < MODE_DEBOUNCE_MS) {
    return;
  }
  if (runMode == RUN_NORMAL && rawMode == stableMode) {
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

  const bool firstClockSync = !clockSet;
  const uint8_t month = twoDigitsAt(text, 5);
  const uint8_t day = twoDigitsAt(text, 8);
  const uint8_t hour = twoDigitsAt(text, 11);
  const uint8_t minute = twoDigitsAt(text, 14);
  const uint8_t second = twoDigitsAt(text, 17);

  setTime(hour, minute, second, day, month, 2000 + twoDigitsAt(text, 2));
  clockSet = true;
  lastClockSyncAt = millis();
  debugTimeSpeedLastMs = lastClockSyncAt;
  debugTimeSpeedExtraMs = 0;
  if (hour == CLOCK_FORCED_RESYNC_HOUR) {
    lastForcedClockSyncDayKey = timestampDayKey(text);
  }
  if (firstClockSync || millis() < CLOCK_STARTUP_SYNC_WINDOW_MS) {
    resetAutoVacanceReference((uint32_t)::now());
  }
  applyCurrentLedBrightness();
  return true;
}

void updateDebugTimeSpeed(uint32_t nowMs) {
  if (debugTimeSpeed <= 1) {
    debugTimeSpeedLastMs = nowMs;
    debugTimeSpeedExtraMs = 0;
    return;
  }

  const uint32_t elapsedMs = nowMs - debugTimeSpeedLastMs;
  debugTimeSpeedLastMs = nowMs;
  debugTimeSpeedExtraMs += elapsedMs * (uint32_t)(debugTimeSpeed - 1);
  const uint32_t extraSeconds = debugTimeSpeedExtraMs / 1000UL;
  if (extraSeconds == 0) {
    return;
  }

  debugTimeSpeedExtraMs %= 1000UL;
  setTime(now() + extraSeconds);
}

bool clockResyncAllowed() {
  return millis() < CLOCK_STARTUP_SYNC_WINDOW_MS || !clockSet ||
      (uint32_t)(millis() - lastClockSyncAt) >= CLOCK_RESYNC_INTERVAL_MS;
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
      setDebugForcedSetpointActive(i + 1, active);
      debugForcedSetpointDeciC[i] = setpointDeciC;
    }
  } else {
    setDebugForcedSetpointActive(zone, active);
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

void applyDebugTemperature(uint8_t zone, bool active, int16_t tempDeciC) {
  if (zone == 0) {
    for (uint8_t i = 0; i < PILOTE_ZONE_COUNT; i++) {
      setDebugForcedTempActive(i + 1, active);
      debugForcedTempDeciC[i] = tempDeciC;
    }
  } else {
    setDebugForcedTempActive(zone, active);
    debugForcedTempDeciC[zone - 1] = tempDeciC;
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
  Serial.print(currentLedBrightness());
  Serial.print(F(" SPEED=X"));
  Serial.println(debugTimeSpeed);
}

bool parseDebugTimeSpeed(const char *text, uint16_t &speed) {
  if (*text == 'x' || *text == 'X') {
    text++;
  }
  if (*text < '0' || *text > '9') {
    return false;
  }

  const long value = atol(text);
  if (value < 1 || value > 240) {
    return false;
  }
  speed = (uint16_t)value;
  return true;
}

void applyDebugTimeSpeed(uint16_t speed) {
  updateDebugTimeSpeed(millis());
  debugTimeSpeed = speed;
  debugTimeSpeedLastMs = millis();
  debugTimeSpeedExtraMs = 0;
  debugPrefix();
  Serial.print(F("TIME SPEED X"));
  Serial.println(debugTimeSpeed);
}

void debugStartAssociationWindow() {
  associationWindowUntil = millis() + RF_ASSOCIATION_WINDOW_MS;
  associationActive = false;
  associationConfirmActive = false;
  debugLine(F("ASSOC WINDOW START 180S"));
}

void debugClearAssociations() {
  clearAllAssociationEntries();
  recomputeZoneWorkloads();
  sendPiloteSet();
  debugLine(F("ASSOC CLEARED"));
  debugPrintAssociations();
}

bool handleDebugCommand(const char *line) {
  if (strcmp(line, "DBG?") == 0 || strcmp(line, "DBG") == 0) {
    debugPrintOverview();
    return true;
  }
  if (strcmp(line, "DBG WATCH") == 0) {
    debugPrintWatch();
    return true;
  }
  if (strcmp(line, "DBG ASSOC") == 0) {
    debugPrintAssociations();
    return true;
  }
  if (strcmp(line, "DBG ASSOC START") == 0) {
    debugStartAssociationWindow();
    return true;
  }
  if (strcmp(line, "DBG ASSOC CLEAR") == 0) {
    debugClearAssociations();
    return true;
  }
  if (strcmp(line, "DBG TIME?") == 0 || strcmp(line, "DBG TIME") == 0) {
    debugPrintTime();
    return true;
  }
  if (strncmp(line, "DBG TIME SPEED ", 15) == 0 ||
      strncmp(line, "DBG SPEED ", 10) == 0) {
    const char *arg = line[4] == 'T' ? line + 15 : line + 10;
    uint16_t speed = 1;
    if (parseDebugTimeSpeed(arg, speed)) {
      applyDebugTimeSpeed(speed);
      debugPrintTime();
    }
    return true;
  }
  if (strncmp(line, "DBG TIME ", 9) == 0) {
    if (parseTimestamp(line + 9)) {
      debugPrintTime();
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
    }
    return true;
  }
  if (strncmp(line, "DBG TEMP ", 9) == 0) {
    const char *arg = line + 9;
    uint8_t zone = 0;
    if (arg[0] == 'Z' && arg[1] >= '1' && arg[1] <= '4' && arg[2] == ' ') {
      zone = arg[1] - '0';
      arg += 3;
    }
    if (strcmp(arg, "OFF") == 0 || strcmp(arg, "off") == 0) {
      applyDebugTemperature(zone, false, FALLBACK_MEASURED_TEMP_DECI_C);
      return true;
    }
    int16_t tempDeciC = 0;
    if (parseDebugSetpointDeciC(arg, tempDeciC)) {
      applyDebugTemperature(zone, true, tempDeciC);
    }
    return true;
  }
  if (strncmp(line, "DBG PROG ", 9) == 0) {
    const uint8_t zone = debugZoneFromText(line + 9);
    if (zone > 0) {
      debugPrintProgram(zone);
    }
    return true;
  }
  if (strncmp(line, "DBG Z", 5) == 0) {
    const uint8_t zone = debugZoneFromText(line + 4);
    if (zone > 0) {
      debugPrintZoneState(zone);
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
    const uint8_t piloteZone = line[1] - '0';
    const uint8_t logicalZone = logicalZoneForPiloteZone(piloteZone);
    ZoneState *state = zoneState(logicalZone);
    if (state != nullptr) {
      state->powerVa = validatedZonePowerVa((uint16_t)atoi(line + 13));
      if (DEBUG_ENABLED) {
        debugPrefix();
        Serial.print(F("PILOTE Z"));
        Serial.print(piloteZone);
        Serial.print(F(" -> Z"));
        Serial.print(logicalZone);
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
  if (zones[zoneIndex].doorOpen && zones[zoneIndex].doorBypassed && zones[zoneIndex].workload > 0) {
    return phaseMs < 3500UL ? rgb(255, 180, 0) : rgb(0, 80, 255);
  }
  if (doorOpenForZone(zoneIndex + 1)) {
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
  const bool previousDoorBypassed = state.doorBypassed;
  const bool doorStateChanged = previousDoorOpen != report.doorOpen;
  state.doorSeen = true;
  state.doorMissing = false;
  state.lastDoorReportAt = millis();
  state.doorLowBattery = lowBatteryForType(report.deviceType, report.batteryMv);
  state.doorOpen = report.doorOpen;

  if (!state.doorOpen || doorStateChanged) {
    state.doorBypassed = false;
  }

  if (DEBUG_ENABLED && previousDoorOpen != state.doorOpen) {
    debugPrefix();
    Serial.print(F("DOOR Z"));
    Serial.print(zone);
    Serial.print(' ');
    Serial.println(state.doorOpen ? F("OPEN") : F("CLOSED"));
  }
  if (DEBUG_ENABLED && previousDoorBypassed && !state.doorBypassed) {
    debugPrefix();
    Serial.print(F("DOOR Z"));
    Serial.print(zone);
    Serial.println(F(" BYPASS_CLEARED"));
  }
}

bool applySondeReportToZone(ZoneState &state,
                            const ThermioRfFrame::Report &report,
                            uint8_t zone,
                            uint32_t now) {
  bool regulationChanged = false;
  const bool resettingProgram =
      report.adminRequest == ThermioRfFrame::AdminClearZoneLearning ||
      report.adminRequest == ThermioRfFrame::AdminClearAllLearning;
  state.sondeSeen = true;
  state.sondeMissing = false;
  state.lastSondeReportAt = now;
  state.sondeLowBattery = lowBatteryForType(report.deviceType, report.batteryMv);

  if (report.presenceCount > 0) {
    state.presenceSeen = true;
    state.lastPresenceAt = (uint32_t)::now();
    noteApartmentPresence();
  }

  if (!resettingProgram && state.learningEnabled != report.learningEnabled) {
    state.learningEnabled = report.learningEnabled;
    saveLearningEnabledToEeprom();
    regulationChanged = true;
  }

  if (!resettingProgram && report.hasSetpoint) {
    const bool explicitUserSetpoint =
        report.userDeltaSteps != 0 ||
        report.adminRequest == ThermioRfFrame::AdminSaveCurrentSetpoint;
    if (explicitUserSetpoint && state.doorOpen && !state.doorBypassed) {
      state.doorBypassed = true;
      regulationChanged = true;
      if (DEBUG_ENABLED) {
        debugPrefix();
        Serial.print(F("DOOR Z"));
        Serial.print(zone);
        Serial.println(F(" BYPASSED_BY_SONDE"));
      }
    }
    if ((explicitUserSetpoint || !state.learningEnabled) &&
        (!state.hasSondeSetpoint || state.sondeSetpointDeciC != report.setpointDeciC)) {
      state.sondeSetpointDeciC = report.setpointDeciC;
      state.hasSondeSetpoint = true;
      regulationChanged = true;
    }
    if (explicitUserSetpoint) {
      state.sondeSetpointStartedAt = (uint32_t)::now();
      state.sondeProgramSourceAtStart = state.usualProgramSource;
    }
  }

  if (report.tempCount > 0) {
    const int16_t nextTempDeciC = report.temperaturesDeciC[report.tempCount - 1];
    if (state.hasTemperature && state.hasRegulationDecision) {
      heatingRegulator.observe(
          zone - 1,
          state.measuredTempDeciC,
          nextTempDeciC,
          installedPowerForZone(zone),
          state.lastRequestedBtuPerHour,
          state.lastMaintenanceBtuPerHour,
          state.learningEnabled);
    }
    state.measuredTempDeciC = nextTempDeciC;
    state.hasTemperature = true;
    regulationChanged = true;
  }
  if (state.hasHumidity != report.hasHumidity ||
      (report.hasHumidity && state.humidityDeciPct != report.humidityDeciPct)) {
    state.hasHumidity = report.hasHumidity;
    state.humidityDeciPct = report.hasHumidity ? report.humidityDeciPct : 0;
    regulationChanged = true;
  }

  return regulationChanged;
}

bool resetZoneLearningState(uint8_t zone) {
  ZoneState *state = zoneState(zone);
  if (state == nullptr) {
    return false;
  }

  const bool cleared = weeklyProgram.clearZone(zone - 1);
  state->sondeSetpointStartedAt = 0;
  state->hasSondeSetpoint = false;
  state->hasRegulationDecision = false;
  state->learningEnabled = true;
  heatingRegulator.resetZone(zone - 1);
  saveLearningEnabledToEeprom();
  recomputeZoneWorkloads();
  sendPiloteSet();
  return cleared;
}

bool resetAllLearningState() {
  const bool cleared = weeklyProgram.clearAll();
  for (uint8_t zone = 1; zone <= PILOTE_ZONE_COUNT; zone++) {
    ZoneState *state = zoneState(zone);
    if (state != nullptr) {
      state->sondeSetpointStartedAt = 0;
      state->hasSondeSetpoint = false;
      state->hasRegulationDecision = false;
      state->learningEnabled = true;
    }
  }
  heatingRegulator.reset();
  saveLearningEnabledToEeprom();
  recomputeZoneWorkloads();
  sendPiloteSet();
  return cleared;
}

void handleAdminRequest(uint8_t zone, const ThermioRfFrame::Report &report) {
  if (report.deviceType != ThermioRfFrame::DeviceSonde) {
    return;
  }

  if (report.adminRequest == ThermioRfFrame::AdminClearZoneLearning) {
    const bool cleared = resetZoneLearningState(zone);
    if (DEBUG_ENABLED) {
      debugPrefix();
      Serial.print(F("ADMIN RESET_ZONE Z"));
      Serial.print(zone);
      Serial.println(cleared ? F(" OK") : F(" KO"));
    }
  } else if (report.adminRequest == ThermioRfFrame::AdminClearAllLearning) {
    const bool cleared = resetAllLearningState();
    if (DEBUG_ENABLED) {
      debugPrefix();
      Serial.println(cleared ? F("ADMIN RESET_ALL OK") : F("ADMIN RESET_ALL KO"));
    }
  } else if (report.adminRequest == ThermioRfFrame::AdminLoadTypicalWeek) {
    if (DEBUG_ENABLED) {
      debugPrefix();
      Serial.println(F("ADMIN LOAD_TYPICAL_WEEK"));
    }
    loadTypicalWeekSchedule();
  } else if (report.adminRequest == ThermioRfFrame::AdminSaveCurrentSetpoint && report.hasSetpoint) {
    const bool saved = saveSetpointToSchedule(zone, report.setpointDeciC);
    if (DEBUG_ENABLED) {
      debugPrefix();
      Serial.print(F("ADMIN SAVE_SETPOINT Z"));
      Serial.print(zone);
      Serial.print(saved ? F(" OK D") : F(" KO D"));
      Serial.print(scheduleDayIndex() + 1);
      Serial.print('=');
      debugPrintDeciC(report.setpointDeciC);
      Serial.println();
    }
    recomputeZoneWorkloads();
    sendPiloteSet();
  } else if (report.adminRequest == ThermioRfFrame::AdminSetVacationSetpoint &&
             report.hasVacationSetpoint) {
    if (DEBUG_ENABLED) {
      debugPrefix();
      Serial.print(F("ADMIN VAC_SET="));
      debugPrintDeciC(report.vacationSetpointDeciC);
      Serial.println();
    }
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
    Serial.print(report.userDeltaSteps);
    Serial.print(F(" ADMIN="));
    Serial.print(report.adminRequest);
    Serial.print(F(" AHT="));
    if (report.hasAhtOffset) {
      Serial.print(report.ahtOffsetDeciC);
    } else {
      Serial.print(F("NA"));
    }
    Serial.print(F(" VAC="));
    if (report.hasVacationSetpoint) {
      debugPrintDeciC(report.vacationSetpointDeciC);
    } else {
      Serial.print(F("NA"));
    }
    Serial.println();
  }

  updateAhtOffsetFromReport(report);
  updateVacationSetpointFromReport(report);
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

  bool pairingReportForActiveAssociation = false;
  if (!duplicate) {
    hasLastReportSequence = true;
    lastReportSequence = reportSequence;
    lastReportSourceId = lastPacketSourceId;

    const bool assocWindowOpen = associationWindowOpen(millis());
    const bool adminPairAllowed =
        lastReport.adminRequest == ThermioRfFrame::AdminPair &&
        (assocWindowOpen ||
         (associationActive && associationNodeId == lastPacketSourceId));

    if (findAssociatedSlave(lastPacketSourceId) < 0 &&
        assocWindowOpen) {
      beginOrRefreshAssociation(lastPacketSourceId, lastReport.deviceType);
    }
    if (!associationActive &&
        adminPairAllowed) {
      beginOrRefreshAssociation(lastPacketSourceId, lastReport.deviceType);
    }
    if (associationActive &&
        associationNodeId == lastPacketSourceId &&
        adminPairAllowed) {
      if (lastReport.pairZoneRequest >= 1 && lastReport.pairZoneRequest <= RF_ASSOC_ZONE_COUNT) {
        associationZone = lastReport.pairZoneRequest;
        associationSaveAt = millis() + RF_ASSOCIATION_SAVE_DELAY_MS;
      }
    }
    pairingReportForActiveAssociation =
        associationActive &&
        associationNodeId == lastPacketSourceId &&
        adminPairAllowed;
    if (!pairingReportForActiveAssociation) {
      if (lastReport.adminRequest != ThermioRfFrame::AdminPair || adminPairAllowed) {
        updateZoneStateFromReport(lastPacketSourceId, lastReport);
      }
    }
  }

  sendAckBurst(lastPacketSourceId, reportSequence);
  if (!duplicate && !pairingReportForActiveAssociation) {
    startRfReceivedBlink(zoneForSlave(lastPacketSourceId), lastReport.deviceType);
  }
  radio.strobeRx();
}

void setup() {
  wdt_disable();

  for (const ModeInput &input : modeInputs) {
    pinMode(input.pin, INPUT);
  }

  Serial.begin(PILOTE_SERIAL_BAUD);
  leds.setBrightness(LED_BRIGHTNESS_MIN);
  leds.begin();
  leds.clear();
  setPixel(LED_MODE, rgb(255, 255, 255));
  showLeds();

  Wire.begin();
  detectExternalEeprom();
  handleFactoryResetBootSequence();
  setPixel(LED_MODE, rgb(255, 255, 255));
  showLeds();

  loadOrCreateConsoleId();
  loadAssociationTableFromEeprom();
  loadAhtOffsetFromEeprom();
  loadVacationSetpointFromEeprom();
  initializeZoneStates();
  loadScheduleFromEeprom();
  initializeHeatingState();
  loadLearningEnabledFromEeprom();
  updateConsoleTemperature(millis(), true);
  initializeModeSelection();
  recomputeZoneWorkloads();
  if (stableMode == MODE_DOUCHE) {
    enterFailsafeMode(false);
    if (!waitForPiloteAtBoot()) {
      consoleHalted = true;
      consoleHaltBlink = false;
      return;
    }
    return;
  }

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
  rfStarted = true;
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
  updateDebugTimeSpeed(now);
  const bool regulationRefreshDue = updateConsoleTemperature(now, false);
  updateModeInput();
  if (runMode == RUN_FAILSAFE) {
    refreshFailsafeHeating();
    renderHeatingStateLeds();
    setPixel(LED_MODE, colorForMode(modeForFailsafeCommand()));
    setPixel(LED_CENTRE, centerStatusColor(now));
    showLeds();
    delay(10);
    return;
  }

  updateHeatingHistory(now);
  updateMissingDeviceStates(now);
  if (regulationRefreshDue) {
    recomputeZoneWorkloads();
    sendPiloteSet();
  }
  updateRf();
  savePendingAssociationIfDue();

  updateAutoVacance(now);
  refreshTimedModeEffects();
  applyPlusMinusIfDue(now);
  renderHeatingStateLeds();
  setPixel(LED_MODE, colorForMode(effectiveMode()));
  setPixel(LED_CENTRE, centerStatusColor(now));
  updateRfReceivedBlink();
  applyUserDeltaFeedbackLed();
  updateAssociationLeds();
  showLeds();
  delay(10);
}
