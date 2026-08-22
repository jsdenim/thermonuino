/*
  Test PCB Thermonuino Porte Ouverte

  Cible: ATmega328P 3.3 V / 8 MHz.
  Pinout d'apres Specifications Thermonuio.md / PCB Porte Ouverte.

  Fonctions testees:
    - LED PCB sur PCINT11 / A3, active HIGH ;
    - DOOR_OPEN sur PCINT18 / D2, REED en parallele vers GND, actif LOW ;
    - bouton sur PCINT19 / D3, arrive a 3.3 V a l'appui, actif HIGH ;
    - CC1101 sur SPI, alimentation 3.3 V permanente.

  Codes LED:
    - demarrage: 1 blink court ;
    - test CC1101 OK au demarrage: 5 blinks courts ;
    - test CC1101 KO/timeout au demarrage: 2 blinks longs ;
    - REED ferme: LED fixe ;
    - bouton appuye: clignotement rapide tant que le bouton reste actif.
    - apres emission RF: LED allumee 3 s si ACK recu ;
    - apres emission RF sans ACK: LED allumee 3 s avec OFF de 250 ms.

  Le test SPI a des timeouts pour ne pas rester bloque si le CC1101 ne repond
  pas ou si MISO reste haut.

  Test RF portee:
    - tentative toutes les 3 s ;
    - ecoute canal avant emission ;
    - emission d'une balise puis attente courte d'un ACK ;
    - retry avec backoff aleatoire si l'ACK n'arrive pas.
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

const uint8_t CC1101_READ_BURST = 0xC0;
const uint8_t CC1101_WRITE_BURST = 0x40;
const uint8_t CC1101_PARTNUM = 0x30;
const uint8_t CC1101_VERSION = 0x31;
const uint8_t CC1101_TXFIFO = 0x3F;
const uint8_t CC1101_RXFIFO = 0x3F;
const uint8_t CC1101_PKTSTATUS = 0x38;
const uint8_t CC1101_RXBYTES = 0x3B;
const uint8_t CC1101_SRES = 0x30;
const uint8_t CC1101_SRX = 0x34;
const uint8_t CC1101_STX = 0x35;
const uint8_t CC1101_SIDLE = 0x36;
const uint8_t CC1101_SFRX = 0x3A;
const uint8_t CC1101_SFTX = 0x3B;

const unsigned int RF_POWER_UP_MS = 20;
const unsigned int CC1101_READY_TIMEOUT_MS = 15;
const unsigned int CC1101_TOTAL_TIMEOUT_MS = 120;
const unsigned int BUTTON_BLINK_MS = 90;
const unsigned long RF_BEACON_INTERVAL_MS = 3000;
const unsigned long RF_CHANNEL_LISTEN_MS = 30;
const unsigned long RF_ACK_TIMEOUT_MS = 2000;
const unsigned int RF_RX_SETTLE_MS = 50;
const uint8_t RF_MAX_ATTEMPTS = 6;
const unsigned int RF_BACKOFF_MIN_MS = 100;
const unsigned int RF_BACKOFF_SPAN_MS = 500;
const unsigned int RF_BACKOFF_STEP_MS = 150;
const unsigned int RF_TX_COMPLETE_DELAY_MS = 350;
const uint8_t RF_TX_COPIES_PER_ATTEMPT = 3;
const unsigned int RF_TX_COPY_GAP_MS = 60;
const unsigned long RF_RESULT_LED_MS = 1000;
const unsigned int RF_RESULT_FAIL_ON_MS = 250;
const uint8_t RF_PROTOCOL_VERSION = 1;
const uint8_t RF_HEADER_LEN = 12;
const uint8_t RF_MAX_PACKET_LEN = 64;
const uint8_t RF_REPORT_PAYLOAD_LEN = 34;
const uint8_t RF_RESPONSE_PAYLOAD_LEN = 19;
const uint16_t RF_NODE_ID = 0x0D01;
const uint16_t RF_DIAL_NODE_ID = 0x0C01;
const uint8_t RF_FRAME_REPORT = 1;
const uint8_t RF_FRAME_RESPONSE = 2;
const uint8_t RF_DEVICE_TYPE_DOOR = 2;
const uint8_t RF_ADMIN_REQUEST_NONE = 0;
const uint8_t RF_ADMIN_REQUEST_PAIR = 1;
const uint8_t REPORT_DEVICE_TYPE = 0;
const uint8_t REPORT_BATTERY_MV = 1;
const uint8_t REPORT_STATUS_FLAGS = 3;
const uint8_t REPORT_ADMIN_REQUEST = 4;
const uint8_t REPORT_USER_DELTA_STEPS = 5;
const uint8_t REPORT_TEMP_COUNT = 6;
const uint8_t REPORT_TEMPERATURES = 7;
const uint8_t REPORT_PRESENCE_COUNT = 31;
const uint8_t REPORT_DOOR_TOGGLE_COUNT = 32;
const uint8_t REPORT_DOOR_OPEN = 33;
const uint8_t RESPONSE_ASSIGNED_ZONE = 0;
const uint8_t RESPONSE_ZONE_DOOR_OPEN = 9;
const uint8_t RESPONSE_COMMAND_FLAGS = 16;
const uint8_t RESPONSE_NEXT_REPORT_DELAY_S = 17;

const SPISettings RF_SPI_SETTINGS(1000000, MSBFIRST, SPI_MODE0);

unsigned long lastButtonBlinkAt = 0;
bool buttonBlinkOn = false;
unsigned long lastRfBeaconAt = 0;
uint8_t rfSequence = 0;
bool rfResultActive = false;
bool rfResultAckReceived = false;
bool rfResultLedOn = false;
unsigned long rfResultUntil = 0;
unsigned long nextRfResultToggleAt = 0;
bool doorOpenState = false;
bool lastDoorOpenState = false;
uint8_t doorToggleCountSinceAck = 0;
uint8_t lastAssignedZone = 0;
bool lastZoneDoorOpen = false;
uint8_t lastCommandFlags = 0;
uint16_t lastNextReportDelayS = 0;

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

bool readDoorOpenState() {
  return digitalRead(PIN_DOOR_OPEN) != LOW;
}

void updateDoorState() {
  doorOpenState = readDoorOpenState();
  if (doorOpenState != lastDoorOpenState) {
    lastDoorOpenState = doorOpenState;
    if (doorToggleCountSinceAck < 255) {
      doorToggleCountSinceAck++;
    }
  }
}

uint16_t readBatteryMv() {
  return 3000;
}

uint8_t readAdminRequest() {
  return digitalRead(PIN_BUTTON) == HIGH ? RF_ADMIN_REQUEST_PAIR : RF_ADMIN_REQUEST_NONE;
}

void startRfResultIndicator(bool ackReceived) {
  rfResultActive = true;
  rfResultAckReceived = ackReceived;
  rfResultLedOn = ackReceived;
  rfResultUntil = millis() + RF_RESULT_LED_MS;
  nextRfResultToggleAt = millis();
  digitalWrite(PIN_LED, ackReceived ? HIGH : LOW);
}

bool updateRfResultIndicator() {
  if (!rfResultActive) {
    return false;
  }

  if ((int32_t)(millis() - rfResultUntil) >= 0) {
    rfResultActive = false;
    ledOff();
    return false;
  }

  if (rfResultAckReceived) {
    ledOn();
    return true;
  }

  if (!rfResultLedOn && (int32_t)(millis() - nextRfResultToggleAt) >= 0) {
    rfResultLedOn = true;
    ledOn();
    nextRfResultToggleAt = millis() + RF_RESULT_FAIL_ON_MS;
  } else if (rfResultLedOn && (int32_t)(millis() - nextRfResultToggleAt) >= 0) {
    rfResultLedOn = false;
    ledOff();
  }
  return true;
}

void rfPowerOn() {
  digitalWrite(PIN_RF_CSN, HIGH);
  delay(RF_POWER_UP_MS);
}

void rfPowerOff() {
  digitalWrite(PIN_RF_CSN, HIGH);
  digitalWrite(PIN_RF_MOSI, LOW);
  digitalWrite(PIN_RF_SCK, LOW);
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

uint8_t cc1101TransferStrobe(uint8_t strobe) {
  digitalWrite(PIN_RF_CSN, LOW);
  waitCc1101Ready(CC1101_READY_TIMEOUT_MS);
  const uint8_t status = SPI.transfer(strobe);
  digitalWrite(PIN_RF_CSN, HIGH);
  return status;
}

void cc1101ResetRadio() {
  digitalWrite(PIN_RF_CSN, HIGH);
  delayMicroseconds(5);
  digitalWrite(PIN_RF_CSN, LOW);
  waitCc1101Ready(CC1101_READY_TIMEOUT_MS);
  SPI.transfer(CC1101_SRES);
  digitalWrite(PIN_RF_CSN, HIGH);
  delay(2);
}

void cc1101WriteRegister(uint8_t address, uint8_t value) {
  digitalWrite(PIN_RF_CSN, LOW);
  waitCc1101Ready(CC1101_READY_TIMEOUT_MS);
  SPI.transfer(address);
  SPI.transfer(value);
  digitalWrite(PIN_RF_CSN, HIGH);
}

uint8_t cc1101ReadRegisterValue(uint8_t address) {
  uint8_t value = 0xFF;
  cc1101ReadStatusRegister(address, value);
  return value;
}

void cc1101FlushRx() {
  cc1101TransferStrobe(CC1101_SIDLE);
  cc1101TransferStrobe(CC1101_SFRX);
}

void cc1101ConfigureTestRadio() {
  cc1101ResetRadio();
  cc1101TransferStrobe(CC1101_SIDLE);
  cc1101TransferStrobe(CC1101_SFRX);
  cc1101TransferStrobe(CC1101_SFTX);

  cc1101WriteRegister(0x02, 0x06); // IOCFG0: sync word received/sent on GDO0
  cc1101WriteRegister(0x07, RF_MAX_PACKET_LEN); // PKTLEN
  cc1101WriteRegister(0x08, 0x05); // PKTCTRL0: variable length + CRC
  cc1101WriteRegister(0x0B, 0x06); // FSCTRL1
  cc1101WriteRegister(0x0D, 0x10); // FREQ2 433.92 MHz
  cc1101WriteRegister(0x0E, 0xB0); // FREQ1
  cc1101WriteRegister(0x0F, 0x71); // FREQ0
  cc1101WriteRegister(0x10, 0xF5); // MDMCFG4
  cc1101WriteRegister(0x11, 0x83); // MDMCFG3
  cc1101WriteRegister(0x12, 0x13); // MDMCFG2: 2-FSK, sync 30/32
  cc1101WriteRegister(0x15, 0x15); // DEVIATN
  cc1101WriteRegister(0x18, 0x18); // MCSM0
  cc1101WriteRegister(0x19, 0x16); // FOCCFG
  cc1101WriteRegister(0x23, 0xE9); // FSCAL3
  cc1101WriteRegister(0x24, 0x2A); // FSCAL2
  cc1101WriteRegister(0x25, 0x00); // FSCAL1
  cc1101WriteRegister(0x26, 0x1F); // FSCAL0

  digitalWrite(PIN_RF_CSN, LOW);
  waitCc1101Ready(CC1101_READY_TIMEOUT_MS);
  SPI.transfer(CC1101_WRITE_BURST | 0x3E); // PATABLE
  SPI.transfer(0xC0);
  digitalWrite(PIN_RF_CSN, HIGH);
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
  const uint8_t payloadLen = frameType == RF_FRAME_REPORT ? RF_REPORT_PAYLOAD_LEN : 0;

  packet[0] = 'T';
  packet[1] = 'N';
  packet[2] = 'U';
  packet[3] = RF_PROTOCOL_VERSION;
  packet[4] = frameType;
  writeU16(packet, 5, RF_NODE_ID);
  writeU16(packet, 7, RF_DIAL_NODE_ID);
  packet[9] = sequence;
  packet[10] = ackSequence;
  packet[11] = payloadLen;

  if (frameType == RF_FRAME_REPORT) {
    updateDoorState();
    uint8_t *payload = packet + RF_HEADER_LEN;
    const uint16_t batteryMv = readBatteryMv();
    payload[REPORT_DEVICE_TYPE] = RF_DEVICE_TYPE_DOOR;
    payload[REPORT_BATTERY_MV] = batteryMv & 0xFF;
    payload[REPORT_BATTERY_MV + 1] = batteryMv >> 8;
    payload[REPORT_STATUS_FLAGS] = 0;
    payload[REPORT_ADMIN_REQUEST] = readAdminRequest();
    payload[REPORT_USER_DELTA_STEPS] = 0;
    payload[REPORT_TEMP_COUNT] = 12; // payload factice proche d'une sonde
    for (uint8_t i = 0; i < 12; i++) {
      const int16_t tempDeciC = 190 + i;
      payload[REPORT_TEMPERATURES + i * 2] = tempDeciC & 0xFF;
      payload[REPORT_TEMPERATURES + i * 2 + 1] = tempDeciC >> 8;
    }
    payload[REPORT_PRESENCE_COUNT] = 0;
    payload[REPORT_DOOR_TOGGLE_COUNT] = doorToggleCountSinceAck;
    payload[REPORT_DOOR_OPEN] = doorOpenState ? 1 : 0;
  }

  return RF_HEADER_LEN + payloadLen;
}

void waitRfTxComplete() {
  const unsigned long startedAt = millis();
  while (digitalRead(PIN_RF_GDO0) == LOW) {
    if ((uint32_t)(millis() - startedAt) >= RF_TX_COMPLETE_DELAY_MS) {
      return;
    }
  }

  while (digitalRead(PIN_RF_GDO0) == HIGH) {
    if ((uint32_t)(millis() - startedAt) >= RF_TX_COMPLETE_DELAY_MS) {
      return;
    }
  }
}

bool decodeResponsePayload(const uint8_t *packet) {
  const uint8_t *payload = packet + RF_HEADER_LEN;
  const uint8_t assignedZone = payload[RESPONSE_ASSIGNED_ZONE];
  const uint16_t nextReportDelayS = readU16(payload, RESPONSE_NEXT_REPORT_DELAY_S);

  if (assignedZone > 4 || nextReportDelayS == 0) {
    return false;
  }

  lastAssignedZone = assignedZone;
  lastZoneDoorOpen = payload[RESPONSE_ZONE_DOOR_OPEN] != 0;
  lastCommandFlags = payload[RESPONSE_COMMAND_FLAGS];
  lastNextReportDelayS = nextReportDelayS;
  return true;
}

bool readThermonuinoPacket(uint16_t expectedSource, uint8_t expectedFrameType, uint8_t expectedAckSequence, uint8_t &sequence) {
  const uint8_t rxBytesRaw = cc1101ReadRegisterValue(CC1101_RXBYTES);
  if ((rxBytesRaw & 0x80) != 0) {
    cc1101FlushRx();
    cc1101TransferStrobe(CC1101_SRX);
    return false;
  }

  const uint8_t rxBytes = rxBytesRaw & 0x7F;
  const uint8_t expectedLength = RF_HEADER_LEN + expectedPayloadLen(expectedFrameType);
  if (rxBytes < expectedLength + 1) {
    return false;
  }

  uint8_t payload[RF_MAX_PACKET_LEN] = {0};
  uint8_t length = 0;
  digitalWrite(PIN_RF_CSN, LOW);
  waitCc1101Ready(CC1101_READY_TIMEOUT_MS);
  SPI.transfer(CC1101_READ_BURST | CC1101_RXFIFO);
  length = SPI.transfer(0x00);
  if (length > rxBytes - 1) {
    length = rxBytes - 1;
  }
  if (length > sizeof(payload)) {
    length = sizeof(payload);
  }
  for (uint8_t i = 0; i < length; i++) {
    payload[i] = SPI.transfer(0x00);
  }
  digitalWrite(PIN_RF_CSN, HIGH);
  cc1101FlushRx();
  cc1101TransferStrobe(CC1101_SRX);

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
  if (ok && (expectedFrameType != RF_FRAME_RESPONSE || decodeResponsePayload(payload))) {
    sequence = payload[9];
    return true;
  }
  return false;
}

void sendThermonuinoPacket(uint8_t frameType, uint8_t sequence) {
  uint8_t payload[RF_MAX_PACKET_LEN] = {0};
  const uint8_t length = buildRfPacket(payload, frameType, sequence, 0xFF);
  cc1101TransferStrobe(CC1101_SIDLE);
  cc1101TransferStrobe(CC1101_SFTX);

  digitalWrite(PIN_RF_CSN, LOW);
  waitCc1101Ready(CC1101_READY_TIMEOUT_MS);
  SPI.transfer(CC1101_WRITE_BURST | CC1101_TXFIFO);
  SPI.transfer(length);
  for (uint8_t i = 0; i < length; i++) {
    SPI.transfer(payload[i]);
  }
  digitalWrite(PIN_RF_CSN, HIGH);
  cc1101TransferStrobe(CC1101_STX);
  waitRfTxComplete();
}

bool rfChannelBusy() {
  cc1101FlushRx();
  cc1101TransferStrobe(CC1101_SRX);
  delay(RF_CHANNEL_LISTEN_MS);
  const uint8_t pktStatus = cc1101ReadRegisterValue(CC1101_PKTSTATUS);
  const uint8_t rxBytes = cc1101ReadRegisterValue(CC1101_RXBYTES) & 0x7F;
  cc1101FlushRx();
  return (pktStatus & 0x10) == 0 || rxBytes >= 7; // CCA=0 means channel not clear.
}

void runRfBeaconExchange() {
  rfPowerOn();
  SPI.beginTransaction(RF_SPI_SETTINGS);
  cc1101ConfigureTestRadio();

  bool received = false;
  for (uint8_t attempt = 0; attempt < RF_MAX_ATTEMPTS && !received; attempt++) {
    if (rfChannelBusy()) {
      delay(random(RF_BACKOFF_MIN_MS, RF_BACKOFF_MIN_MS + RF_BACKOFF_SPAN_MS + 1) + attempt * RF_BACKOFF_STEP_MS);
    }

    const uint8_t beaconSequence = rfSequence;
    rfSequence++;
    for (uint8_t copy = 0; copy < RF_TX_COPIES_PER_ATTEMPT; copy++) {
      sendThermonuinoPacket(RF_FRAME_REPORT, beaconSequence);
      if (copy + 1 < RF_TX_COPIES_PER_ATTEMPT) {
        delay(RF_TX_COPY_GAP_MS);
      }
    }
    cc1101TransferStrobe(CC1101_SRX);
    delay(RF_RX_SETTLE_MS);

    const unsigned long rxStartedAt = millis();
    while ((uint32_t)(millis() - rxStartedAt) < RF_ACK_TIMEOUT_MS) {
      uint8_t ackSequence = 0;
      if (readThermonuinoPacket(RF_DIAL_NODE_ID, RF_FRAME_RESPONSE, beaconSequence, ackSequence)) {
        received = true;
        break;
      }
      delay(10);
    }

    if (!received) {
      delay(random(RF_BACKOFF_MIN_MS, RF_BACKOFF_MIN_MS + RF_BACKOFF_SPAN_MS + 1) + attempt * RF_BACKOFF_STEP_MS);
    }
  }

  cc1101TransferStrobe(CC1101_SIDLE);
  SPI.endTransaction();
  rfPowerOff();
  if (received) {
    doorToggleCountSinceAck = 0;
  }
  startRfResultIndicator(received);
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
  if (updateRfResultIndicator()) {
    return;
  }

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
  doorOpenState = readDoorOpenState();
  lastDoorOpenState = doorOpenState;

  pinMode(PIN_RF_EN, INPUT);
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
  randomSeed(analogRead(A0) ^ micros());
  lastRfBeaconAt = millis() - RF_BEACON_INTERVAL_MS;
  
}

void loop() {
  updateDoorState();
  if ((uint32_t)(millis() - lastRfBeaconAt) >= RF_BEACON_INTERVAL_MS) {
    runRfBeaconExchange();
    lastRfBeaconAt = millis();
  }
  updateInputLed();
  delay(5);
}
