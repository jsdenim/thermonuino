/*
  Thermonuino - simulateur de pilote TIC

  Cible: Arduino Uno, Serial 9600 bauds.

  Ce sketch remplace temporairement le module pilote-tic pour tester la console:
    - accepte les commandes console du pilote reel: SET, DC_LENGTH, Zx_WORKLOAD,
      STATUS?, PING, AUTO, ALL_OFF, ALL_ON, LEARN;
    - renvoie les ACK attendus;
    - publie une telemetrie type Linky: TIMESTAMP, PAPP, Zx_PUISSANCE;
    - simule le PWM de zones et logge les changements ON/OFF.

  Sur Uno, Serial est partage entre les pins 0/1 et l'USB. Les lignes SIM_* sont
  donc visibles sur l'ordinateur et ignorees par la console.
*/

#include <string.h>
#include <stdlib.h>

const uint8_t ZONE_COUNT = 4;
const uint16_t ZONE_POWER_VA[ZONE_COUNT] = {1000, 1200, 1200, 2400};
const unsigned long BAUD_RATE = 9600;
const unsigned long DEFAULT_CYCLE_MINUTES = 30UL;
const unsigned long DUTY_SLOT_MS = 10000UL;
const unsigned long TELEMETRY_MS = 5000UL;
const uint16_t START_YEAR = 2026;
const uint8_t START_MONTH = 1;
const uint8_t START_DAY = 1;
const uint8_t START_HOUR = 0;
const uint8_t START_MINUTE = 0;
const uint8_t START_SECOND = 0;

uint8_t workload[ZONE_COUNT] = {0, 0, 0, 0};
bool zoneOn[ZONE_COUNT] = {false, false, false, false};
uint16_t slotAccumulator[ZONE_COUNT] = {0, 0, 0, 0};
unsigned long cycleLengthMs = DEFAULT_CYCLE_MINUTES * 60UL * 1000UL;
unsigned long cycleStartedAt = 0;
unsigned long lastDutySlotAt = 0;
unsigned long lastTelemetryAt = 0;
uint32_t commandCount = 0;
uint32_t lineOverflowCount = 0;
char lineBuffer[96];
uint8_t lineLen = 0;

bool sameText(const char *a, const char *b) {
  return strcmp(a, b) == 0;
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
  uint16_t parsed = 0;
  if (!parseUnsignedInt(text, &parsed) || parsed > 255) {
    return false;
  }
  *value = (uint8_t)parsed;
  return true;
}

void print2(uint8_t value) {
  if (value < 10) {
    Serial.print('0');
  }
  Serial.print(value);
}

bool leapYear(uint16_t year) {
  return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

uint8_t daysInMonth(uint16_t year, uint8_t month) {
  static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2 && leapYear(year)) {
    return 29;
  }
  return days[month - 1];
}

void printSimulatedTimestamp() {
  uint32_t elapsed = millis() / 1000UL;
  uint16_t year = START_YEAR;
  uint8_t month = START_MONTH;
  uint8_t day = START_DAY;
  uint8_t hour = START_HOUR;
  uint8_t minute = START_MINUTE;
  uint8_t second = START_SECOND;

  second += elapsed % 60UL;
  elapsed /= 60UL;
  if (second >= 60) {
    second -= 60;
    elapsed++;
  }

  minute += elapsed % 60UL;
  elapsed /= 60UL;
  if (minute >= 60) {
    minute -= 60;
    elapsed++;
  }

  hour += elapsed % 24UL;
  elapsed /= 24UL;
  if (hour >= 24) {
    hour -= 24;
    elapsed++;
  }

  while (elapsed > 0) {
    const uint8_t dim = daysInMonth(year, month);
    if (day < dim) {
      day++;
    } else {
      day = 1;
      month++;
      if (month > 12) {
        month = 1;
        year++;
      }
    }
    elapsed--;
  }

  Serial.print(year);
  Serial.print('-');
  print2(month);
  Serial.print('-');
  print2(day);
  Serial.print(' ');
  print2(hour);
  Serial.print(':');
  print2(minute);
  Serial.print(':');
  print2(second);
}

uint16_t currentPowerVa() {
  uint16_t power = 0;
  for (uint8_t i = 0; i < ZONE_COUNT; i++) {
    if (zoneOn[i]) {
      power += ZONE_POWER_VA[i];
    }
  }
  return power;
}

void logZoneChange(uint8_t index, bool on) {
  Serial.print(F("SIM Z"));
  Serial.print(index + 1);
  Serial.print(on ? F(" ON ") : F(" OFF "));
  Serial.print(F("WORKLOAD="));
  Serial.print(workload[index]);
  Serial.print(F(" POWER="));
  Serial.println(on ? ZONE_POWER_VA[index] : 0);
}

void setZoneOn(uint8_t index, bool on) {
  if (zoneOn[index] == on) {
    return;
  }
  zoneOn[index] = on;
  logZoneChange(index, on);
}

void setAllZones(bool on) {
  for (uint8_t i = 0; i < ZONE_COUNT; i++) {
    setZoneOn(i, on);
  }
}

void resetCycle() {
  cycleStartedAt = millis();
  lastDutySlotAt = millis();
  for (uint8_t i = 0; i < ZONE_COUNT; i++) {
    slotAccumulator[i] = 0;
    setZoneOn(i, workload[i] == 255);
  }
}

void updateDutyCycle() {
  const unsigned long now = millis();
  if (cycleLengthMs == 0 || (unsigned long)(now - cycleStartedAt) >= cycleLengthMs) {
    resetCycle();
  }
  if ((unsigned long)(now - lastDutySlotAt) < DUTY_SLOT_MS) {
    return;
  }
  lastDutySlotAt = now;

  for (uint8_t i = 0; i < ZONE_COUNT; i++) {
    if (workload[i] == 0) {
      setZoneOn(i, false);
    } else if (workload[i] == 255) {
      setZoneOn(i, true);
    } else {
      slotAccumulator[i] += workload[i];
      if (slotAccumulator[i] >= 255) {
        slotAccumulator[i] -= 255;
        setZoneOn(i, true);
      } else {
        setZoneOn(i, false);
      }
    }
  }
}

void printTelemetry() {
  Serial.print(F("TIMESTAMP="));
  printSimulatedTimestamp();
  Serial.println();

  Serial.print(F("PAPP="));
  Serial.println(currentPowerVa());

  for (uint8_t i = 0; i < ZONE_COUNT; i++) {
    Serial.print('Z');
    Serial.print(i + 1);
    Serial.print(F("_PUISSANCE="));
    Serial.println(ZONE_POWER_VA[i]);
  }
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

void printStatus() {
  Serial.print(F("STATUS MODE=SIM DC_LENGTH="));
  Serial.print(cycleLengthMs / 60000UL);
  for (uint8_t i = 0; i < ZONE_COUNT; i++) {
    Serial.print(F(" Z"));
    Serial.print(i + 1);
    Serial.print(F("_WORKLOAD="));
    Serial.print(workload[i]);
    Serial.print(F(" Z"));
    Serial.print(i + 1);
    Serial.print(F("_ON="));
    Serial.print(zoneOn[i] ? F("1") : F("0"));
    Serial.print(F(" Z"));
    Serial.print(i + 1);
    Serial.print(F("_POWER="));
    Serial.print(ZONE_POWER_VA[i]);
  }
  Serial.print(F(" PAPP="));
  Serial.print(currentPowerVa());
  Serial.print(F(" COMMANDS="));
  Serial.print(commandCount);
  Serial.print(F(" OVERFLOWS="));
  Serial.println(lineOverflowCount);
}

void applyConsoleInstruction() {
  resetCycle();
  acknowledgeSet();
}

void handleConsoleCommand(char *line) {
  if (line[0] == '\0') {
    return;
  }

  commandCount++;
  Serial.print(F("SIM RX "));
  Serial.println(line);

  if (sameText(line, "PING")) {
    Serial.println(F("PONG PILOTE_TIC"));
    return;
  }
  if (sameText(line, "STATUS?") || sameText(line, "DIAG")) {
    printStatus();
    return;
  }
  if (sameText(line, "AUTO")) {
    Serial.println(F("ACK AUTO"));
    resetCycle();
    return;
  }
  if (sameText(line, "LEARN")) {
    Serial.println(F("LEARN START SIMULATION"));
    for (uint8_t i = 0; i < ZONE_COUNT; i++) {
      Serial.print(F("LEARN Z"));
      Serial.print(i + 1);
      Serial.print(F(" POWER="));
      Serial.println(ZONE_POWER_VA[i]);
    }
    Serial.println(F("LEARN SAVED"));
    printTelemetry();
    return;
  }
  if (sameText(line, "ALL_OFF")) {
    for (uint8_t i = 0; i < ZONE_COUNT; i++) {
      workload[i] = 0;
    }
    setAllZones(false);
    Serial.println(F("ACK ALL_OFF"));
    return;
  }
  if (sameText(line, "ALL_ON")) {
    for (uint8_t i = 0; i < ZONE_COUNT; i++) {
      workload[i] = 255;
    }
    setAllZones(true);
    Serial.println(F("ACK ALL_ON"));
    return;
  }

  if (strncmp(line, "SET ", 4) == 0) {
    char *token = strtok(line + 4, " ");
    if (token == NULL) {
      Serial.println(F("ERR SET DC_LENGTH manquant"));
      return;
    }
    uint16_t minutes = 0;
    if (!parseUnsignedInt(token, &minutes) || minutes < 1 || minutes > 240) {
      Serial.println(F("ERR DC_LENGTH invalide"));
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
    uint16_t minutes = 0;
    if (!parseUnsignedInt(line + 10, &minutes) || minutes < 1 || minutes > 240) {
      Serial.println(F("ERR DC_LENGTH invalide"));
      return;
    }
    cycleLengthMs = (unsigned long)minutes * 60UL * 1000UL;
    applyConsoleInstruction();
    return;
  }

  if (line[0] == 'Z' &&
      line[1] >= '1' &&
      line[1] <= '4' &&
      strncmp(line + 2, "_WORKLOAD=", 10) == 0) {
    uint8_t value = 0;
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
      lineBuffer[lineLen] = '\0';
      handleConsoleCommand(lineBuffer);
      lineLen = 0;
    } else if (lineLen < sizeof(lineBuffer) - 1) {
      lineBuffer[lineLen++] = c;
    } else {
      lineLen = 0;
      lineOverflowCount++;
      Serial.println(F("ERR ligne trop longue"));
    }
  }
}

void setup() {
  Serial.begin(BAUD_RATE);
  cycleStartedAt = millis();
  lastDutySlotAt = millis();
  lastTelemetryAt = millis();
  Serial.println(F("BOOT PILOTE_SIMULATION"));
  Serial.println(F("READY 9600"));
  printTelemetry();
}

void loop() {
  readConsole();
  updateDutyCycle();
  if ((unsigned long)(millis() - lastTelemetryAt) >= TELEMETRY_MS) {
    lastTelemetryAt = millis();
    printTelemetry();
  }
}
