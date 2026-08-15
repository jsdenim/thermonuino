/*
  Test PCB Thermonuino Pilote - TIC + zones + boutons

  TIC:
    - PC4/A4 en reception directe depuis l'opto/adaptation.
    - Affiche la date/heure une seule fois au demarrage.
    - Affiche regulierement uniquement la puissance instantanee soutiree.

  Tests PCB:
    - Cycle les 4 zones en boucle.
    - Un appui sur un bouton Zone bloque le test sur la zone correspondante.
    - Un appui sur le bouton Statut relance le cycle auto.

  Sortie debug:
    - Serial materiel PD1/PD0 a 9600 bauds

  Watchdog externe:
    - WDI STWD100 sur PD2, pulse toutes les 250 ms

  TIC standard:
    - 9600 bauds, 7 bits, parite paire, 1 stop
    - SoftwareSerial lit en 8N1, puis le code garde raw & 0x7F.

  Etiquettes utilisees en TIC standard:
    - DATE   : horodate, ex: E260723211604
    - SINSTS : puissance apparente instantanee soutiree, en VA
*/

#include <SoftwareSerial.h>
#include <string.h>

const uint8_t PIN_BTN_STATUS = 3;    // PD3
const uint8_t PIN_BTN_ZONE1  = 7;    // PD7
const uint8_t PIN_BTN_ZONE2  = 8;    // PB0
const uint8_t PIN_BTN_ZONE3  = 9;    // PB1
const uint8_t PIN_BTN_ZONE4  = 10;   // PB2

const uint8_t PIN_ZONE1_CMD  = A3;   // PC3
const uint8_t PIN_ZONE2_CMD  = A2;   // PC2
const uint8_t PIN_ZONE3_CMD  = A1;   // PC1
const uint8_t PIN_ZONE4_CMD  = A0;   // PC0

const uint8_t PIN_TIC_RX = A4;        // PC4, reception TIC depuis opto/adaptation
const uint8_t PIN_TIC_UNUSED_TX = A5; // PC5, requis par SoftwareSerial, non utilise
const uint8_t PIN_WATCHDOG_WDI = 2;   // PD2, entree WDI du STWD100
const uint8_t PIN_LED_STATUS = 6;     // PD6, LED active a LOW

const uint8_t BUTTON_PINS[4] = {
  PIN_BTN_ZONE1,
  PIN_BTN_ZONE2,
  PIN_BTN_ZONE3,
  PIN_BTN_ZONE4
};

const uint8_t ZONE_CMD_PINS[4] = {
  PIN_ZONE1_CMD,
  PIN_ZONE2_CMD,
  PIN_ZONE3_CMD,
  PIN_ZONE4_CMD
};

const unsigned long ZONE_CYCLE_MS = 1000;
const unsigned long DEBOUNCE_MS = 30;
const unsigned long WATCHDOG_HEARTBEAT_MS = 250;
const unsigned long POWER_PRINT_MS = 2000;
const unsigned long SETUP_DATE_WAIT_MS = 5000;
const unsigned long BOOT_BLINK_HALF_PERIOD_MS = 75;
const uint8_t BOOT_BLINK_COUNT = 10;
const bool BUTTONS_ENABLED = true;

// Passer a true si l'etage opto inverse le signal TIC.
const bool TIC_INVERTED = false;

SoftwareSerial ticSerial(PIN_TIC_RX, PIN_TIC_UNUSED_TX, TIC_INVERTED);

char ticLine[128];
uint8_t ticLineLen = 0;

char currentDate[11] = ""; // YYYY-MM-DD
char currentTime[9] = "";  // HH:MM:SS
char currentPowerVa[8] = "";

bool autoMode = true;
uint8_t activeZone = 0;
uint32_t ticByteCount = 0;
uint32_t ticLineCount = 0;
uint32_t overflowCount = 0;
uint32_t dateCount = 0;
uint32_t powerCount = 0;
unsigned long lastPowerPrintAt = 0;
unsigned long lastZoneCycleAt = 0;
unsigned long lastWatchdogAt = 0;
bool watchdogLevel = false;
bool hasDateTime = false;
bool hasPower = false;
bool setupDatePrinted = false;

bool lastRawButtons[5] = {false, false, false, false, false};
bool stableButtons[5] = {false, false, false, false, false};
unsigned long lastDebounceAt[5] = {0, 0, 0, 0, 0};

void setStatusLed(bool on) {
  digitalWrite(PIN_LED_STATUS, on ? LOW : HIGH);
}

void pulseWatchdog() {
  watchdogLevel = !watchdogLevel;
  digitalWrite(PIN_WATCHDOG_WDI, watchdogLevel ? HIGH : LOW);
  lastWatchdogAt = millis();
}

void serviceWatchdog() {
  unsigned long now = millis();
  if (now - lastWatchdogAt >= WATCHDOG_HEARTBEAT_MS) {
    pulseWatchdog();
  }
}

void bootBlink() {
  const uint8_t transitions = BOOT_BLINK_COUNT * 2;

  for (uint8_t i = 0; i < transitions; i++) {
    for (uint8_t zone = 0; zone < 4; zone++) {
      digitalWrite(ZONE_CMD_PINS[zone], HIGH);
    }
    setStatusLed((i % 2) == 0);
    unsigned long startAt = millis();
    while (millis() - startAt < BOOT_BLINK_HALF_PERIOD_MS) {
      for (uint8_t zone = 0; zone < 4; zone++) {
        digitalWrite(ZONE_CMD_PINS[zone], HIGH);
      }
      serviceWatchdog();
    }
  }

  setStatusLed(false);
}

void setActiveZone(uint8_t zoneIndex) {
  activeZone = zoneIndex;

  for (uint8_t i = 0; i < 4; i++) {
    // Logique inversee:
    // HIGH = courant envoye = opto actif = sortie commandee arretee.
    // LOW  = pas de courant = zone testee active.
    digitalWrite(ZONE_CMD_PINS[i], i == activeZone ? LOW : HIGH);
  }
}

void setAllOptosOn() {
  for (uint8_t i = 0; i < 4; i++) {
    digitalWrite(ZONE_CMD_PINS[i], HIGH);
  }
}

bool buttonPressedEvent(uint8_t index, bool rawPressed, unsigned long now) {
  bool event = false;

  if (rawPressed != lastRawButtons[index]) {
    lastRawButtons[index] = rawPressed;
    lastDebounceAt[index] = now;
  }

  if ((now - lastDebounceAt[index]) >= DEBOUNCE_MS && rawPressed != stableButtons[index]) {
    stableButtons[index] = rawPressed;
    event = rawPressed;
  }

  return event;
}

bool sameText(const char *a, const char *b) {
  return strcmp(a, b) == 0;
}

bool parseTicDate(const char *value) {
  // Format TIC standard: SYYMMDDhhmmss, ou S = saison/etat contractuel.
  // Exemple recu: E260723211604 => 2026-07-23 21:16:04.
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
  dateCount++;
  return true;
}

void printDateTimeOnce() {
  if (!hasDateTime || setupDatePrinted) {
    return;
  }

  setupDatePrinted = true;
  Serial.print(F("DATE_TIC "));
  Serial.print(currentDate);
  Serial.print(' ');
  Serial.println(currentTime);
}

void handleTicGroup(char *line) {
  char *label = strtok(line, "\t");
  char *value = strtok(NULL, "\t");

  if (label == NULL || value == NULL) {
    return;
  }

  if (sameText(label, "DATE")) {
    parseTicDate(value);
    printDateTimeOnce();
    return;
  }

  if (sameText(label, "SINSTS")) {
    strncpy(currentPowerVa, value, sizeof(currentPowerVa) - 1);
    currentPowerVa[sizeof(currentPowerVa) - 1] = '\0';
    hasPower = true;
    powerCount++;
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

  if (c == 0x02) { // STX
    ticLineLen = 0;
    return;
  }

  if (c == 0x03) { // ETX
    finishTicLine();
    return;
  }

  if (c == '\n') {
    ticLineLen = 0;
    return;
  }

  if (c == '\r') {
    finishTicLine();
    return;
  }

  if (c < 0x20 && c != '\t') {
    return;
  }

  if (ticLineLen < sizeof(ticLine) - 1) {
    ticLine[ticLineLen++] = c;
  } else {
    overflowCount++;
    ticLineLen = 0;
  }
}

void readTic() {
  while (ticSerial.available() > 0) {
    handleTicByte((uint8_t)ticSerial.read());
  }
}

void printRegularPower() {
  Serial.print(F("PUISSANCE_SOUTIREE="));
  if (hasPower) {
    Serial.print(currentPowerVa);
    Serial.println(F(" VA"));
  } else {
    Serial.println(F("NA"));
  }
}

void printDebugStatus() {
  Serial.print(F("DEBUG rx="));
  Serial.print(digitalRead(PIN_TIC_RX) ? F("HIGH") : F("LOW"));
  Serial.print(F(" octets="));
  Serial.print(ticByteCount);
  Serial.print(F(" lignes="));
  Serial.print(ticLineCount);
  Serial.print(F(" dates="));
  Serial.print(dateCount);
  Serial.print(F(" puissances="));
  Serial.print(powerCount);
  Serial.print(F(" overflow="));
  Serial.println(overflowCount);
}

void handleSerialCommand(const char *command) {
  if (command[0] == '\0') {
    return;
  }

  if (strcmp(command, "AUTO") == 0) {
    autoMode = true;
    lastZoneCycleAt = millis();
    Serial.println(F("OK AUTO"));
  } else if (strcmp(command, "DEBUG") == 0) {
    printDebugStatus();
  } else if (strcmp(command, "OFF") == 0) {
    autoMode = false;
    setAllOptosOn();
    Serial.println(F("OK OFF"));
  } else if (command[0] == 'Z' && command[1] >= '1' && command[1] <= '4' && command[2] == '\0') {
    autoMode = false;
    setActiveZone(command[1] - '1');
    Serial.print(F("OK ZONE "));
    Serial.println(activeZone + 1);
  } else {
    Serial.print(F("COMMANDE INCONNUE: "));
    Serial.println(command);
  }
}

void readSerialCommands() {
  static char commandBuffer[16];
  static uint8_t commandLen = 0;

  while (Serial.available() > 0) {
    char c = Serial.read();

    if (c == '\r') {
      continue;
    }

    if (c == '\n') {
      commandBuffer[commandLen] = '\0';
      handleSerialCommand(commandBuffer);
      commandLen = 0;
    } else if (commandLen < sizeof(commandBuffer) - 1) {
      commandBuffer[commandLen++] = c;
    } else {
      commandLen = 0;
      Serial.println(F("ERREUR commande trop longue"));
    }
  }
}

void waitForInitialDate() {
  unsigned long startAt = millis();

  while (!setupDatePrinted && millis() - startAt < SETUP_DATE_WAIT_MS) {
    serviceWatchdog();
    readTic();
  }

  if (!setupDatePrinted) {
    Serial.println(F("DATE_TIC non recue pendant setup"));
    setupDatePrinted = true;
  }
}

void setup() {
  pinMode(PIN_WATCHDOG_WDI, OUTPUT);
  digitalWrite(PIN_WATCHDOG_WDI, LOW);
  pinMode(PIN_LED_STATUS, OUTPUT);
  pinMode(PIN_TIC_RX, INPUT);
  pinMode(PIN_BTN_STATUS, INPUT);
  for (uint8_t i = 0; i < 4; i++) {
    pinMode(BUTTON_PINS[i], INPUT);
    pinMode(ZONE_CMD_PINS[i], OUTPUT);
  }

  pulseWatchdog();
  setAllOptosOn();
  bootBlink();

  Serial.begin(9600);
  ticSerial.begin(9600);
  ticSerial.listen();

  setActiveZone(activeZone);

  Serial.println(F("Test PCB: zones + boutons + TIC DATE/SINSTS"));
  Serial.println(F("Debug: PD1 TX / PD0 RX, 9600 bauds"));
  Serial.println(F("Commandes: AUTO, DEBUG, Z1, Z2, Z3, Z4, OFF"));
  Serial.print(F("TIC_INVERTED="));
  Serial.println(TIC_INVERTED ? F("true") : F("false"));
  Serial.println(F("Attente DATE TIC pendant setup..."));
  waitForInitialDate();

  lastZoneCycleAt = millis();
  lastPowerPrintAt = millis();
}

void loop() {
  unsigned long now = millis();

  serviceWatchdog();
  readTic();
  readSerialCommands();

  if (BUTTONS_ENABLED) {
    if (buttonPressedEvent(0, digitalRead(PIN_BTN_STATUS), now)) {
      autoMode = true;
      lastZoneCycleAt = now;
      Serial.println(F("Bouton Statut -> AUTO"));
    }

    for (uint8_t i = 0; i < 4; i++) {
      if (buttonPressedEvent(i + 1, digitalRead(BUTTON_PINS[i]), now)) {
        autoMode = false;
        setActiveZone(i);
        Serial.print(F("Bouton Zone "));
        Serial.print(i + 1);
        Serial.println(F(" -> verrouillage"));
      }
    }
  }

  if (autoMode && (now - lastZoneCycleAt >= ZONE_CYCLE_MS)) {
    lastZoneCycleAt = now;
    setActiveZone((activeZone + 1) % 4);
  }

  if (hasPower) {
    setStatusLed(true);
  } else if (ticByteCount > 0) {
    setStatusLed((now / 500) % 2 == 0);
  } else {
    setStatusLed((now / 150) % 2 == 0);
  }

  if (now - lastPowerPrintAt >= POWER_PRINT_MS) {
    lastPowerPrintAt = now;
    printRegularPower();
  }
}
