/*
  Test PCB Thermonuino - switch 2 sens + bouton via reseau WAKE

  Cible: ATmega328P 3 V / 8 MHz, profil Arduino Pro Mini 3.3 V / 8 MHz.

  D'apres pinout.txt:
    WAKE      PCINT18 / PD2 / D2
    LED       PCINT21 / PD5 / D5
    RF_EN     PCINT0  / PB0 / D8
    RF_CSN    PCINT2  / PB2 / D10
    RF_MOSI   PCINT3  / PB3 / D11
    RF_MISO   PCINT4  / PB4 / D12
    RF_SCK    PCINT5  / PB5 / D13
    CMD_SENS1 PCINT16 / PD0 / D0, actif a GND
    CMD_SENS2 PCINT9  / PC1 / A1, actif a GND
    CMD_BTN   PCINT11 / PC3 / A3, actif a GND

  Comportement:
    - veille power-down
    - interruption pin-change sur WAKE
    - flash rapide au reveil
    - lecture du switch apres debounce
    - 1 blink lent = bouton central
    - 2 blinks lents = sens 1
    - 3 blinks lents = sens 2
    - bouton central: test SPI CC1101, 5 blinks lents si OK
*/

#include <SPI.h>
#include <avr/interrupt.h>
#include <avr/power.h>
#include <avr/sleep.h>

const byte PIN_WAKE = 2;
const byte PIN_LED = 5;
const byte PIN_RF_EN = 8;
const byte PIN_RF_CSN = 10;
const byte PIN_CMD_SENS1 = 0;
const byte PIN_CMD_SENS2 = A1;
const byte PIN_CMD_BTN = A3;

// AO3401A P-MOS en high-side: gate LOW = alim RF ON. Mettre HIGH si besoin.
const byte RF_EN_ON_LEVEL = LOW;
const byte RF_EN_OFF_LEVEL = (RF_EN_ON_LEVEL == LOW) ? HIGH : LOW;

const unsigned int WAKE_FLASH_MS = 40;
const unsigned int DEBOUNCE_MS = 35;
const unsigned int RF_POWER_UP_MS = 20;
const unsigned int SLOW_ON_MS = 220;
const unsigned int SLOW_OFF_MS = 220;
const unsigned int CYCLE_PAUSE_MS = 700;

const byte CC1101_READ_BURST = 0xC0;
const byte CC1101_PARTNUM = 0x30;
const byte CC1101_VERSION = 0x31;

volatile bool wakeInterruptSeen = false;

ISR(PCINT2_vect) {
  wakeInterruptSeen = true;
}

static void ledOff() {
  digitalWrite(PIN_LED, LOW);
}

static void blinkLed(byte count, unsigned int onMs, unsigned int offMs) {
  for (byte i = 0; i < count; i++) {
    digitalWrite(PIN_LED, HIGH);
    delay(onMs);
    digitalWrite(PIN_LED, LOW);
    delay(offMs);
  }
}

static byte readCommandBlinkCount() {
  if (digitalRead(PIN_CMD_BTN) == LOW) {
    return 1;
  }
  if (digitalRead(PIN_CMD_SENS1) == LOW) {
    return 2;
  }
  if (digitalRead(PIN_CMD_SENS2) == LOW) {
    return 3;
  }

  return 0;
}

static byte cc1101ReadStatusRegister(byte address) {
  digitalWrite(PIN_RF_CSN, LOW);

  unsigned long startedAt = millis();
  while (digitalRead(MISO) == HIGH && (millis() - startedAt) < 10) {
    // Le CC1101 force SO/MISO a 0 quand son interface SPI est prete.
  }

  SPI.transfer(CC1101_READ_BURST | address);
  byte value = SPI.transfer(0x00);
  digitalWrite(PIN_RF_CSN, HIGH);

  return value;
}

static bool testCc1101Spi() {
  digitalWrite(PIN_RF_EN, RF_EN_ON_LEVEL);
  delay(RF_POWER_UP_MS);

  SPI.begin();
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));

  byte partnum = cc1101ReadStatusRegister(CC1101_PARTNUM);
  byte version1 = cc1101ReadStatusRegister(CC1101_VERSION);
  byte version2 = cc1101ReadStatusRegister(CC1101_VERSION);

  SPI.endTransaction();
  SPI.end();

  digitalWrite(PIN_RF_CSN, HIGH);
  digitalWrite(PIN_RF_EN, RF_EN_OFF_LEVEL);

  bool busIsNotFloating = (version1 != 0x00) && (version1 != 0xFF);
  bool responseIsStable = (version1 == version2);
  bool partnumLooksValid = (partnum == 0x00);

  return busIsNotFloating && responseIsStable && partnumLooksValid;
}

static void enableWakePcint() {
  cli();

  // D2 = PD2 = PCINT18, donc groupe PCIE2 / masque PCMSK2.
  PCICR |= _BV(PCIE2);
  PCMSK2 |= _BV(PCINT18);
  PCIFR |= _BV(PCIF2);

  sei();
}

static void goToSleepUntilWakeChanges() {
  wakeInterruptSeen = false;

  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  cli();
  PCIFR |= _BV(PCIF2);
  sleep_enable();
  sleep_bod_disable();
  sei();
  sleep_cpu();

  sleep_disable();
}

void setup() {
  pinMode(PIN_LED, OUTPUT);
  ledOff();

  pinMode(PIN_RF_EN, OUTPUT);
  digitalWrite(PIN_RF_EN, RF_EN_OFF_LEVEL);

  pinMode(PIN_RF_CSN, OUTPUT);
  digitalWrite(PIN_RF_CSN, HIGH);

  pinMode(PIN_CMD_SENS1, INPUT_PULLUP);
  pinMode(PIN_CMD_SENS2, INPUT_PULLUP);
  pinMode(PIN_CMD_BTN, INPUT_PULLUP);

  /*
    Si WAKE a deja un pull-up/pull-down materiel sur le PCB, remplacer par
    INPUT pour ne pas biaiser le reseau. INPUT_PULLUP est pratique pour un
    premier test si WAKE est tire a GND par les commandes.
  */
  pinMode(PIN_WAKE, INPUT_PULLUP);

  ADCSRA &= ~_BV(ADEN);
  power_adc_disable();

  enableWakePcint();
}

void loop() {
  goToSleepUntilWakeChanges();

  blinkLed(1, WAKE_FLASH_MS, WAKE_FLASH_MS);
  delay(DEBOUNCE_MS);

  byte blinkCount = readCommandBlinkCount();
  if (blinkCount > 0) {
    blinkLed(blinkCount, SLOW_ON_MS, SLOW_OFF_MS);
  }

  if (blinkCount == 1 && testCc1101Spi()) {
    delay(CYCLE_PAUSE_MS);
    blinkLed(5, SLOW_ON_MS, SLOW_OFF_MS);
  }

  delay(CYCLE_PAUSE_MS);
  ledOff();
}
