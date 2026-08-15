/*
  Test zones Thermonuino - 10 s ON / 10 s OFF

  Les 4 zones commutent ensemble:
    - 10 secondes ON
    - 10 secondes OFF

  Rappel logique hardware:
    - LOW  sur commande zone = zone ON
    - HIGH sur commande zone = zone OFF, opto actif

  Watchdog externe:
    - WDI STWD100 sur PD2, pulse toutes les 250 ms

  LED statut:
    - active a LOW sur PD6
    - allumee quand les zones sont ON
    - eteinte quand les zones sont OFF
*/

const uint8_t PIN_ZONE1_CMD = A3;   // PC3
const uint8_t PIN_ZONE2_CMD = A2;   // PC2
const uint8_t PIN_ZONE3_CMD = A1;   // PC1
const uint8_t PIN_ZONE4_CMD = A0;   // PC0

const uint8_t PIN_WATCHDOG_WDI = 2; // PD2
const uint8_t PIN_LED_STATUS = 6;   // PD6, LED active a LOW

const uint8_t ZONE_CMD_PINS[4] = {
  PIN_ZONE1_CMD,
  PIN_ZONE2_CMD,
  PIN_ZONE3_CMD,
  PIN_ZONE4_CMD
};

const unsigned long ZONE_PERIOD_MS = 10000;
const unsigned long WATCHDOG_HEARTBEAT_MS = 250;

bool zonesOn = false;
bool watchdogLevel = false;
unsigned long lastZoneToggleAt = 0;
unsigned long lastWatchdogAt = 0;

void serviceWatchdog() {
  unsigned long now = millis();

  if (now - lastWatchdogAt >= WATCHDOG_HEARTBEAT_MS) {
    watchdogLevel = !watchdogLevel;
    digitalWrite(PIN_WATCHDOG_WDI, watchdogLevel ? HIGH : LOW);
    lastWatchdogAt = now;
  }
}

void setStatusLed(bool on) {
  digitalWrite(PIN_LED_STATUS, on ? LOW : HIGH);
}

void setAllZones(bool on) {
  zonesOn = on;

  for (uint8_t i = 0; i < 4; i++) {
    digitalWrite(ZONE_CMD_PINS[i], on ? LOW : HIGH);
  }

  setStatusLed(on);

  Serial.print(F("ZONES="));
  Serial.println(on ? F("ON") : F("OFF"));
}

void setup() {
  pinMode(PIN_WATCHDOG_WDI, OUTPUT);
  digitalWrite(PIN_WATCHDOG_WDI, LOW);

  pinMode(PIN_LED_STATUS, OUTPUT);
  setStatusLed(false);

  for (uint8_t i = 0; i < 4; i++) {
    pinMode(ZONE_CMD_PINS[i], OUTPUT);
    digitalWrite(ZONE_CMD_PINS[i], HIGH); // OFF au demarrage
  }

  Serial.begin(9600);
  Serial.println(F("Test zones: 10 s ON / 10 s OFF"));

  lastWatchdogAt = millis();
  lastZoneToggleAt = millis();
  setAllZones(true);
}

void loop() {
  unsigned long now = millis();

  serviceWatchdog();

  if (now - lastZoneToggleAt >= ZONE_PERIOD_MS) {
    lastZoneToggleAt = now;
    setAllZones(!zonesOn);
  }
}
