/*
  Thermonuino - module pilote TIC

  Carte cible:
    - ATmega328P 5 V, horloge interne 8 MHz
    - liaison console sur Serial materiel PD0/PD1, 9600 bauds
    - TIC Linky standard sur SoftwareSerial, 9600 bauds, 7E1 lu en 8N1

  Protocole console -> pilote:
    DC_LENGTH=30
    Z1_WORKLOAD=0
    Z2_WORKLOAD=255
    Z3_WORKLOAD=127
    Z4_WORKLOAD=64

  Commandes pratiques de test:
    SET 30 0 255 127 64
    AUTO
    STATUS?
    LEARN
    PING
    ALL_OFF
    ALL_ON
*/

#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>
#include <SoftwareSerial.h>
#include <string.h>
#include <stdlib.h>

const uint8_t PIN_LED_CHAIN_DATA = 5;  // PCINT21 / PD5 / D5
const uint8_t PIN_BTN_STATUS = 3;      // PCINT19 / PD3 / appui vers GND
const uint8_t PIN_BTN_MUX1 = A5;       // PCINT13 / SWZ1, SWZ2
const uint8_t PIN_BTN_MUX2 = A4;       // PCINT12 / SWZ3, SWZ4
const uint8_t PIN_LINKY_RX = 6;        // PCINT22 / PD6
const uint8_t PIN_TIC_UNUSED_TX = 4;   // requis par SoftwareSerial, non cable
const uint8_t PIN_HEARTBEAT = 2;       // PCINT18 / PD2

const uint8_t PIN_CMDZ1 = A3;          // PCINT11 / PC3
const uint8_t PIN_CMDZ2 = A2;          // PCINT10 / PC2
const uint8_t PIN_CMDZ3 = A1;          // PCINT9  / PC1
const uint8_t PIN_CMDZ4 = A0;          // PCINT8  / PC0

const uint8_t ZONE_COUNT = 4;
const uint8_t LED_COUNT = 6;
const uint8_t LED_ZONE_INDEX[ZONE_COUNT] = {3, 2, 1, 0}; // Z1, Z2, Z3, Z4
const uint8_t LED_LINKY_INDEX = 4;
const uint8_t LED_STATUS_INDEX = 5;
const uint8_t ZONE_CMD_PINS[ZONE_COUNT] = {PIN_CMDZ1, PIN_CMDZ2, PIN_CMDZ3, PIN_CMDZ4};

const unsigned long BAUD_RATE = 9600;
const unsigned long DUTY_SLOT_MS = 10000UL;
const unsigned long DEFAULT_CYCLE_MINUTES = 30UL;
const unsigned long WATCHDOG_HEARTBEAT_MS = 250UL;
const unsigned long TELEMETRY_MS = 5000UL;
const unsigned long LINKY_LED_OK_MS = 1000UL;
const unsigned long DEBOUNCE_MS = 35UL;
const unsigned long CONSOLE_TIMEOUT_MS = 10UL * 60UL * 1000UL;
const unsigned long STATUS_LONG_PRESS_MS = 3000UL;
const unsigned long LEARNING_SETTLE_OFF_MS = 2000UL;
const unsigned long LEARNING_ZONE_ON_MS = 5000UL;
const uint16_t LEARNING_MIN_POWER_DELTA_VA = 500;

const uint32_t EEPROM_POWER_MAGIC = 0x54505731UL; // TPW1
const uint8_t EEPROM_POWER_VERSION = 1;
const int EEPROM_POWER_ADDR = 0;

const int MUX_10K_MAX = 255;
const int MUX_PRESSED_MAX = 980;
const bool INVERT_REQUESTED_HEAT = false;
const uint8_t ZONE_OUTPUT_HEAT_LEVEL = HIGH;
const uint8_t ZONE_OUTPUT_OFF_LEVEL = LOW;
const bool TIC_INVERTED = false;

enum ControlMode : uint8_t {
  MODE_CONSOLE,
  MODE_LOCAL_OVERRIDE,
  MODE_LEARNING
};

enum LearningState : uint8_t {
  LEARN_IDLE,
  LEARN_SETTLE_OFF,
  LEARN_ZONE_ON,
  LEARN_DONE
};

struct PowerCalibrationRecord {
  uint32_t magic;
  uint8_t version;
  uint16_t powers[ZONE_COUNT];
  uint16_t checksum;
};

Adafruit_NeoPixel leds(LED_COUNT, PIN_LED_CHAIN_DATA, NEO_GRB + NEO_KHZ800);
SoftwareSerial ticSerial(PIN_LINKY_RX, PIN_TIC_UNUSED_TX, TIC_INVERTED);

ControlMode controlMode = MODE_CONSOLE;
uint8_t workload[ZONE_COUNT] = {0, 0, 0, 0};
uint16_t slotAccumulator[ZONE_COUNT] = {0, 0, 0, 0};
bool zoneHeating[ZONE_COUNT] = {false, false, false, false};
bool localForcedOn[ZONE_COUNT] = {false, false, false, false};
uint16_t zonePowerVa[ZONE_COUNT] = {0, 0, 0, 0};

unsigned long cycleLengthMs = DEFAULT_CYCLE_MINUTES * 60UL * 1000UL;
unsigned long cycleStartedAt = 0;
unsigned long lastDutySlotAt = 0;
unsigned long lastTelemetryAt = 0;
unsigned long lastWatchdogAt = 0;
unsigned long linkyLedUntil = 0;
unsigned long lastConsoleCommandAt = 0;

bool watchdogLevel = false;
bool ledsDirty = true;
bool linkyLedRenderedOn = false;
bool learningLedRenderedOn = false;
bool lastStatusRaw = false;
bool stableStatus = false;
bool statusLongPressHandled = false;
unsigned long statusDebounceAt = 0;
unsigned long statusPressedAt = 0;
uint8_t lastMuxButton = 0;
uint8_t stableMuxButton = 0;
unsigned long muxDebounceAt = 0;
LearningState learningState = LEARN_IDLE;
uint8_t learningZone = 0;
uint16_t learningPowerBefore = 0;
unsigned long learningStepStartedAt = 0;

char consoleLine[96];
uint8_t consoleLineLen = 0;
char ticLine[128];
uint8_t ticLineLen = 0;
char currentDate[11] = "";
char currentTime[9] = "";
uint16_t currentPowerVa = 0;
bool hasDateTime = false;
bool hasPower = false;
uint32_t ticByteCount = 0;
uint32_t ticLineCount = 0;
uint32_t ticOverflowCount = 0;

uint32_t rgb(uint8_t red, uint8_t green, uint8_t blue) {
  return leds.Color(red, green, blue);
}

void markLedsDirty() {
  ledsDirty = true;
}

void serviceWatchdog() {
  unsigned long now = millis();
  if (now - lastWatchdogAt >= WATCHDOG_HEARTBEAT_MS) {
    watchdogLevel = !watchdogLevel;
    digitalWrite(PIN_HEARTBEAT, watchdogLevel ? HIGH : LOW);
    lastWatchdogAt = now;
  }
}

void applyZoneOutputs() {
  for (uint8_t i = 0; i < ZONE_COUNT; i++) {
    // zoneHeating=true doit toujours correspondre a la LED orange
    // et a la commande physique qui fait reellement chauffer la zone.
    digitalWrite(ZONE_CMD_PINS[i], zoneHeating[i] ? ZONE_OUTPUT_HEAT_LEVEL : ZONE_OUTPUT_OFF_LEVEL);
  }
}

void commitZoneStates() {
  applyZoneOutputs();
  markLedsDirty();
}

void renderLeds() {
  bool linkyLedOn = millis() < linkyLedUntil;

  for (uint8_t i = 0; i < ZONE_COUNT; i++) {
    if (zoneHeating[i]) {
      leds.setPixelColor(LED_ZONE_INDEX[i], rgb(95, 42, 0));
    } else if (zonePowerVa[i] == 0) {
      leds.setPixelColor(LED_ZONE_INDEX[i], rgb(24, 0, 45));
    } else {
      leds.setPixelColor(LED_ZONE_INDEX[i], 0);
    }
  }

  leds.setPixelColor(LED_LINKY_INDEX, linkyLedOn ? rgb(0, 70, 0) : 0);

  if (controlMode == MODE_LEARNING) {
    bool blinkOn = ((millis() / 250UL) % 2) == 0;
    leds.setPixelColor(LED_STATUS_INDEX, blinkOn ? rgb(0, 20, 80) : 0);
  } else if (controlMode == MODE_CONSOLE) {
    bool stale = (millis() - lastConsoleCommandAt) > CONSOLE_TIMEOUT_MS;
    leds.setPixelColor(LED_STATUS_INDEX, stale ? rgb(60, 0, 0) : rgb(0, 70, 0));
  } else {
    leds.setPixelColor(LED_STATUS_INDEX, rgb(90, 35, 0));
  }

  leds.show();
  ledsDirty = false;
  linkyLedRenderedOn = linkyLedOn;
}

void refreshLedsIfNeeded() {
  bool linkyLedOn = millis() < linkyLedUntil;
  bool learningLedOn = controlMode == MODE_LEARNING && ((millis() / 250UL) % 2) == 0;
  if (ledsDirty || linkyLedOn != linkyLedRenderedOn || learningLedOn != learningLedRenderedOn) {
    renderLeds();
    learningLedRenderedOn = learningLedOn;
  }
}

bool actualHeatFromRequestedHeat(bool requestedHeat) {
  return INVERT_REQUESTED_HEAT ? !requestedHeat : requestedHeat;
}

void setZoneActualHeat(uint8_t zone, bool actualHeat) {
  if (zone >= ZONE_COUNT) {
    return;
  }

  zoneHeating[zone] = actualHeat;
}

void setAllZonesActualHeat(bool actualHeat) {
  for (uint8_t i = 0; i < ZONE_COUNT; i++) {
    setZoneActualHeat(i, actualHeat);
  }
  commitZoneStates();
}

void setAllZonesRequestedHeat(bool requestedHeat) {
  setAllZonesActualHeat(actualHeatFromRequestedHeat(requestedHeat));
}

void stopPowerLearning() {
  learningState = LEARN_IDLE;
}

uint16_t computePowerChecksum(const PowerCalibrationRecord *record) {
  uint16_t checksum = (uint16_t)(record->magic & 0xFFFF);
  checksum ^= (uint16_t)(record->magic >> 16);
  checksum ^= record->version;

  for (uint8_t i = 0; i < ZONE_COUNT; i++) {
    checksum ^= record->powers[i];
    checksum = (uint16_t)((checksum << 3) | (checksum >> 13));
  }

  return checksum;
}

bool loadZonePowersFromEeprom() {
  PowerCalibrationRecord record;
  EEPROM.get(EEPROM_POWER_ADDR, record);

  if (record.magic != EEPROM_POWER_MAGIC || record.version != EEPROM_POWER_VERSION) {
    return false;
  }
  if (record.checksum != computePowerChecksum(&record)) {
    return false;
  }

  bool hasAtLeastOnePower = false;
  for (uint8_t i = 0; i < ZONE_COUNT; i++) {
    zonePowerVa[i] = record.powers[i];
    if (zonePowerVa[i] > 0) {
      hasAtLeastOnePower = true;
    }
  }

  markLedsDirty();
  return hasAtLeastOnePower;
}

void saveZonePowersToEeprom() {
  PowerCalibrationRecord record;
  record.magic = EEPROM_POWER_MAGIC;
  record.version = EEPROM_POWER_VERSION;
  for (uint8_t i = 0; i < ZONE_COUNT; i++) {
    record.powers[i] = zonePowerVa[i];
  }
  record.checksum = computePowerChecksum(&record);
  EEPROM.put(EEPROM_POWER_ADDR, record);
}

void startPowerLearning(const __FlashStringHelper *reason) {
  controlMode = MODE_LEARNING;
  learningState = LEARN_SETTLE_OFF;
  learningZone = 0;
  learningPowerBefore = 0;
  learningStepStartedAt = millis();
  setAllZonesActualHeat(false);

  Serial.print(F("LEARN START "));
  Serial.println(reason);
}

void recomputeDutyOutputs(bool resetAccumulators) {
  if (resetAccumulators) {
    for (uint8_t i = 0; i < ZONE_COUNT; i++) {
      slotAccumulator[i] = 0;
    }
  }

  for (uint8_t i = 0; i < ZONE_COUNT; i++) {
    bool requestedHeat = false;

    if (workload[i] == 0) {
      requestedHeat = false;
    } else if (workload[i] == 255) {
      requestedHeat = true;
    } else {
      slotAccumulator[i] += workload[i];
      if (slotAccumulator[i] >= 255) {
        slotAccumulator[i] -= 255;
        requestedHeat = true;
      } else {
        requestedHeat = false;
      }
    }

    setZoneActualHeat(i, actualHeatFromRequestedHeat(requestedHeat));
  }

  commitZoneStates();
}

void startConsoleCycle(bool resetAccumulators) {
  controlMode = MODE_CONSOLE;
  cycleStartedAt = millis();
  lastDutySlotAt = cycleStartedAt;
  recomputeDutyOutputs(resetAccumulators);
}

void serviceDutyCycle() {
  if (controlMode != MODE_CONSOLE) {
    return;
  }

  unsigned long now = millis();
  if (now - cycleStartedAt >= cycleLengthMs) {
    startConsoleCycle(true);
    return;
  }

  while (now - lastDutySlotAt >= DUTY_SLOT_MS) {
    lastDutySlotAt += DUTY_SLOT_MS;
    recomputeDutyOutputs(false);
  }
}

void returnToConsoleMode() {
  for (uint8_t i = 0; i < ZONE_COUNT; i++) {
    localForcedOn[i] = false;
  }
  stopPowerLearning();
  startConsoleCycle(true);
  Serial.println(F("ACK AUTO"));
}

void servicePowerLearning() {
  if (learningState == LEARN_IDLE) {
    return;
  }

  unsigned long now = millis();

  if (learningState == LEARN_SETTLE_OFF) {
    if (now - learningStepStartedAt < LEARNING_SETTLE_OFF_MS) {
      return;
    }

    if (!hasPower) {
      Serial.println(F("LEARN WAIT_TIC"));
      learningStepStartedAt = now;
      return;
    }

    learningPowerBefore = currentPowerVa;
    for (uint8_t i = 0; i < ZONE_COUNT; i++) {
      setZoneActualHeat(i, i == learningZone);
    }
    commitZoneStates();
    learningState = LEARN_ZONE_ON;
    learningStepStartedAt = now;

    Serial.print(F("LEARN Z"));
    Serial.print(learningZone + 1);
    Serial.print(F(" BEFORE="));
    Serial.println(learningPowerBefore);
    return;
  }

  if (learningState == LEARN_ZONE_ON) {
    if (now - learningStepStartedAt < LEARNING_ZONE_ON_MS) {
      return;
    }

    uint16_t powerAfter = currentPowerVa;
    uint16_t powerDelta = 0;
    uint16_t learnedPower = 0;
    if (hasPower && powerAfter > learningPowerBefore) {
      powerDelta = powerAfter - learningPowerBefore;
      if (powerDelta >= LEARNING_MIN_POWER_DELTA_VA) {
        learnedPower = powerDelta;
      }
    }
    zonePowerVa[learningZone] = learnedPower;

    Serial.print(F("LEARN Z"));
    Serial.print(learningZone + 1);
    Serial.print(F(" AFTER="));
    Serial.print(powerAfter);
    Serial.print(F(" DELTA="));
    Serial.print(powerDelta);
    Serial.print(F(" POWER="));
    Serial.println(learnedPower);

    learningZone++;
    setAllZonesActualHeat(false);

    if (learningZone >= ZONE_COUNT) {
      saveZonePowersToEeprom();
      learningState = LEARN_DONE;
      learningStepStartedAt = now;
      Serial.println(F("LEARN SAVED"));
      return;
    }

    learningState = LEARN_SETTLE_OFF;
    learningStepStartedAt = now;
    return;
  }

  if (learningState == LEARN_DONE) {
    returnToConsoleMode();
  }
}

bool debouncePressed(bool rawPressed, bool *lastRaw, bool *stable, unsigned long *changedAt) {
  unsigned long now = millis();
  if (rawPressed != *lastRaw) {
    *lastRaw = rawPressed;
    *changedAt = now;
  }
  if ((now - *changedAt) >= DEBOUNCE_MS && rawPressed != *stable) {
    *stable = rawPressed;
    return rawPressed;
  }
  return false;
}

int readAnalogStable(uint8_t pin) {
  analogRead(pin);
  delayMicroseconds(150);
  long total = 0;
  for (uint8_t i = 0; i < 12; i++) {
    total += analogRead(pin);
  }
  return total / 12;
}

uint8_t readMuxButton() {
  int mux1 = readAnalogStable(PIN_BTN_MUX1);
  int mux2 = readAnalogStable(PIN_BTN_MUX2);
  if (mux1 <= MUX_10K_MAX) return 1;
  if (mux1 <= MUX_PRESSED_MAX) return 2;
  if (mux2 <= MUX_10K_MAX) return 3;
  if (mux2 <= MUX_PRESSED_MAX) return 4;
  return 0;
}

uint8_t muxButtonPressedEvent() {
  uint8_t raw = readMuxButton();
  unsigned long now = millis();
  if (raw != lastMuxButton) {
    lastMuxButton = raw;
    muxDebounceAt = now;
  }
  if ((now - muxDebounceAt) >= DEBOUNCE_MS && raw != stableMuxButton) {
    stableMuxButton = raw;
    return raw;
  }
  return 0;
}

void serviceStatusButton() {
  bool rawPressed = digitalRead(PIN_BTN_STATUS) == LOW;
  unsigned long now = millis();

  if (rawPressed != lastStatusRaw) {
    lastStatusRaw = rawPressed;
    statusDebounceAt = now;
  }

  if ((now - statusDebounceAt) >= DEBOUNCE_MS && rawPressed != stableStatus) {
    stableStatus = rawPressed;

    if (stableStatus) {
      statusPressedAt = now;
      statusLongPressHandled = false;
    } else if (!statusLongPressHandled) {
      returnToConsoleMode();
    }
  }

  if (stableStatus && !statusLongPressHandled && (now - statusPressedAt) >= STATUS_LONG_PRESS_MS) {
    statusLongPressHandled = true;
    startPowerLearning(F("BUTTON"));
  }
}

void serviceButtons() {
  serviceStatusButton();

  uint8_t pressed = muxButtonPressedEvent();
  if (pressed >= 1 && pressed <= 4) {
    uint8_t zone = pressed - 1;
    if (controlMode == MODE_LEARNING) {
      stopPowerLearning();
      Serial.println(F("LEARN ABORT BUTTON"));
    }
    controlMode = MODE_LOCAL_OVERRIDE;
    localForcedOn[zone] = !localForcedOn[zone];
    for (uint8_t i = 0; i < ZONE_COUNT; i++) {
      setZoneActualHeat(i, actualHeatFromRequestedHeat(localForcedOn[i]));
    }
    commitZoneStates();
    Serial.print(F("ACK LOCAL Z"));
    Serial.print(pressed);
    Serial.print('=');
    Serial.println(localForcedOn[zone] ? F("ON") : F("OFF"));
  }
}

bool sameText(const char *a, const char *b) {
  return strcmp(a, b) == 0;
}

bool parseTicDate(const char *value) {
  if (strlen(value) < 13) {
    return false;
  }

  currentDate[0] = '2';
  currentDate[1] = '0';
  currentDate[2] = value[1];
  currentDate[3] = value[2];
  currentDate[4] = '-';
  currentDate[5] = value[3];
  currentDate[6] = value[4];
  currentDate[7] = '-';
  currentDate[8] = value[5];
  currentDate[9] = value[6];
  currentDate[10] = '\0';

  currentTime[0] = value[7];
  currentTime[1] = value[8];
  currentTime[2] = ':';
  currentTime[3] = value[9];
  currentTime[4] = value[10];
  currentTime[5] = ':';
  currentTime[6] = value[11];
  currentTime[7] = value[12];
  currentTime[8] = '\0';

  hasDateTime = true;
  linkyLedUntil = millis() + LINKY_LED_OK_MS;
  markLedsDirty();
  return true;
}

void handleTicGroup(char *line) {
  char *label = strtok(line, "\t ");
  char *value = strtok(NULL, "\t ");
  if (label == NULL || value == NULL) {
    return;
  }

  if (sameText(label, "DATE")) {
    parseTicDate(value);
  } else if (sameText(label, "SINSTS")) {
    currentPowerVa = (uint16_t)atoi(value);
    hasPower = true;
    linkyLedUntil = millis() + LINKY_LED_OK_MS;
    markLedsDirty();
  }
}

void finishTicLine() {
  if (ticLineLen == 0) {
    return;
  }
  ticLine[ticLineLen] = '\0';
  ticLineCount++;
  handleTicGroup(ticLine);
  ticLineLen = 0;
}

void handleTicByte(uint8_t rawByte) {
  ticByteCount++;
  char c = (char)(rawByte & 0x7F);

  if (c == 0x02) {
    ticLineLen = 0;
    return;
  }
  if (c == 0x03 || c == '\r') {
    finishTicLine();
    return;
  }
  if (c == '\n') {
    ticLineLen = 0;
    return;
  }
  if (c < 0x20 && c != '\t') {
    return;
  }
  if (ticLineLen < sizeof(ticLine) - 1) {
    ticLine[ticLineLen++] = c;
  } else {
    ticOverflowCount++;
    ticLineLen = 0;
  }
}

void readTic() {
  while (ticSerial.available() > 0) {
    handleTicByte((uint8_t)ticSerial.read());
  }
}

void printTelemetry() {
  Serial.print(F("TIMESTAMP="));
  if (hasDateTime) {
    Serial.print(currentDate);
    Serial.print(' ');
    Serial.println(currentTime);
  } else {
    Serial.println(F("NA"));
  }

  Serial.print(F("PAPP="));
  if (hasPower) {
    Serial.println(currentPowerVa);
  } else {
    Serial.println(F("NA"));
  }

  for (uint8_t i = 0; i < ZONE_COUNT; i++) {
    Serial.print('Z');
    Serial.print(i + 1);
    Serial.print(F("_PUISSANCE="));
    Serial.println(zonePowerVa[i]);
  }
}

void printStatus() {
  int mux1 = readAnalogStable(PIN_BTN_MUX1);
  int mux2 = readAnalogStable(PIN_BTN_MUX2);

  Serial.print(F("STATUS MODE="));
  if (controlMode == MODE_CONSOLE) {
    Serial.print(F("CONSOLE"));
  } else if (controlMode == MODE_LOCAL_OVERRIDE) {
    Serial.print(F("LOCAL"));
  } else {
    Serial.print(F("LEARNING"));
  }
  Serial.print(F(" DC_LENGTH="));
  Serial.print(cycleLengthMs / 60000UL);
  for (uint8_t i = 0; i < ZONE_COUNT; i++) {
    Serial.print(F(" Z"));
    Serial.print(i + 1);
    Serial.print(F("_WORKLOAD="));
    Serial.print(workload[i]);
    Serial.print(F(" Z"));
    Serial.print(i + 1);
    Serial.print(F("_ON="));
    Serial.print(zoneHeating[i] ? F("1") : F("0"));
    Serial.print(F(" Z"));
    Serial.print(i + 1);
    Serial.print(F("_POWER="));
    Serial.print(zonePowerVa[i]);
  }
  Serial.print(F(" TIC_BYTES="));
  Serial.print(ticByteCount);
  Serial.print(F(" TIC_LINES="));
  Serial.print(ticLineCount);
  Serial.print(F(" TIC_OVERFLOW="));
  Serial.print(ticOverflowCount);
  Serial.print(F(" MUX1="));
  Serial.print(mux1);
  Serial.print(F(" MUX2="));
  Serial.println(mux2);
}

bool parseUnsignedInt(const char *text, uint16_t *value) {
  if (text == NULL || text[0] == '\0') {
    return false;
  }

  uint32_t parsed = 0;
  for (uint8_t i = 0; text[i] != '\0'; i++) {
    if (text[i] < '0' || text[i] > '9') {
      return false;
    }
    parsed = parsed * 10 + (uint8_t)(text[i] - '0');
    if (parsed > 65535UL) {
      return false;
    }
  }

  *value = (uint16_t)parsed;
  return true;
}

bool parseByteValue(const char *text, uint8_t *value) {
  uint16_t parsed;
  if (!parseUnsignedInt(text, &parsed)) {
    return false;
  }
  if (parsed > 255) {
    return false;
  }
  *value = (uint8_t)parsed;
  return true;
}

void acknowledgeSet() {
  Serial.print(F("ACK SET DC_LENGTH="));
  Serial.print(cycleLengthMs / 60000UL);
  for (uint8_t i = 0; i < ZONE_COUNT; i++) {
    Serial.print(F(" Z"));
    Serial.print(i + 1);
    Serial.print(F("_WORKLOAD="));
    Serial.print(workload[i]);
  }
  Serial.println();
}

void applyConsoleInstruction() {
  lastConsoleCommandAt = millis();
  for (uint8_t i = 0; i < ZONE_COUNT; i++) {
    localForcedOn[i] = false;
  }
  stopPowerLearning();
  startConsoleCycle(true);
  acknowledgeSet();
}

void handleConsoleCommand(char *line) {
  if (line[0] == '\0') {
    return;
  }

  if (sameText(line, "PING")) {
    lastConsoleCommandAt = millis();
    Serial.println(F("PONG PILOTE_TIC"));
    return;
  }
  if (sameText(line, "STATUS?") || sameText(line, "DIAG")) {
    printStatus();
    return;
  }
  if (sameText(line, "AUTO")) {
    lastConsoleCommandAt = millis();
    returnToConsoleMode();
    return;
  }
  if (sameText(line, "LEARN")) {
    lastConsoleCommandAt = millis();
    startPowerLearning(F("COMMAND"));
    return;
  }
  if (sameText(line, "ALL_OFF")) {
    lastConsoleCommandAt = millis();
    stopPowerLearning();
    controlMode = MODE_LOCAL_OVERRIDE;
    setAllZonesRequestedHeat(false);
    Serial.println(F("ACK ALL_OFF"));
    return;
  }
  if (sameText(line, "ALL_ON")) {
    lastConsoleCommandAt = millis();
    stopPowerLearning();
    controlMode = MODE_LOCAL_OVERRIDE;
    setAllZonesRequestedHeat(true);
    Serial.println(F("ACK ALL_ON"));
    return;
  }

  if (strncmp(line, "SET ", 4) == 0) {
    char *token = strtok(line + 4, " ");
    if (token == NULL) {
      Serial.println(F("ERR SET DC_LENGTH manquant"));
      return;
    }
    uint16_t minutes;
    if (!parseUnsignedInt(token, &minutes)) {
      Serial.println(F("ERR DC_LENGTH invalide"));
      return;
    }
    if (minutes < 1 || minutes > 240) {
      Serial.println(F("ERR DC_LENGTH hors limites"));
      return;
    }

    uint8_t nextWorkload[ZONE_COUNT];
    for (uint8_t i = 0; i < ZONE_COUNT; i++) {
      token = strtok(NULL, " ");
      if (token == NULL || !parseByteValue(token, &nextWorkload[i])) {
        Serial.println(F("ERR workload invalide"));
        return;
      }
    }

    cycleLengthMs = (unsigned long)minutes * 60UL * 1000UL;
    for (uint8_t i = 0; i < ZONE_COUNT; i++) {
      workload[i] = nextWorkload[i];
    }
    applyConsoleInstruction();
    return;
  }

  if (strncmp(line, "DC_LENGTH=", 10) == 0) {
    uint16_t minutes;
    if (!parseUnsignedInt(line + 10, &minutes)) {
      Serial.println(F("ERR DC_LENGTH invalide"));
      return;
    }
    if (minutes < 1 || minutes > 240) {
      Serial.println(F("ERR DC_LENGTH hors limites"));
      return;
    }
    cycleLengthMs = (unsigned long)minutes * 60UL * 1000UL;
    applyConsoleInstruction();
    return;
  }

  if (line[0] == 'Z' && line[1] >= '1' && line[1] <= '4' && strncmp(line + 2, "_WORKLOAD=", 10) == 0) {
    uint8_t value;
    if (!parseByteValue(line + 12, &value)) {
      Serial.println(F("ERR workload hors limites"));
      return;
    }
    workload[line[1] - '1'] = value;
    applyConsoleInstruction();
    return;
  }

  Serial.print(F("ERR commande inconnue: "));
  Serial.println(line);
}

void readConsole() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      consoleLine[consoleLineLen] = '\0';
      handleConsoleCommand(consoleLine);
      consoleLineLen = 0;
    } else if (consoleLineLen < sizeof(consoleLine) - 1) {
      consoleLine[consoleLineLen++] = c;
    } else {
      consoleLineLen = 0;
      Serial.println(F("ERR ligne trop longue"));
    }
  }
}

void bootLeds() {
  for (uint8_t i = 0; i < LED_COUNT; i++) {
    leds.clear();
    leds.setPixelColor(i, rgb(0, 20, 45));
    leds.show();
    unsigned long startedAt = millis();
    while (millis() - startedAt < 80) {
      serviceWatchdog();
    }
  }
  leds.clear();
  leds.show();
}

void setup() {
  pinMode(PIN_HEARTBEAT, OUTPUT);
  pinMode(PIN_BTN_STATUS, INPUT_PULLUP);
  pinMode(PIN_BTN_MUX1, INPUT_PULLUP);
  pinMode(PIN_BTN_MUX2, INPUT_PULLUP);
  pinMode(PIN_LINKY_RX, INPUT);

  for (uint8_t i = 0; i < ZONE_COUNT; i++) {
    pinMode(ZONE_CMD_PINS[i], OUTPUT);
  }

  digitalWrite(PIN_HEARTBEAT, LOW);
  setAllZonesActualHeat(false);

  leds.begin();
  leds.setBrightness(18);
  bootLeds();

  Serial.begin(BAUD_RATE);
  ticSerial.begin(BAUD_RATE);
  ticSerial.listen();

  lastConsoleCommandAt = millis();
  cycleStartedAt = millis();
  lastDutySlotAt = millis();
  lastTelemetryAt = millis();

  Serial.println(F("BOOT PILOTE_TIC"));
  Serial.println(F("READY 9600"));
  if (loadZonePowersFromEeprom()) {
    Serial.println(F("EEPROM POWER OK"));
    startConsoleCycle(true);
  } else {
    Serial.println(F("EEPROM POWER EMPTY"));
    startPowerLearning(F("EEPROM_EMPTY"));
  }
}

void loop() {
  serviceWatchdog();
  readTic();
  readConsole();
  serviceButtons();
  servicePowerLearning();
  serviceDutyCycle();
  refreshLedsIfNeeded();

  unsigned long now = millis();
  if (now - lastTelemetryAt >= TELEMETRY_MS) {
    lastTelemetryAt = now;
    printTelemetry();
  }
}
