/*
  Thermonuino - simulateur de pilote TIC

  Cible: Arduino Uno, USB Serial 9600 bauds.

  Ce sketch remplace temporairement le module pilote-tic pour tester la console:
    - accepte les commandes console du pilote reel: SET, DC_LENGTH, Zx_WORKLOAD,
      STATUS?, PING, AUTO, ALL_OFF, ALL_ON, LEARN;
    - accepte TIME? et TIME yyyy-mm-dd hh:mm:ss pour regler l'heure simulee;
    - renvoie les ACK attendus;
    - publie une telemetrie type Linky: TIMESTAMP, PAPP, Zx_PUISSANCE;
    - simule le PWM de zones et logge les changements ON/OFF.

  Cablage:
    - USB Uno vers ordinateur: moniteur serie et commandes de test.
    - Uno D8  RX AltSoftSerial <- TX console.
    - Uno D9  TX AltSoftSerial -> RX console.
    - GND Uno <-> GND console.

  Les lignes PIL> visibles sur l'USB sont une copie de ce que le simulateur
  envoie a la console. Le prefixe PIL> n'est pas envoye a la console.

  Commandes utiles depuis le moniteur serie:
    - vers la console:
        DBG? ou DBG              affiche l'etat general et les zones;
        DBG Z1 .. DBG Z4         affiche le detail d'une zone;
        DBG PROG Z1 .. Z4        affiche le detail programmation/mode d'une zone;
        DBG ASSOC                affiche les associations RF connues;
        DBG SET 28               force toutes les consignes console a 28.0 C;
        DBG SET Z1 27.5          force seulement la consigne console Z1;
        DBG SET OFF              annule les consignes forcees;
        DBG SET Z1 OFF           annule la consigne forcee Z1;
        DBG TIME?                affiche l'heure interne console;
        DBG TIME yyyy-mm-dd hh:mm:ss
                                  force l'heure interne console et la luminosite;
        DBG HELP                 rappelle les commandes debug console.
      Ces commandes sont relayees telles quelles par le simulateur vers la console.
    - vers le simulateur pilote:
        TIME?                    affiche l'heure simulee;
        TIME yyyy-mm-dd hh:mm:ss regle l'heure simulee et republie TIMESTAMP;
                                  n'ecrit pas directement l'heure console;
        STATUS? ou DIAG          affiche l'etat du simulateur;
        HELP ou ?                affiche cette aide sur le moniteur serie;
        PING                     verifie la reponse du simulateur;
        ALL_OFF / ALL_ON         force les workloads a 0 ou 255;
        LEARN                    simule l'apprentissage des puissances.
*/

#include <string.h>
#include <stdlib.h>
#include <AltSoftSerial.h>

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

struct SimClock {
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
  unsigned long setAtMs;
};

uint8_t workload[ZONE_COUNT] = {0, 0, 0, 0};
bool zoneOn[ZONE_COUNT] = {false, false, false, false};
uint16_t slotAccumulator[ZONE_COUNT] = {0, 0, 0, 0};
unsigned long cycleLengthMs = DEFAULT_CYCLE_MINUTES * 60UL * 1000UL;
unsigned long cycleStartedAt = 0;
unsigned long lastDutySlotAt = 0;
unsigned long lastTelemetryAt = 0;
uint32_t commandCount = 0;
struct InputLine {
  char buffer[192];
  uint8_t len;
  bool discard;
};

AltSoftSerial consoleSerial;

class PiloteMirror : public Print {
 public:
  size_t write(uint8_t value) override {
    if (atLineStart) {
      Serial.print(F("PIL> "));
      atLineStart = false;
    }
    Serial.write(value);
    consoleSerial.write(value);
    if (value == '\n') {
      atLineStart = true;
    }
    return 1;
  }

 private:
  bool atLineStart = true;
};

PiloteMirror piloteIo;
uint32_t lineOverflowCount = 0;
InputLine hostInput = {{0}, 0, false};
InputLine consoleInput = {{0}, 0, false};
SimClock simClock = {
  START_YEAR,
  START_MONTH,
  START_DAY,
  START_HOUR,
  START_MINUTE,
  START_SECOND,
  0
};

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
    piloteIo.print('0');
  }
  piloteIo.print(value);
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

bool digitAt(const char *text, uint8_t index) {
  return text[index] >= '0' && text[index] <= '9';
}

uint8_t twoDigitsAt(const char *text, uint8_t index) {
  return (text[index] - '0') * 10 + (text[index + 1] - '0');
}

uint16_t fourDigitsAt(const char *text, uint8_t index) {
  return (uint16_t)(text[index] - '0') * 1000U +
      (uint16_t)(text[index + 1] - '0') * 100U +
      (uint16_t)(text[index + 2] - '0') * 10U +
      (uint16_t)(text[index + 3] - '0');
}

bool timestampFormatLooksValid(const char *text) {
  return digitAt(text, 0) &&
      digitAt(text, 1) &&
      digitAt(text, 2) &&
      digitAt(text, 3) &&
      text[4] == '-' &&
      digitAt(text, 5) &&
      digitAt(text, 6) &&
      text[7] == '-' &&
      digitAt(text, 8) &&
      digitAt(text, 9) &&
      text[10] == ' ' &&
      digitAt(text, 11) &&
      digitAt(text, 12) &&
      text[13] == ':' &&
      digitAt(text, 14) &&
      digitAt(text, 15) &&
      text[16] == ':' &&
      digitAt(text, 17) &&
      digitAt(text, 18) &&
      text[19] == '\0';
}

bool parseTimestamp(const char *text, SimClock &clock) {
  if (!timestampFormatLooksValid(text)) {
    return false;
  }

  const uint16_t year = fourDigitsAt(text, 0);
  const uint8_t month = twoDigitsAt(text, 5);
  const uint8_t day = twoDigitsAt(text, 8);
  const uint8_t hour = twoDigitsAt(text, 11);
  const uint8_t minute = twoDigitsAt(text, 14);
  const uint8_t second = twoDigitsAt(text, 17);

  if (month < 1 || month > 12 ||
      day < 1 || day > daysInMonth(year, month) ||
      hour > 23 ||
      minute > 59 ||
      second > 59) {
    return false;
  }

  clock.year = year;
  clock.month = month;
  clock.day = day;
  clock.hour = hour;
  clock.minute = minute;
  clock.second = second;
  clock.setAtMs = millis();
  return true;
}

void printTimestampFromClock(const SimClock &clock) {
  uint32_t elapsed = (millis() - clock.setAtMs) / 1000UL;
  uint16_t year = clock.year;
  uint8_t month = clock.month;
  uint8_t day = clock.day;
  uint8_t hour = clock.hour;
  uint8_t minute = clock.minute;
  uint8_t second = clock.second;

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

  piloteIo.print(year);
  piloteIo.print('-');
  print2(month);
  piloteIo.print('-');
  print2(day);
  piloteIo.print(' ');
  print2(hour);
  piloteIo.print(':');
  print2(minute);
  piloteIo.print(':');
  print2(second);
}

void printSimulatedTimestamp() {
  printTimestampFromClock(simClock);
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
  piloteIo.print(F("SIM Z"));
  piloteIo.print(index + 1);
  piloteIo.print(on ? F(" ON ") : F(" OFF "));
  piloteIo.print(F("WORKLOAD="));
  piloteIo.print(workload[index]);
  piloteIo.print(F(" POWER="));
  piloteIo.println(on ? ZONE_POWER_VA[index] : 0);
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
  piloteIo.print(F("TIMESTAMP="));
  printSimulatedTimestamp();
  piloteIo.println();

  piloteIo.print(F("PAPP="));
  piloteIo.println(currentPowerVa());

  for (uint8_t i = 0; i < ZONE_COUNT; i++) {
    piloteIo.print('Z');
    piloteIo.print(i + 1);
    piloteIo.print(F("_PUISSANCE="));
    piloteIo.println(ZONE_POWER_VA[i]);
  }
}

void acknowledgeSet() {
  piloteIo.print(F("ACK SET DC_LENGTH="));
  piloteIo.print(cycleLengthMs / 60000UL);
  for (uint8_t i = 0; i < ZONE_COUNT; i++) {
    piloteIo.print(F(" Z"));
    piloteIo.print(i + 1);
    piloteIo.print(F("_WORKLOAD="));
    piloteIo.print(workload[i]);
  }
  piloteIo.println();
}

void printStatus() {
  piloteIo.print(F("STATUS MODE=SIM DC_LENGTH="));
  piloteIo.print(cycleLengthMs / 60000UL);
  piloteIo.print(F(" TIME="));
  printSimulatedTimestamp();
  for (uint8_t i = 0; i < ZONE_COUNT; i++) {
    piloteIo.print(F(" Z"));
    piloteIo.print(i + 1);
    piloteIo.print(F("_WORKLOAD="));
    piloteIo.print(workload[i]);
    piloteIo.print(F(" Z"));
    piloteIo.print(i + 1);
    piloteIo.print(F("_ON="));
    piloteIo.print(zoneOn[i] ? F("1") : F("0"));
    piloteIo.print(F(" Z"));
    piloteIo.print(i + 1);
    piloteIo.print(F("_POWER="));
    piloteIo.print(ZONE_POWER_VA[i]);
  }
  piloteIo.print(F(" PAPP="));
  piloteIo.print(currentPowerVa());
  piloteIo.print(F(" COMMANDS="));
  piloteIo.print(commandCount);
  piloteIo.print(F(" OVERFLOWS="));
  piloteIo.println(lineOverflowCount);
}

void applyConsoleInstruction() {
  resetCycle();
  acknowledgeSet();
}

void printHelp() {
  Serial.println(F("PIL> COMMANDES VERS CONSOLE"));
  Serial.println(F("PIL>   DBG / DBG?"));
  Serial.println(F("PIL>   DBG Z1 .. DBG Z4"));
  Serial.println(F("PIL>   DBG PROG Z1 .. DBG PROG Z4"));
  Serial.println(F("PIL>   DBG ASSOC"));
  Serial.println(F("PIL>   DBG SET 28"));
  Serial.println(F("PIL>   DBG SET Z1 27.5"));
  Serial.println(F("PIL>   DBG SET OFF"));
  Serial.println(F("PIL>   DBG SET Z1 OFF"));
  Serial.println(F("PIL>   DBG TIME?"));
  Serial.println(F("PIL>   DBG TIME yyyy-mm-dd hh:mm:ss"));
  Serial.println(F("PIL>   DBG HELP"));
  Serial.println(F("PIL> COMMANDES VERS PILOTE-SIMULATION"));
  Serial.println(F("PIL>   TIME?"));
  Serial.println(F("PIL>   TIME yyyy-mm-dd hh:mm:ss"));
  Serial.println(F("PIL>   STATUS? / DIAG"));
  Serial.println(F("PIL>   PING"));
  Serial.println(F("PIL>   ALL_OFF / ALL_ON"));
  Serial.println(F("PIL>   LEARN"));
  Serial.println(F("PIL>   HELP / ?"));
}

bool looksLikePiloteCommand(const char *line) {
  return strcmp(line, "PING") == 0 ||
      strcmp(line, "STATUS?") == 0 ||
      strcmp(line, "DIAG") == 0 ||
      strcmp(line, "AUTO") == 0 ||
      strcmp(line, "LEARN") == 0 ||
      strcmp(line, "ALL_OFF") == 0 ||
      strcmp(line, "ALL_ON") == 0 ||
      strncmp(line, "SET ", 4) == 0 ||
      strncmp(line, "DC_LENGTH=", 10) == 0 ||
      (line[0] == 'Z' &&
       line[1] >= '1' &&
       line[1] <= '4' &&
       strncmp(line + 2, "_WORKLOAD=", 10) == 0);
}

void handleConsoleCommand(char *line, bool fromHost) {
  if (line[0] == '\0') {
    return;
  }

  if (strncmp(line, "DBG", 3) == 0) {
    if (fromHost) {
      Serial.print(F("PIL> FORWARD "));
      Serial.println(line);
      consoleSerial.println(line);
    }
    return;
  }

  if (sameText(line, "HELP") || sameText(line, "?")) {
    printHelp();
    return;
  }

  if (strncmp(line, "#DBG", 4) == 0 ||
      strncmp(line, "CON>", 4) == 0) {
    Serial.println(line);
    return;
  }

  if (!fromHost && !looksLikePiloteCommand(line)) {
    Serial.print(F("CON? "));
    Serial.println(line);
    return;
  }

  commandCount++;
  piloteIo.print(F("SIM RX "));
  piloteIo.println(line);

  if (sameText(line, "PING")) {
    piloteIo.println(F("PONG PILOTE_TIC"));
    return;
  }
  if (sameText(line, "STATUS?") || sameText(line, "DIAG")) {
    printStatus();
    return;
  }
  if (sameText(line, "TIME?")) {
    piloteIo.print(F("TIME="));
    printSimulatedTimestamp();
    piloteIo.println();
    return;
  }
  if (strncmp(line, "TIME ", 5) == 0) {
    if (!parseTimestamp(line + 5, simClock)) {
      piloteIo.println(F("ERR TIME format attendu: TIME yyyy-mm-dd hh:mm:ss"));
      return;
    }
    piloteIo.print(F("ACK TIME "));
    printSimulatedTimestamp();
    piloteIo.println();
    printTelemetry();
    return;
  }
  if (sameText(line, "AUTO")) {
    piloteIo.println(F("ACK AUTO"));
    resetCycle();
    return;
  }
  if (sameText(line, "LEARN")) {
    piloteIo.println(F("LEARN START SIMULATION"));
    for (uint8_t i = 0; i < ZONE_COUNT; i++) {
      piloteIo.print(F("LEARN Z"));
      piloteIo.print(i + 1);
      piloteIo.print(F(" POWER="));
      piloteIo.println(ZONE_POWER_VA[i]);
    }
    piloteIo.println(F("LEARN SAVED"));
    printTelemetry();
    return;
  }
  if (sameText(line, "ALL_OFF")) {
    for (uint8_t i = 0; i < ZONE_COUNT; i++) {
      workload[i] = 0;
    }
    setAllZones(false);
    piloteIo.println(F("ACK ALL_OFF"));
    return;
  }
  if (sameText(line, "ALL_ON")) {
    for (uint8_t i = 0; i < ZONE_COUNT; i++) {
      workload[i] = 255;
    }
    setAllZones(true);
    piloteIo.println(F("ACK ALL_ON"));
    return;
  }

  if (strncmp(line, "SET ", 4) == 0) {
    char *token = strtok(line + 4, " ");
    if (token == NULL) {
      piloteIo.println(F("ERR SET DC_LENGTH manquant"));
      return;
    }
    uint16_t minutes = 0;
    if (!parseUnsignedInt(token, &minutes) || minutes < 1 || minutes > 240) {
      piloteIo.println(F("ERR DC_LENGTH invalide"));
      return;
    }

    uint8_t nextWorkload[ZONE_COUNT];
    for (uint8_t i = 0; i < ZONE_COUNT; i++) {
      token = strtok(NULL, " ");
      if (token == NULL || !parseByteValue(token, &nextWorkload[i])) {
        piloteIo.println(F("ERR workload invalide"));
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
      piloteIo.println(F("ERR DC_LENGTH invalide"));
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
      piloteIo.println(F("ERR workload hors limites"));
      return;
    }
    workload[line[1] - '1'] = value;
    applyConsoleInstruction();
    return;
  }

  piloteIo.print(F("ERR commande inconnue: "));
  piloteIo.println(line);
}

void resetInputLine(InputLine &input) {
  input.len = 0;
  input.discard = false;
}

void readInput(Stream &stream, InputLine &input, bool fromHost) {
  while (stream.available() > 0) {
    char c = stream.read();
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      if (!input.discard) {
        input.buffer[input.len] = '\0';
        handleConsoleCommand(input.buffer, fromHost);
      }
      resetInputLine(input);
      continue;
    }

    if (input.discard) {
      continue;
    }

    if (input.len < sizeof(input.buffer) - 1) {
      input.buffer[input.len++] = c;
    } else {
      input.discard = true;
      lineOverflowCount++;
      piloteIo.println(F("ERR ligne trop longue"));
    }
  }
}

void readHostSerial() {
  readInput(Serial, hostInput, true);
}

void readConsoleLink() {
  readInput(consoleSerial, consoleInput, false);
}

void setup() {
  Serial.begin(BAUD_RATE);
  consoleSerial.begin(BAUD_RATE);
  cycleStartedAt = millis();
  lastDutySlotAt = millis();
  lastTelemetryAt = millis();
  simClock.setAtMs = millis();
  piloteIo.println(F("BOOT PILOTE_SIMULATION"));
  piloteIo.println(F("READY 9600"));
  printTelemetry();
}

void loop() {
  readHostSerial();
  readConsoleLink();
  updateDutyCycle();
  if ((unsigned long)(millis() - lastTelemetryAt) >= TELEMETRY_MS) {
    lastTelemetryAt = millis();
    printTelemetry();
  }
}
