#include <Adafruit_NeoPixel.h>
#include <Wire.h>

/*
  Test PCB Thermonuino Dial

  Pinout d'apres pintou.txt, ATmega328 5 V / 8 MHz.

  PCINT -> Arduino:
    PCINT0  PB0 D8
    PCINT1  PB1 D9
    PCINT2  PB2 D10
    PCINT3  PB3 D11
    PCINT4  PB4 D12
    PCINT5  PB5 D13
    PCINT8  PC0 A0
    PCINT9  PC1 A1
    PCINT10 PC2 A2
    PCINT11 PC3 A3
    PCINT12 PC4 A4 / SDA
    PCINT13 PC5 A5 / SCL
    PCINT18 PD2 D2
    PCINT23 PD7 D7
*/

// LED chain: LEDSALON, LEDCHAMBRE, LEDBUREAU, LEDSDB, LEDCENTRE, LEDMODE.
constexpr uint8_t PIN_LED_CHAIN_DATA = A3;  // PCINT11
constexpr uint8_t LED_COUNT = 6;
constexpr uint8_t LED_SDB = 3;
constexpr uint8_t LED_CENTRE = 4;
constexpr uint8_t LED_MODE = 5;

// EEPROM 24LC512T-I/SN on I2C.
constexpr uint8_t EEPROM_ADDR_FIRST = 0x50;
constexpr uint8_t EEPROM_ADDR_LAST = 0x57;

// CC1101, using software SPI exactly as listed in pintou.txt.
constexpr uint8_t PIN_CC1101_CSN = 2;   // PCINT18
constexpr uint8_t PIN_CC1101_GDO0 = A2; // PCINT10, not required for this communication test
constexpr uint8_t PIN_CC1101_MOSI = 12; // PCINT4
constexpr uint8_t PIN_CC1101_MISO = 11; // PCINT3
constexpr uint8_t PIN_CC1101_SCK = 13;  // PCINT5

// PCB mode track inputs.
constexpr uint8_t PIN_MODE_DOUCHE = A0; // PCINT8
constexpr uint8_t PIN_MODE_STOP = A1;   // PCINT9
constexpr uint8_t PIN_MODE_PLUS = 10;   // PCINT2
constexpr uint8_t PIN_MODE_NORMAL = 9;  // PCINT1
constexpr uint8_t PIN_MODE_MOINS = 8;   // PCINT0
constexpr uint8_t PIN_MODE_VAC = 7;     // PCINT23

constexpr uint32_t SELF_TEST_INTERVAL_MS = 2000;
constexpr uint32_t MODE_SAMPLE_INTERVAL_MS = 20;
constexpr uint8_t MODE_STABLE_SAMPLES = 4;

Adafruit_NeoPixel leds(LED_COUNT, PIN_LED_CHAIN_DATA, NEO_GRB + NEO_KHZ800);

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

struct ModeInput {
  uint8_t pin;
  ModeValue mode;
};

const ModeInput modeInputs[] = {
  {PIN_MODE_DOUCHE, MODE_DOUCHE},
  {PIN_MODE_STOP, MODE_STOP},
  {PIN_MODE_PLUS, MODE_PLUS},
  {PIN_MODE_NORMAL, MODE_NORMAL},
  {PIN_MODE_MOINS, MODE_MOINS},
  {PIN_MODE_VAC, MODE_VACANCES},
};

uint32_t lastSelfTestAt = 0;
uint32_t lastModeSampleAt = 0;
ModeValue lastRawMode = MODE_NONE;
ModeValue stableMode = MODE_NONE;
uint8_t stableCount = 0;

uint32_t rgb(uint8_t red, uint8_t green, uint8_t blue) {
  return leds.Color(red, green, blue);
}

void setPixel(uint8_t index, uint32_t color) {
  leds.setPixelColor(index, color);
}

bool testExternalEeprom() {
  for (uint8_t addr = EEPROM_ADDR_FIRST; addr <= EEPROM_ADDR_LAST; addr++) {
    Wire.beginTransmission(addr);
    Wire.write(0x00);
    Wire.write(0x00);

    if (Wire.endTransmission(false) != 0) {
      continue;
    }

    if (Wire.requestFrom(addr, (uint8_t)1) == 1 && Wire.available() == 1) {
      Wire.read();
      return true;
    }
  }

  return false;
}

void cc1101Select() {
  digitalWrite(PIN_CC1101_SCK, LOW);
  digitalWrite(PIN_CC1101_CSN, LOW);
}

void cc1101Deselect() {
  digitalWrite(PIN_CC1101_CSN, HIGH);
}

bool cc1101WaitReady(uint16_t timeoutUs) {
  const uint32_t startedAt = micros();
  while (digitalRead(PIN_CC1101_MISO) == HIGH) {
    if ((uint32_t)(micros() - startedAt) >= timeoutUs) {
      return false;
    }
  }

  return true;
}

uint8_t softSpiTransfer(uint8_t value) {
  uint8_t readValue = 0;

  for (int8_t bit = 7; bit >= 0; bit--) {
    digitalWrite(PIN_CC1101_MOSI, (value & (1 << bit)) ? HIGH : LOW);
    delayMicroseconds(2);
    digitalWrite(PIN_CC1101_SCK, HIGH);

    readValue <<= 1;
    if (digitalRead(PIN_CC1101_MISO) == HIGH) {
      readValue |= 1;
    }

    delayMicroseconds(2);
    digitalWrite(PIN_CC1101_SCK, LOW);
  }

  return readValue;
}

uint8_t cc1101ReadStatusRegister(uint8_t reg) {
  constexpr uint8_t READ_BURST = 0xC0;

  cc1101Select();
  if (!cc1101WaitReady(1000)) {
    cc1101Deselect();
    return 0xFF;
  }

  softSpiTransfer(reg | READ_BURST);
  const uint8_t value = softSpiTransfer(0x00);
  cc1101Deselect();

  return value;
}

bool testCc1101() {
  constexpr uint8_t REG_PARTNUM = 0x30;
  constexpr uint8_t REG_VERSION = 0x31;

  const uint8_t partnum = cc1101ReadStatusRegister(REG_PARTNUM);
  const uint8_t version = cc1101ReadStatusRegister(REG_VERSION);

  // CC1101 PARTNUM is normally 0x00. VERSION is commonly 0x04, 0x14, or 0x17
  // depending on silicon/revision/module clones. Reject open bus values.
  if (partnum == 0xFF || version == 0xFF || version == 0x00) {
    return false;
  }

  return partnum == 0x00;
}

ModeValue readRawMode() {
  uint8_t activeCount = 0;
  ModeValue activeMode = MODE_NONE;

  for (const ModeInput &input : modeInputs) {
    if (digitalRead(input.pin) == HIGH) {
      activeCount++;
      activeMode = input.mode;
    }
  }

  if (activeCount == 0) {
    return MODE_NONE;
  }

  if (activeCount > 1) {
    return MODE_INVALID;
  }

  return activeMode;
}

void updateModeState() {
  if ((uint32_t)(millis() - lastModeSampleAt) < MODE_SAMPLE_INTERVAL_MS) {
    return;
  }

  lastModeSampleAt = millis();
  const ModeValue rawMode = readRawMode();

  if (rawMode == lastRawMode) {
    if (stableCount < MODE_STABLE_SAMPLES) {
      stableCount++;
    }
  } else {
    lastRawMode = rawMode;
    stableCount = 1;
  }

  if (stableCount >= MODE_STABLE_SAMPLES) {
    stableMode = rawMode;
  }
}

uint32_t colorForMode(ModeValue mode) {
  switch (mode) {
    case MODE_NORMAL:
      return rgb(255, 110, 0); // orange
    case MODE_MOINS:
      return rgb(0, 80, 255); // bleu
    case MODE_PLUS:
      return rgb(255, 0, 0); // rouge
    case MODE_VACANCES:
      return rgb(140, 0, 255); // violet
    case MODE_STOP:
      return rgb(0, 0, 0); // eteinte
    case MODE_DOUCHE: {
      const uint16_t phase = millis() % 1600;
      const uint8_t level = phase < 800 ? map(phase, 0, 799, 20, 255) : map(phase, 800, 1599, 255, 20);
      return rgb(level, 0, 0); // rouge pulsatile
    }
    case MODE_INVALID:
      return rgb(255, 255, 255); // plusieurs pistes lues: garder blanc pour signaler l'anomalie
    case MODE_NONE:
    default:
      return rgb(255, 255, 255); // aucune valeur lisible
  }
}

void runSelfTests() {
  const bool eepromOk = testExternalEeprom();
  const bool cc1101Ok = testCc1101();

  setPixel(LED_CENTRE, eepromOk ? rgb(0, 255, 0) : rgb(255, 0, 0));
  setPixel(LED_SDB, cc1101Ok ? rgb(0, 255, 0) : rgb(255, 0, 0));
}

void setup() {
  pinMode(PIN_CC1101_CSN, OUTPUT);
  pinMode(PIN_CC1101_MOSI, OUTPUT);
  pinMode(PIN_CC1101_MISO, INPUT);
  pinMode(PIN_CC1101_SCK, OUTPUT);
  pinMode(PIN_CC1101_GDO0, INPUT);

  digitalWrite(PIN_CC1101_CSN, HIGH);
  digitalWrite(PIN_CC1101_SCK, LOW);
  digitalWrite(PIN_CC1101_MOSI, LOW);

  for (const ModeInput &input : modeInputs) {
    pinMode(input.pin, INPUT);
  }

  Wire.begin();
  leds.begin();
  leds.clear();

  setPixel(LED_MODE, rgb(255, 255, 255));
  runSelfTests();
  leds.show();
}

void loop() {
  updateModeState();

  if ((uint32_t)(millis() - lastSelfTestAt) >= SELF_TEST_INTERVAL_MS) {
    lastSelfTestAt = millis();
    runSelfTests();
  }

  setPixel(LED_MODE, colorForMode(stableMode));
  leds.show();
  delay(10);
}
