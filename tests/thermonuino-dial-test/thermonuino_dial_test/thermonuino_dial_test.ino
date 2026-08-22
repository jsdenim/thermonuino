#include <Adafruit_NeoPixel.h>
#include <Wire.h>

/*
  Test PCB Thermonuino Dial

  Pinout d'apres Specifications Thermonuio.md / PCB Console.
  ATmega328 5 V / 8 MHz.

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

  I2C:
    EEPROM 24LC512 + AHT30/AHT20 sur PCINT12 SDA et PCINT13 SCL.
*/

// LED chain: LEDSALON, LEDCHAMBRE, LEDBUREAU, LEDSDB, LEDCENTRE, LEDMODE.
constexpr uint8_t PIN_LED_CHAIN_DATA = A3;  // PCINT11
constexpr uint8_t LED_COUNT = 6;
constexpr uint8_t LED_SALON = 0;
constexpr uint8_t LED_CHAMBRE = 1;
constexpr uint8_t LED_BUREAU = 2;
constexpr uint8_t LED_SDB = 3;
constexpr uint8_t LED_CENTRE = 4;
constexpr uint8_t LED_MODE = 5;
constexpr uint8_t LED_ZONE_2 = LED_CHAMBRE;

// EEPROM 24LC512T-I/SN and AHT30/AHT20 on I2C.
constexpr uint8_t EEPROM_ADDR_FIRST = 0x50;
constexpr uint8_t EEPROM_ADDR_LAST = 0x57;
constexpr uint8_t AHT_ADDR = 0x38;

// CC1101, using software SPI exactly as listed in the general specifications.
constexpr uint8_t PIN_CC1101_CSN = 2;   // PCINT18
constexpr uint8_t PIN_CC1101_GDO0 = A2; // PCINT10, not required for this communication test
constexpr uint8_t PIN_CC1101_MOSI = 11; // PCINT4
constexpr uint8_t PIN_CC1101_MISO = 12; // PCINT3
constexpr uint8_t PIN_CC1101_SCK = 13;  // PCINT5
constexpr uint8_t CC1101_READ_BURST = 0xC0;
constexpr uint8_t CC1101_WRITE_BURST = 0x40;
constexpr uint8_t CC1101_TXFIFO = 0x3F;
constexpr uint8_t CC1101_RXFIFO = 0x3F;
constexpr uint8_t CC1101_RXBYTES = 0x3B;
constexpr uint8_t CC1101_SRES = 0x30;
constexpr uint8_t CC1101_SRX = 0x34;
constexpr uint8_t CC1101_STX = 0x35;
constexpr uint8_t CC1101_SIDLE = 0x36;
constexpr uint8_t CC1101_SFRX = 0x3A;
constexpr uint8_t CC1101_SFTX = 0x3B;
constexpr uint8_t RF_PROTOCOL_VERSION = 1;
constexpr uint8_t RF_HEADER_LEN = 12;
constexpr uint8_t RF_MAX_PACKET_LEN = 64;
constexpr uint8_t RF_REPORT_PAYLOAD_LEN = 34;
constexpr uint8_t RF_RESPONSE_PAYLOAD_LEN = 19;
constexpr uint16_t RF_NODE_ID = 0x0C01;
constexpr uint16_t RF_DOOR_NODE_ID = 0x0D01;
constexpr uint8_t RF_FRAME_REPORT = 1;
constexpr uint8_t RF_FRAME_RESPONSE = 2;
constexpr uint8_t RF_DEVICE_TYPE_DOOR = 2;
constexpr uint8_t REPORT_DEVICE_TYPE = 0;
constexpr uint8_t REPORT_BATTERY_MV = 1;
constexpr uint8_t REPORT_ADMIN_REQUEST = 4;
constexpr uint8_t REPORT_USER_DELTA_STEPS = 5;
constexpr uint8_t REPORT_TEMP_COUNT = 6;
constexpr uint8_t REPORT_DOOR_TOGGLE_COUNT = 32;
constexpr uint8_t REPORT_DOOR_OPEN = 33;
constexpr uint8_t RESPONSE_ASSIGNED_ZONE = 0;
constexpr uint8_t RESPONSE_DATE_TIME = 1;
constexpr uint8_t RESPONSE_GLOBAL_MODE = 7;
constexpr uint8_t RESPONSE_HEAT_ACTIVE = 8;
constexpr uint8_t RESPONSE_ZONE_DOOR_OPEN = 9;
constexpr uint8_t RESPONSE_OUTSIDE_TEMP = 10;
constexpr uint8_t RESPONSE_USUAL_SETPOINT = 12;
constexpr uint8_t RESPONSE_CURRENT_SETPOINT = 14;
constexpr uint8_t RESPONSE_COMMAND_FLAGS = 16;
constexpr uint8_t RESPONSE_NEXT_REPORT_DELAY_S = 17;
constexpr uint32_t RF_ACK_REPLY_DELAY_MS = 300;
constexpr uint8_t RF_ACK_TX_COUNT = 3;
constexpr uint16_t RF_ACK_TX_GAP_MS = 150;
constexpr uint16_t RF_TX_COMPLETE_DELAY_MS = 350;
constexpr uint32_t RF_RX_REFRESH_INTERVAL_MS = 500;

// PCB mode track inputs: external 4.7k pull-up to 5 V, switch/contact to GND.
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
uint32_t lastTempSampleAt = 0;
uint32_t lastModeSampleAt = 0;
uint32_t lastRfRxRefreshAt = 0;
ModeValue lastRawMode = MODE_NONE;
ModeValue stableMode = MODE_NONE;
uint8_t stableCount = 0;
bool ahtOk = false;
bool cc1101Ok = false;
float lastTemperatureC = 0.0f;
uint8_t rfSequence = 0;
bool rfBlinkActive = false;
uint8_t rfBlinkStep = 0;
uint32_t nextRfBlinkAt = 0;
uint32_t rfDiagUntil = 0;
uint32_t rfSyncSeenUntil = 0;
uint32_t rfInvalidUntil = 0;
uint32_t rfOverflowUntil = 0;
bool hasLastDoorSequence = false;
uint8_t lastDoorSequence = 0;
bool lastRfGdo0State = false;
uint16_t lastReportBatteryMv = 0;
uint8_t lastReportDoorToggleCount = 0;
bool lastReportDoorOpen = false;
uint8_t lastReportAdminRequest = 0;

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

bool ahtReadStatus(uint8_t &status) {
  Wire.requestFrom(AHT_ADDR, (uint8_t)1);
  if (Wire.available() != 1) {
    return false;
  }

  status = Wire.read();
  return true;
}

bool initAht() {
  Wire.beginTransmission(AHT_ADDR);
  Wire.write(0xBE);
  Wire.write(0x08);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) {
    return false;
  }

  delay(10);
  uint8_t status = 0;
  return ahtReadStatus(status);
}

bool readAhtTemperature(float &temperatureC) {
  uint8_t status = 0;
  if (!ahtReadStatus(status)) {
    return false;
  }

  if ((status & 0x08) == 0 && !initAht()) {
    return false;
  }

  Wire.beginTransmission(AHT_ADDR);
  Wire.write(0xAC);
  Wire.write(0x33);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) {
    return false;
  }

  delay(80);
  uint8_t data[6] = {0};
  Wire.requestFrom(AHT_ADDR, (uint8_t)6);
  for (uint8_t i = 0; i < 6; i++) {
    if (!Wire.available()) {
      return false;
    }
    data[i] = Wire.read();
  }

  if ((data[0] & 0x80) != 0) {
    return false;
  }

  const uint32_t rawTemp =
      (((uint32_t)data[3] & 0x0F) << 16) |
      ((uint32_t)data[4] << 8) |
      data[5];
  temperatureC = ((float)rawTemp * 200.0f / 1048576.0f) - 50.0f;
  return true;
}

uint32_t colorForTemperature(float temperatureC) {
  if (temperatureC < 22.0f) {
    return rgb(0, 0, 255); // bleu
  }
  if (temperatureC < 26.0f) {
    return rgb(255, 180, 0); // jaune
  }
  if (temperatureC < 28.0f) {
    return rgb(255, 80, 0); // orange
  }
  return rgb(255, 0, 0); // rouge
}

void updateTemperatureTest() {
  if ((uint32_t)(millis() - lastTempSampleAt) < SELF_TEST_INTERVAL_MS) {
    return;
  }

  lastTempSampleAt = millis();
  ahtOk = readAhtTemperature(lastTemperatureC);
  setPixel(LED_ZONE_2, ahtOk ? colorForTemperature(lastTemperatureC) : rgb(255, 0, 255));
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

uint8_t cc1101Strobe(uint8_t strobe) {
  cc1101Select();
  if (!cc1101WaitReady(1000)) {
    cc1101Deselect();
    return 0xFF;
  }
  const uint8_t status = softSpiTransfer(strobe);
  cc1101Deselect();
  return status;
}

void cc1101ResetRadio() {
  cc1101Deselect();
  delayMicroseconds(5);
  cc1101Select();
  cc1101WaitReady(1000);
  softSpiTransfer(CC1101_SRES);
  cc1101Deselect();
  delay(2);
}

void cc1101WriteRegister(uint8_t address, uint8_t value) {
  cc1101Select();
  if (!cc1101WaitReady(1000)) {
    cc1101Deselect();
    return;
  }
  softSpiTransfer(address);
  softSpiTransfer(value);
  cc1101Deselect();
}

void cc1101WritePatable(uint8_t value) {
  cc1101Select();
  if (!cc1101WaitReady(1000)) {
    cc1101Deselect();
    return;
  }
  softSpiTransfer(CC1101_WRITE_BURST | 0x3E);
  softSpiTransfer(value);
  cc1101Deselect();
}

void cc1101FlushRx() {
  cc1101Strobe(CC1101_SIDLE);
  cc1101Strobe(CC1101_SFRX);
}

void cc1101ConfigureTestRadio() {
  cc1101ResetRadio();
  cc1101Strobe(CC1101_SIDLE);
  cc1101Strobe(CC1101_SFRX);
  cc1101Strobe(CC1101_SFTX);

  cc1101WriteRegister(0x02, 0x06);
  cc1101WriteRegister(0x07, RF_MAX_PACKET_LEN);
  cc1101WriteRegister(0x08, 0x05);
  cc1101WriteRegister(0x0B, 0x06);
  cc1101WriteRegister(0x0D, 0x10);
  cc1101WriteRegister(0x0E, 0xB0);
  cc1101WriteRegister(0x0F, 0x71);
  cc1101WriteRegister(0x10, 0xF5);
  cc1101WriteRegister(0x11, 0x83);
  cc1101WriteRegister(0x12, 0x13);
  cc1101WriteRegister(0x15, 0x15);
  cc1101WriteRegister(0x18, 0x18);
  cc1101WriteRegister(0x19, 0x16);
  cc1101WriteRegister(0x23, 0xE9);
  cc1101WriteRegister(0x24, 0x2A);
  cc1101WriteRegister(0x25, 0x00);
  cc1101WriteRegister(0x26, 0x1F);
  cc1101WritePatable(0xC0);
}

void startRfReceivedBlink() {
  rfBlinkActive = true;
  rfBlinkStep = 0;
  nextRfBlinkAt = 0;
}

void markRfSyncSeen() {
  rfSyncSeenUntil = millis() + 120;
  rfDiagUntil = millis() + 120;
}

void markRfInvalidPacket() {
  rfInvalidUntil = millis() + 300;
  rfDiagUntil = millis() + 300;
}

void markRfOverflow() {
  rfOverflowUntil = millis() + 600;
  rfDiagUntil = millis() + 600;
}

void updateRfDiagnosticLed() {
  if ((int32_t)(millis() - rfDiagUntil) >= 0) {
    return;
  }

  if ((int32_t)(millis() - rfOverflowUntil) < 0) {
    setPixel(LED_BUREAU, rgb(255, 0, 0));
    return;
  }

  if ((int32_t)(millis() - rfInvalidUntil) < 0) {
    setPixel(LED_BUREAU, rgb(255, 180, 0));
    return;
  }

  if ((int32_t)(millis() - rfSyncSeenUntil) < 0) {
    setPixel(LED_BUREAU, rgb(0, 0, 255));
  }
}

void updateRfReceivedBlink() {
  if (!rfBlinkActive || (int32_t)(millis() - nextRfBlinkAt) < 0) {
    return;
  }

  if (rfBlinkStep >= 6) {
    rfBlinkActive = false;
    setPixel(LED_BUREAU, rgb(0, 0, 0));
    return;
  }

  const bool ledOn = (rfBlinkStep % 2) == 0;
  const uint16_t durationMs = 120;
  setPixel(LED_BUREAU, ledOn ? rgb(0, 80, 80) : rgb(0, 0, 0));
  rfBlinkStep++;
  nextRfBlinkAt = millis() + durationMs;
}

void writeU16(uint8_t *buffer, uint8_t offset, uint16_t value) {
  buffer[offset] = value & 0xFF;
  buffer[offset + 1] = value >> 8;
}

uint16_t readU16(const uint8_t *buffer, uint8_t offset) {
  return (uint16_t)buffer[offset] | ((uint16_t)buffer[offset + 1] << 8);
}

uint8_t expectedPayloadLen(uint8_t frameType) {
  return frameType == RF_FRAME_RESPONSE ? RF_RESPONSE_PAYLOAD_LEN : RF_REPORT_PAYLOAD_LEN;
}

uint8_t buildRfPacket(uint8_t *packet, uint8_t frameType, uint8_t sequence, uint8_t ackSequence) {
  const uint8_t payloadLen = frameType == RF_FRAME_RESPONSE ? RF_RESPONSE_PAYLOAD_LEN : 0;

  packet[0] = 'T';
  packet[1] = 'N';
  packet[2] = 'U';
  packet[3] = RF_PROTOCOL_VERSION;
  packet[4] = frameType;
  writeU16(packet, 5, RF_NODE_ID);
  writeU16(packet, 7, RF_DOOR_NODE_ID);
  packet[9] = sequence;
  packet[10] = ackSequence;
  packet[11] = payloadLen;

  if (frameType == RF_FRAME_RESPONSE) {
    uint8_t *payload = packet + RF_HEADER_LEN;
    payload[RESPONSE_ASSIGNED_ZONE] = 1;
    payload[RESPONSE_DATE_TIME] = 26; // year since 2000
    payload[RESPONSE_DATE_TIME + 1] = 8;
    payload[RESPONSE_DATE_TIME + 2] = 22;
    payload[RESPONSE_DATE_TIME + 3] = 12;
    payload[RESPONSE_DATE_TIME + 4] = 0;
    payload[RESPONSE_DATE_TIME + 5] = 0;
    payload[RESPONSE_GLOBAL_MODE] = 0; // normal
    payload[RESPONSE_HEAT_ACTIVE] = 0;
    payload[RESPONSE_ZONE_DOOR_OPEN] = lastReportDoorOpen ? 1 : 0;
    writeU16(payload, RESPONSE_OUTSIDE_TEMP, 120); // 12.0 C factice
    writeU16(payload, RESPONSE_USUAL_SETPOINT, 190); // 19.0 C
    writeU16(payload, RESPONSE_CURRENT_SETPOINT, 190); // 19.0 C
    payload[RESPONSE_COMMAND_FLAGS] = 0;
    writeU16(payload, RESPONSE_NEXT_REPORT_DELAY_S, 3600);
  }

  return RF_HEADER_LEN + payloadLen;
}

bool decodeReportPayload(const uint8_t *payload) {
  const uint8_t *report = payload + RF_HEADER_LEN;
  const int8_t userDeltaSteps = (int8_t)report[REPORT_USER_DELTA_STEPS];
  if (report[REPORT_DEVICE_TYPE] != RF_DEVICE_TYPE_DOOR ||
      report[REPORT_TEMP_COUNT] > 12 ||
      report[REPORT_DOOR_OPEN] > 1 ||
      userDeltaSteps < -8 ||
      userDeltaSteps > 8) {
    return false;
  }

  lastReportBatteryMv = readU16(report, REPORT_BATTERY_MV);
  lastReportAdminRequest = report[REPORT_ADMIN_REQUEST];
  lastReportDoorToggleCount = report[REPORT_DOOR_TOGGLE_COUNT];
  lastReportDoorOpen = report[REPORT_DOOR_OPEN] != 0;
  return true;
}

void waitRfTxComplete() {
  const uint32_t startedAt = millis();
  while (digitalRead(PIN_CC1101_GDO0) == LOW) {
    if ((uint32_t)(millis() - startedAt) >= RF_TX_COMPLETE_DELAY_MS) {
      return;
    }
  }

  while (digitalRead(PIN_CC1101_GDO0) == HIGH) {
    if ((uint32_t)(millis() - startedAt) >= RF_TX_COMPLETE_DELAY_MS) {
      return;
    }
  }
}

bool readThermonuinoPacket(uint16_t expectedSource, uint8_t expectedFrameType, uint8_t expectedAckSequence, uint8_t &sequence) {
  const uint8_t rxBytesRaw = cc1101ReadStatusRegister(CC1101_RXBYTES);
  if ((rxBytesRaw & 0x80) != 0) {
    markRfOverflow();
    cc1101FlushRx();
    cc1101Strobe(CC1101_SRX);
    return false;
  }

  const uint8_t rxBytes = rxBytesRaw & 0x7F;
  const uint8_t expectedLength = RF_HEADER_LEN + expectedPayloadLen(expectedFrameType);
  if (rxBytes < expectedLength + 1) {
    return false;
  }

  uint8_t payload[RF_MAX_PACKET_LEN] = {0};
  uint8_t length = 0;
  cc1101Select();
  if (!cc1101WaitReady(1000)) {
    cc1101Deselect();
    return false;
  }
  softSpiTransfer(CC1101_READ_BURST | CC1101_RXFIFO);
  length = softSpiTransfer(0x00);
  if (length > rxBytes - 1) {
    length = rxBytes - 1;
  }
  if (length > sizeof(payload)) {
    length = sizeof(payload);
  }
  for (uint8_t i = 0; i < length; i++) {
    payload[i] = softSpiTransfer(0x00);
  }
  cc1101Deselect();
  cc1101FlushRx();
  cc1101Strobe(CC1101_SRX);

  const bool ok = length >= RF_HEADER_LEN &&
      payload[0] == 'T' &&
      payload[1] == 'N' &&
      payload[2] == 'U' &&
      payload[3] == RF_PROTOCOL_VERSION &&
      payload[4] == expectedFrameType &&
      readU16(payload, 5) == expectedSource &&
      readU16(payload, 5) != RF_NODE_ID &&
      readU16(payload, 7) == RF_NODE_ID &&
      length == expectedLength &&
      payload[11] == length - RF_HEADER_LEN &&
      (expectedAckSequence == 0xFF || payload[10] == expectedAckSequence);
  if (ok) {
    if (expectedFrameType == RF_FRAME_REPORT && !decodeReportPayload(payload)) {
      markRfInvalidPacket();
      return false;
    }
    sequence = payload[9];
  } else {
    markRfInvalidPacket();
  }
  return ok;
}

void sendThermonuinoPacket(uint8_t frameType, uint8_t sequence, uint8_t ackSequence) {
  uint8_t payload[RF_MAX_PACKET_LEN] = {0};
  const uint8_t length = buildRfPacket(payload, frameType, sequence, ackSequence);
  cc1101Strobe(CC1101_SIDLE);
  cc1101Strobe(CC1101_SFTX);

  cc1101Select();
  if (!cc1101WaitReady(1000)) {
    cc1101Deselect();
    return;
  }
  softSpiTransfer(CC1101_WRITE_BURST | CC1101_TXFIFO);
  softSpiTransfer(length);
  for (uint8_t i = 0; i < length; i++) {
    softSpiTransfer(payload[i]);
  }
  cc1101Deselect();
  cc1101Strobe(CC1101_STX);
  waitRfTxComplete();
}

void sendAckBurst(uint8_t beaconSequence) {
  delay(RF_ACK_REPLY_DELAY_MS);
  for (uint8_t ack = 0; ack < RF_ACK_TX_COUNT; ack++) {
    setPixel(LED_SDB, rgb(255, 110, 0));
    leds.show();
    sendThermonuinoPacket(RF_FRAME_RESPONSE, rfSequence++, beaconSequence);
    if (ack + 1 < RF_ACK_TX_COUNT) {
      delay(RF_ACK_TX_GAP_MS);
    }
  }
  setPixel(LED_SDB, cc1101Ok ? rgb(0, 255, 0) : rgb(255, 0, 0));
  leds.show();
}

void updateRfRangeTest() {
  const bool rfGdo0State = digitalRead(PIN_CC1101_GDO0) == HIGH;
  if (rfGdo0State && !lastRfGdo0State) {
    markRfSyncSeen();
  }
  lastRfGdo0State = rfGdo0State;

  if ((uint32_t)(millis() - lastRfRxRefreshAt) >= RF_RX_REFRESH_INTERVAL_MS) {
    lastRfRxRefreshAt = millis();
    cc1101Strobe(CC1101_SRX);
  }

  uint8_t beaconSequence = 0;
  if (!readThermonuinoPacket(RF_DOOR_NODE_ID, RF_FRAME_REPORT, 0xFF, beaconSequence)) {
    return;
  }

  const bool duplicate = hasLastDoorSequence && beaconSequence == lastDoorSequence;

  if (!duplicate) {
    hasLastDoorSequence = true;
    lastDoorSequence = beaconSequence;
  }

  sendAckBurst(beaconSequence);
  if (!duplicate) {
    startRfReceivedBlink();
  }
  cc1101Strobe(CC1101_SRX);
}

ModeValue readRawMode() {
  uint8_t activeCount = 0;
  ModeValue activeMode = MODE_NONE;

  for (const ModeInput &input : modeInputs) {
    if (digitalRead(input.pin) == LOW) {
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

  setPixel(LED_CENTRE, eepromOk ? rgb(0, 255, 0) : rgb(255, 0, 0));
  setPixel(LED_SDB, cc1101Ok ? rgb(0, 255, 0) : rgb(255, 0, 0));
  updateTemperatureTest();
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
  leds.setBrightness(3);
  leds.begin();
  leds.clear();

  setPixel(LED_MODE, rgb(255, 255, 255));
  initAht();
  lastTempSampleAt = millis() - SELF_TEST_INTERVAL_MS;
  cc1101Ok = testCc1101();
  runSelfTests();
  cc1101ConfigureTestRadio();
  cc1101Strobe(CC1101_SRX);
  lastRfRxRefreshAt = millis();
  leds.show();
}

void loop() {
  updateRfRangeTest();

  updateModeState();

  if ((uint32_t)(millis() - lastSelfTestAt) >= SELF_TEST_INTERVAL_MS) {
    lastSelfTestAt = millis();
    runSelfTests();
  }

  setPixel(LED_MODE, colorForMode(stableMode));
  updateRfDiagnosticLed();
  updateRfReceivedBlink();
  leds.show();
  delay(10);
}
