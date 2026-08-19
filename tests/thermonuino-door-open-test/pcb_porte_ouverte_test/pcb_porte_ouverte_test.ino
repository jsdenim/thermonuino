/*
  Test PCB Thermonuino Porte Ouverte

  Cible: ATmega328P 3.3 V / 8 MHz.
  Pinout d'apres Specifications Thermonuio.md / PCB Porte Ouverte.

  Fonctions testees:
    - LED PCB sur PCINT11 / A3, active HIGH ;
    - DOOR_OPEN sur PCINT18 / D2, REED en parallele vers GND, actif LOW ;
    - bouton sur PCINT19 / D3, arrive a 3.3 V a l'appui, actif HIGH ;
    - CC1101 sur SPI avec alimentation RF_EN activee uniquement pendant le test.

  Codes LED:
    - demarrage: 1 blink court ;
    - test CC1101 OK au demarrage: 5 blinks courts ;
    - test CC1101 KO/timeout au demarrage: 2 blinks longs ;
    - REED ferme: LED fixe ;
    - bouton appuye: clignotement rapide tant que le bouton reste actif.

  Le test SPI a des timeouts pour ne pas rester bloque si le CC1101 ne repond
  pas ou si MISO reste haut.
*/

#include <SPI.h>

const uint8_t PIN_RF_EN = 8;       // PCINT0 / PB0 / D8
const uint8_t PIN_RF_GDO0 = 9;     // PCINT1 / PB1 / D9
const uint8_t PIN_RF_CSN = 10;     // PCINT2 / PB2 / D10
const uint8_t PIN_RF_MOSI = 11;    // PCINT3 / PB3 / D11
const uint8_t PIN_RF_MISO = 12;    // PCINT4 / PB4 / D12
const uint8_t PIN_RF_SCK = 13;     // PCINT5 / PB5 / D13

const uint8_t PIN_DOOR_OPEN = 2;   // PCINT18 / PD2 / D2, REED vers GND
const uint8_t PIN_BUTTON = 3;      // PCINT19 / PD3 / D3, appui vers 3.3 V
const uint8_t PIN_LED = A3;        // PCINT11 / PC3 / A3, active HIGH

// AO3401A P-MOS high-side, comme les tests de la partie mesure: gate LOW = RF ON.
const uint8_t RF_EN_ON_LEVEL = HIGH;
const uint8_t RF_EN_OFF_LEVEL = (RF_EN_ON_LEVEL == LOW) ? HIGH : LOW;

const uint8_t CC1101_READ_BURST = 0xC0;
const uint8_t CC1101_PARTNUM = 0x30;
const uint8_t CC1101_VERSION = 0x31;

const unsigned int RF_POWER_UP_MS = 20;
const unsigned int CC1101_READY_TIMEOUT_MS = 15;
const unsigned int CC1101_TOTAL_TIMEOUT_MS = 120;
const unsigned int BUTTON_BLINK_MS = 90;

const SPISettings RF_SPI_SETTINGS(1000000, MSBFIRST, SPI_MODE0);

unsigned long lastButtonBlinkAt = 0;
bool buttonBlinkOn = false;

void ledOn() {
  digitalWrite(PIN_LED, HIGH);
}

void ledOff() {
  digitalWrite(PIN_LED, LOW);
}

void blinkLed(uint8_t count, unsigned int onMs, unsigned int offMs) {
  for (uint8_t i = 0; i < count; i++) {
    ledOn();
    delay(onMs);
    ledOff();
    delay(offMs);
  }
}

void rfPowerOn() {
  digitalWrite(PIN_RF_CSN, HIGH);
  digitalWrite(PIN_RF_EN, RF_EN_ON_LEVEL);
  delay(RF_POWER_UP_MS);
}

void rfPowerOff() {
  digitalWrite(PIN_RF_CSN, HIGH);
  digitalWrite(PIN_RF_MOSI, LOW);
  digitalWrite(PIN_RF_SCK, LOW);
  digitalWrite(PIN_RF_EN, RF_EN_OFF_LEVEL);
}

bool waitCc1101Ready(unsigned int timeoutMs) {
  const unsigned long startedAt = millis();
  while (digitalRead(PIN_RF_MISO) == HIGH) {
    if ((uint32_t)(millis() - startedAt) >= timeoutMs) {
      return false;
    }
  }

  return true;
}

bool cc1101ReadStatusRegister(uint8_t address, uint8_t &value) {
  digitalWrite(PIN_RF_CSN, LOW);
  if (!waitCc1101Ready(CC1101_READY_TIMEOUT_MS)) {
    digitalWrite(PIN_RF_CSN, HIGH);
    return false;
  }

  SPI.transfer(CC1101_READ_BURST | address);
  value = SPI.transfer(0x00);
  digitalWrite(PIN_RF_CSN, HIGH);
  return true;
}

bool testCc1101Spi() {
  const unsigned long startedAt = millis();
  bool ok = false;

  rfPowerOn();
  SPI.beginTransaction(RF_SPI_SETTINGS);

  uint8_t partnum = 0xFF;
  uint8_t version1 = 0xFF;
  uint8_t version2 = 0xFF;
  const bool readOk =
      cc1101ReadStatusRegister(CC1101_PARTNUM, partnum) &&
      cc1101ReadStatusRegister(CC1101_VERSION, version1) &&
      cc1101ReadStatusRegister(CC1101_VERSION, version2);

  if ((uint32_t)(millis() - startedAt) < CC1101_TOTAL_TIMEOUT_MS) {
    const bool busIsNotFloating = version1 != 0x00 && version1 != 0xFF;
    const bool responseIsStable = version1 == version2;
    const bool partnumLooksValid = partnum == 0x00;
    ok = readOk && busIsNotFloating && responseIsStable && partnumLooksValid;
  }

  SPI.endTransaction();
  rfPowerOff();
  return ok;
}

void updateInputLed() {
  const bool reedClosed = digitalRead(PIN_DOOR_OPEN) == LOW;
  const bool buttonPressed = digitalRead(PIN_BUTTON) == HIGH;

  if (buttonPressed) {
    if ((uint32_t)(millis() - lastButtonBlinkAt) >= BUTTON_BLINK_MS) {
      lastButtonBlinkAt = millis();
      buttonBlinkOn = !buttonBlinkOn;
      digitalWrite(PIN_LED, buttonBlinkOn ? HIGH : LOW);
    }
    return;
  }

  buttonBlinkOn = false;
  if (reedClosed) {
    ledOn();
  } else {
    ledOff();
  }
}

void setup() {
  pinMode(PIN_LED, OUTPUT);
  ledOff();

  pinMode(PIN_DOOR_OPEN, INPUT_PULLUP);
  pinMode(PIN_BUTTON, INPUT);

  pinMode(PIN_RF_EN, OUTPUT);
  pinMode(PIN_RF_GDO0, INPUT);
  pinMode(PIN_RF_CSN, OUTPUT);
  pinMode(PIN_RF_MOSI, OUTPUT);
  pinMode(PIN_RF_MISO, INPUT);
  pinMode(PIN_RF_SCK, OUTPUT);
  
  rfPowerOff();

  SPI.begin();

  blinkLed(1, 80, 150);
  delay(1000);
  const bool rfOk = testCc1101Spi();
  blinkLed(rfOk ? 5 : 2, rfOk ? 100 : 350, rfOk ? 120 : 350);
  
}

void loop() {
  updateInputLed();
  delay(5);
}
