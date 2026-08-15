/*
  Test nouveau pinout Thermonuino Pilote

  Carte cible:
    - ATmega328P 5 V, horloge interne 8 MHz

  Fonctions testees:
    - Lecture TIC Linky: quand une trame DATE valide est recue, LED LINKY verte 1 s.
    - Cycle automatique: une zone ON pendant 5 s, puis zone suivante.
    - Commande zone:
        LOW  sur CMDZx = pas de courant dans l'opto = zone ON = LED zone allumee.
        HIGH sur CMDZx = courant dans CMDZx = zone OFF = LED zone eteinte.
    - Bouton zone: verrouille le mode manuel sur cette zone et inverse son etat.
    - Bouton statut: revient au cycle automatique entre les 4 zones.

  LEDCHAINDATA:
    Chaine de 6 LED adressables, ordre physique:
      0 LEDZONE4, 1 LEDZONE3, 2 LEDZONE2, 3 LEDZONE1, 4 LEDLINKY, 5 LEDSTATUT.

  Notes pinout:
    BTNMUX2 est sur PCINT12/A4.
    LINKY_RX est sur PCINT22/D6.
*/

#include <Adafruit_NeoPixel.h>
#include <SoftwareSerial.h>
#include <string.h>

const uint8_t PIN_LED_CHAIN_DATA = 5;  // PCINT21 / PD5 / D5
const uint8_t PIN_BTN_STATUS = 3;      // PCINT19 / PD3 / D3, appui vers GND
const uint8_t PIN_BTN_MUX1 = A5;       // PCINT13 / PC5 / A5, SWZ1/SWZ2
const uint8_t PIN_BTN_MUX2 = A4;       // PCINT12 / PC4 / A4, SWZ3/SWZ4 selon pinout.txt

const uint8_t PIN_LINKY_RX = 6;        // PCINT22 / PD6 / D6
const uint8_t PIN_TIC_UNUSED_TX = 4;   // Requis par SoftwareSerial, non cable

const uint8_t PIN_CMDZ1 = A3;          // PCINT11 / PC3 / A3
const uint8_t PIN_CMDZ2 = A2;          // PCINT10 / PC2 / A2
const uint8_t PIN_CMDZ3 = A1;          // PCINT9  / PC1 / A1
const uint8_t PIN_CMDZ4 = A0;          // PCINT8  / PC0 / A0

const uint8_t PIN_HEARTBEAT = 2;       // PCINT18 / PD2 / D2

const uint8_t ZONE_COUNT = 4;
const uint8_t LED_COUNT = 6;
const uint8_t LED_ZONE_INDEX[ZONE_COUNT] = {3, 2, 1, 0}; // Z1, Z2, Z3, Z4
const uint8_t LED_LINKY_INDEX = 4;
const uint8_t LED_STATUS_INDEX = 5;
const uint8_t ZONE_CMD_PINS[ZONE_COUNT] = {
  PIN_CMDZ1,
  PIN_CMDZ2,
  PIN_CMDZ3,
  PIN_CMDZ4
};

const unsigned long AUTO_ZONE_MS = 5000;
const unsigned long LINKY_OK_LED_MS = 1000;
const unsigned long WATCHDOG_HEARTBEAT_MS = 250;
const unsigned long DEBOUNCE_MS = 35;
const unsigned long SERIAL_STATUS_MS = 2000;

// Detection des boutons multiplexes:
// - non appuye: proche de 1023
// - SWZ1/SWZ3: 10 k vers GND, valeur ADC la plus basse
// - SWZ2/SWZ4: 10 k + 2.2 k vers GND, valeur ADC un peu plus haute
// Valeurs validees sur la carte de test avec lecture ADC stabilisee.
const int MUX1_10K_MAX = 255;          // SWZ1 / SWZ2, seuil 10 k / 12.2 k
const int MUX1_PRESSED_MAX = 980;      // au-dessus: aucun bouton considere appuye
const int MUX2_10K_MAX = 255;          // SWZ3 / SWZ4, seuil 10 k / 12.2 k
const int MUX2_PRESSED_MAX = 980;      // au-dessus: aucun bouton considere appuye

const bool TIC_INVERTED = false;

Adafruit_NeoPixel leds(LED_COUNT, PIN_LED_CHAIN_DATA, NEO_GRB + NEO_KHZ800);
SoftwareSerial ticSerial(PIN_LINKY_RX, PIN_TIC_UNUSED_TX, TIC_INVERTED);

char ticLine[128];
uint8_t ticLineLen = 0;
char currentDate[11] = "";
char currentTime[9] = "";

bool autoMode = true;
bool zoneOn[ZONE_COUNT] = {true, false, false, false};
uint8_t activeZone = 0;

bool watchdogLevel = false;
bool hasDateTime = false;
bool ledsDirty = true;
bool linkyLedRenderedOn = false;
uint32_t dateCount = 0;
uint32_t ticByteCount = 0;
uint32_t ticLineCount = 0;
uint32_t overflowCount = 0;

unsigned long lastZoneCycleAt = 0;
unsigned long lastWatchdogAt = 0;
unsigned long linkyLedUntil = 0;
unsigned long lastSerialStatusAt = 0;

bool lastStatusRaw = false;
bool stableStatus = false;
unsigned long statusDebounceAt = 0;

uint8_t lastMuxButton = 0;
uint8_t stableMuxButton = 0;
unsigned long muxDebounceAt = 0;

uint32_t color(uint8_t r, uint8_t g, uint8_t b);
void serviceWatchdog();
void applyZoneOutputs();
void renderLeds();
void setOnlyZoneOn(uint8_t zoneIndex);
void toggleManualZone(uint8_t zoneIndex);
bool debouncePressed(bool rawPressed, bool *lastRaw, bool *stable, unsigned long *changedAt);
int readAnalogStable(uint8_t pin);
uint8_t readMuxButton();
void printMuxPressDebug(uint8_t button);
uint8_t muxButtonPressedEvent();
bool sameText(const char *a, const char *b);
bool parseTicDate(const char *value);
void handleTicGroup(char *line);
void finishTicLine();
void handleTicByte(uint8_t rawByte);
void readTic();
void printStatus();

uint32_t color(uint8_t r, uint8_t g, uint8_t b) {
  return leds.Color(r, g, b);
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
    digitalWrite(ZONE_CMD_PINS[i], zoneOn[i] ? LOW : HIGH);
  }
}

void renderLeds() {
  bool linkyLedOn = millis() < linkyLedUntil;

  for (uint8_t i = 0; i < ZONE_COUNT; i++) {
    leds.setPixelColor(LED_ZONE_INDEX[i], zoneOn[i] ? color(0, 80, 0) : 0);
  }

  leds.setPixelColor(LED_LINKY_INDEX, linkyLedOn ? color(0, 100, 0) : 0);
  leds.setPixelColor(LED_STATUS_INDEX, autoMode ? color(0, 0, 70) : color(90, 35, 0));
  leds.show();

  ledsDirty = false;
  linkyLedRenderedOn = linkyLedOn;
}

void setOnlyZoneOn(uint8_t zoneIndex) {
  activeZone = zoneIndex;

  for (uint8_t i = 0; i < ZONE_COUNT; i++) {
    zoneOn[i] = (i == activeZone);
  }

  applyZoneOutputs();
  ledsDirty = true;
  renderLeds();
}

void toggleManualZone(uint8_t zoneIndex) {
  autoMode = false;
  activeZone = zoneIndex;

  for (uint8_t i = 0; i < ZONE_COUNT; i++) {
    if (i != activeZone) {
      zoneOn[i] = false;
    }
  }

  zoneOn[activeZone] = !zoneOn[activeZone];
  applyZoneOutputs();
  ledsDirty = true;
  renderLeds();

  Serial.print(F("MANUEL Z"));
  Serial.print(activeZone + 1);
  Serial.print(F("="));
  Serial.println(zoneOn[activeZone] ? F("ON") : F("OFF"));
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
  analogRead(pin); // jette la premiere lecture apres commutation du mux ADC
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

  if (mux1 <= MUX1_10K_MAX) {
    return 1;
  }
  if (mux1 <= MUX1_PRESSED_MAX) {
    return 2;
  }
  if (mux2 <= MUX2_10K_MAX) {
    return 3;
  }
  if (mux2 <= MUX2_PRESSED_MAX) {
    return 4;
  }

  return 0;
}

void printMuxPressDebug(uint8_t button) {
  int mux1 = readAnalogStable(PIN_BTN_MUX1);
  int mux2 = readAnalogStable(PIN_BTN_MUX2);

  Serial.print(F("BTN Z"));
  Serial.print(button);
  Serial.print(F(" mux1="));
  Serial.print(mux1);
  Serial.print(F(" mux2="));
  Serial.print(mux2);
  Serial.print(F(" seuil1="));
  Serial.print(MUX1_10K_MAX);
  Serial.print('/');
  Serial.print(MUX1_PRESSED_MAX);
  Serial.print(F(" seuil2="));
  Serial.print(MUX2_10K_MAX);
  Serial.print('/');
  Serial.println(MUX2_PRESSED_MAX);
}

uint8_t muxButtonPressedEvent() {
  unsigned long now = millis();
  uint8_t rawButton = readMuxButton();

  if (rawButton != lastMuxButton) {
    lastMuxButton = rawButton;
    muxDebounceAt = now;
  }

  if ((now - muxDebounceAt) >= DEBOUNCE_MS && rawButton != stableMuxButton) {
    stableMuxButton = rawButton;
    return stableMuxButton;
  }

  return 0;
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
  dateCount++;
  linkyLedUntil = millis() + LINKY_OK_LED_MS;
  ledsDirty = true;

  Serial.print(F("DATE_TIC "));
  Serial.print(currentDate);
  Serial.print(' ');
  Serial.println(currentTime);

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

  if (c == 0x03) {
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

void printStatus() {
  int mux1 = readAnalogStable(PIN_BTN_MUX1);
  int mux2 = readAnalogStable(PIN_BTN_MUX2);

  Serial.print(F("MODE="));
  Serial.print(autoMode ? F("AUTO") : F("MANUEL"));
  Serial.print(F(" Z="));
  Serial.print(activeZone + 1);
  Serial.print(F(" ETATS="));

  for (uint8_t i = 0; i < ZONE_COUNT; i++) {
    Serial.print(zoneOn[i] ? '1' : '0');
  }

  Serial.print(F(" TIC octets="));
  Serial.print(ticByteCount);
  Serial.print(F(" lignes="));
  Serial.print(ticLineCount);
  Serial.print(F(" dates="));
  Serial.print(dateCount);
  Serial.print(F(" overflow="));
  Serial.print(overflowCount);
  Serial.print(F(" mux1="));
  Serial.print(mux1);
  Serial.print(F(" mux2="));
  Serial.println(mux2);
}

void setup() {
  pinMode(PIN_HEARTBEAT, OUTPUT);
  digitalWrite(PIN_HEARTBEAT, LOW);

  pinMode(PIN_BTN_STATUS, INPUT_PULLUP);
  pinMode(PIN_BTN_MUX1, INPUT_PULLUP);
  pinMode(PIN_BTN_MUX2, INPUT_PULLUP);

  for (uint8_t i = 0; i < ZONE_COUNT; i++) {
    pinMode(ZONE_CMD_PINS[i], OUTPUT);
    digitalWrite(ZONE_CMD_PINS[i], HIGH);
  }

  leds.begin();
  leds.clear();
  leds.setBrightness(40);
  leds.show();

  Serial.begin(9600);
  ticSerial.begin(9600);
  ticSerial.listen();

  setOnlyZoneOn(0);

  lastZoneCycleAt = millis();
  lastWatchdogAt = millis();
  lastSerialStatusAt = millis();

  Serial.println(F("Test nouveau pinout Thermonuino"));
  Serial.println(F("AUTO: une zone ON pendant 5 s. Bouton zone: verrouille + inverse. Statut: AUTO."));
  Serial.println(F("LINKY_RX=D6/PCINT22, BTNMUX2=A4/PCINT12."));
}

void loop() {
  unsigned long now = millis();

  serviceWatchdog();
  readTic();

  if (debouncePressed(digitalRead(PIN_BTN_STATUS) == LOW, &lastStatusRaw, &stableStatus, &statusDebounceAt)) {
    autoMode = true;
    setOnlyZoneOn(activeZone);
    lastZoneCycleAt = now;
    Serial.println(F("STATUT -> AUTO"));
  }

  uint8_t pressedZone = muxButtonPressedEvent();
  if (pressedZone >= 1 && pressedZone <= ZONE_COUNT) {
    printMuxPressDebug(pressedZone);
    toggleManualZone(pressedZone - 1);
  }

  if (autoMode && (now - lastZoneCycleAt >= AUTO_ZONE_MS)) {
    lastZoneCycleAt = now;
    setOnlyZoneOn((activeZone + 1) % ZONE_COUNT);
  }

  if (ledsDirty || linkyLedRenderedOn != (now < linkyLedUntil)) {
    renderLeds();
  }

  if (now - lastSerialStatusAt >= SERIAL_STATUS_MS) {
    lastSerialStatusAt = now;
    printStatus();
  }
}
