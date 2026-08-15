/*
  Diagnostic ePaper Good Display 0.97" / SSD1680 sur PCB Thermonuino.

  But:
    Verifier que le controleur de l'ecran repond au moins via BUSY.
    Le bus SPI ePaper Good Display est normalement write-only cote MCU:
    on ne peut pas lire un ID fiable comme avec le CC1101. BUSY est le retour.

  Codes LED:
    1 blink court au demarrage
    3 blinks = BUSY a bouge pendant RESET ou SWRESET
    5 blinks = BUSY a bouge pendant refresh aussi
    2 blinks = pas de reaction BUSY detectee

  D'apres pinout.txt:
    EPD_CS    PCINT22 / PD6 / D6
    EPD_RES   PCINT20 / PD4 / D4
    EPD_DC    PCINT19 / PD3 / D3
    EPD_BUSY  PCINT10 / PC2 / A2
    LED       PCINT21 / PD5 / D5
    RF_CSN    PCINT2  / PB2 / D10, garder HIGH
    RF_EN     PCINT0  / PB0 / D8, garder RF coupe
*/

#include <SPI.h>

const byte PIN_EPD_BUSY = A2;
const byte PIN_EPD_RST = 4;
const byte PIN_EPD_DC = 3;
const byte PIN_EPD_CS = 6;
const byte PIN_LED = 5;
const byte PIN_RF_CSN = 10;
const byte PIN_RF_EN = 8;

// AO3401A P-MOS en high-side: gate HIGH = RF OFF. Inverser si ton PCB differe.
const byte RF_EN_OFF_LEVEL = HIGH;

const uint16_t EPD_WIDTH = 184;
const uint16_t EPD_HEIGHT = 88;
const byte EPD_WIDTH_BYTES = EPD_WIDTH / 8;

const SPISettings EPD_SPI_SETTINGS(500000, MSBFIRST, SPI_MODE0);

void blinkLed(byte count, unsigned int onMs = 130, unsigned int offMs = 180) {
  delay(500);
  for (byte i = 0; i < count; i++) {
    digitalWrite(PIN_LED, HIGH);
    delay(onMs);
    digitalWrite(PIN_LED, LOW);
    delay(offMs);
  }
}

void epdCommand(byte command) {
  digitalWrite(PIN_EPD_DC, LOW);
  digitalWrite(PIN_EPD_CS, LOW);
  SPI.transfer(command);
  digitalWrite(PIN_EPD_CS, HIGH);
}

void epdData(byte data) {
  digitalWrite(PIN_EPD_DC, HIGH);
  digitalWrite(PIN_EPD_CS, LOW);
  SPI.transfer(data);
  digitalWrite(PIN_EPD_CS, HIGH);
}

bool waitForBusyChange(byte initialLevel, unsigned long timeoutMs) {
  unsigned long startedAt = millis();

  while (millis() - startedAt < timeoutMs) {
    if (digitalRead(PIN_EPD_BUSY) != initialLevel) {
      return true;
    }
    delay(2);
  }

  return false;
}

bool waitForBusyStable(byte targetLevel, unsigned long timeoutMs) {
  unsigned long startedAt = millis();

  while (millis() - startedAt < timeoutMs) {
    if (digitalRead(PIN_EPD_BUSY) == targetLevel) {
      delay(20);
      if (digitalRead(PIN_EPD_BUSY) == targetLevel) {
        return true;
      }
    }
    delay(5);
  }

  return false;
}

bool pulseResetAndWatchBusy() {
  byte before = digitalRead(PIN_EPD_BUSY);

  digitalWrite(PIN_EPD_RST, LOW);
  delay(20);
  digitalWrite(PIN_EPD_RST, HIGH);

  bool changed = waitForBusyChange(before, 300);
  delay(200);

  return changed;
}

bool sendSwResetAndWatchBusy() {
  byte before = digitalRead(PIN_EPD_BUSY);

  epdCommand(0x12);

  bool changed = waitForBusyChange(before, 1000);

  // Les samples Good Display attendent BUSY LOW. Si ton panneau est inverse,
  // le fait d'avoir vu une transition reste le signal important pour ce diag.
  waitForBusyStable(LOW, 5000);

  return changed;
}

void epdMinimalInit() {
  epdCommand(0x01);
  epdData((EPD_HEIGHT - 1) & 0xFF);
  epdData((EPD_HEIGHT - 1) >> 8);
  epdData(0x00);

  epdCommand(0x11);
  epdData(0x01);

  epdCommand(0x44);
  epdData(0x00);
  epdData(EPD_WIDTH_BYTES - 1);

  epdCommand(0x45);
  epdData((EPD_HEIGHT - 1) & 0xFF);
  epdData((EPD_HEIGHT - 1) >> 8);
  epdData(0x00);
  epdData(0x00);

  epdCommand(0x4E);
  epdData(0x00);
  epdCommand(0x4F);
  epdData((EPD_HEIGHT - 1) & 0xFF);
  epdData((EPD_HEIGHT - 1) >> 8);

  epdCommand(0x3C);
  epdData(0x05);

  epdCommand(0x18);
  epdData(0x80);

  epdCommand(0x21);
  epdData(0x00);
  epdData(0x80);
}

void epdWriteAllBlack() {
  epdCommand(0x26);
  digitalWrite(PIN_EPD_DC, HIGH);
  digitalWrite(PIN_EPD_CS, LOW);
  for (uint16_t i = 0; i < (uint16_t)EPD_WIDTH_BYTES * EPD_HEIGHT; i++) {
    SPI.transfer(0xFF);
  }
  digitalWrite(PIN_EPD_CS, HIGH);

  epdCommand(0x4E);
  epdData(0x00);
  epdCommand(0x4F);
  epdData((EPD_HEIGHT - 1) & 0xFF);
  epdData((EPD_HEIGHT - 1) >> 8);

  epdCommand(0x24);
  digitalWrite(PIN_EPD_DC, HIGH);
  digitalWrite(PIN_EPD_CS, LOW);
  for (uint16_t i = 0; i < (uint16_t)EPD_WIDTH_BYTES * EPD_HEIGHT; i++) {
    SPI.transfer(0x00);
  }
  digitalWrite(PIN_EPD_CS, HIGH);
}

bool refreshAndWatchBusy() {
  byte before = digitalRead(PIN_EPD_BUSY);

  epdCommand(0x22);
  epdData(0xF7);
  epdCommand(0x20);

  bool changed = waitForBusyChange(before, 1000);
  waitForBusyStable(LOW, 12000);

  return changed;
}

void setup() {
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  pinMode(PIN_RF_EN, OUTPUT);
  digitalWrite(PIN_RF_EN, RF_EN_OFF_LEVEL);

  pinMode(PIN_RF_CSN, OUTPUT);
  digitalWrite(PIN_RF_CSN, HIGH);

  pinMode(PIN_EPD_BUSY, INPUT);
  pinMode(PIN_EPD_RST, OUTPUT);
  pinMode(PIN_EPD_DC, OUTPUT);
  pinMode(PIN_EPD_CS, OUTPUT);

  digitalWrite(PIN_EPD_CS, HIGH);
  digitalWrite(PIN_EPD_DC, LOW);
  digitalWrite(PIN_EPD_RST, HIGH);

  blinkLed(1, 60, 80);

  SPI.begin();
  SPI.beginTransaction(EPD_SPI_SETTINGS);

  bool resetMovedBusy = pulseResetAndWatchBusy();
  bool swResetMovedBusy = sendSwResetAndWatchBusy();

  if (resetMovedBusy || swResetMovedBusy) {
    blinkLed(3);
  } else {
    blinkLed(2);
  }

  epdMinimalInit();
  epdWriteAllBlack();
  bool refreshMovedBusy = refreshAndWatchBusy();

  SPI.endTransaction();
  SPI.end();

  if (refreshMovedBusy) {
    blinkLed(5);
  } else {
    blinkLed(2);
  }
}

void loop() {
}
