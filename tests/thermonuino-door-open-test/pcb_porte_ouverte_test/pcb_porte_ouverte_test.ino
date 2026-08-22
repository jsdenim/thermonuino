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
const uint8_t RF_TX_COPIES_PER_ATTEMPT = 3;
const unsigned int RF_TX_COPY_GAP_MS = 60;
const unsigned long RF_RESULT_LED_MS = 1000;
const unsigned int RF_RESULT_FAIL_ON_MS = 750;
const unsigned int RF_RESULT_FAIL_OFF_MS = 250;
const uint8_t RF_NODE_ID = 'D';
const uint8_t RF_DIAL_NODE_ID = 'C';
const uint8_t RF_PACKET_BEACON = 'B';
const uint8_t RF_PACKET_ACK = 'A';

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

void startRfResultIndicator(bool ackReceived) {
  rfResultActive = true;
  rfResultAckReceived = ackReceived;
  rfResultLedOn = true;
  rfResultUntil = millis() + RF_RESULT_LED_MS;
  nextRfResultToggleAt = millis() + RF_RESULT_FAIL_ON_MS;
  ledOn();
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

  if ((int32_t)(millis() - nextRfResultToggleAt) >= 0) {
    rfResultLedOn = !rfResultLedOn;
    digitalWrite(PIN_LED, rfResultLedOn ? HIGH : LOW);
    nextRfResultToggleAt = millis() + (rfResultLedOn ? RF_RESULT_FAIL_ON_MS : RF_RESULT_FAIL_OFF_MS);
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
  cc1101WriteRegister(0x07, 0x08); // PKTLEN
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

bool readThermonuinoPacket(uint8_t expectedSource, uint8_t expectedKind, uint8_t expectedSequence, uint8_t &sequence) {
  const uint8_t rxBytesRaw = cc1101ReadRegisterValue(CC1101_RXBYTES);
  if ((rxBytesRaw & 0x80) != 0) {
    cc1101FlushRx();
    cc1101TransferStrobe(CC1101_SRX);
    return false;
  }

  const uint8_t rxBytes = rxBytesRaw & 0x7F;
  if (rxBytes < 7) {
    return false;
  }

  uint8_t payload[8] = {0};
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

  const bool ok = length >= 6 &&
      payload[0] == 'T' &&
      payload[1] == 'N' &&
      payload[2] == 'U' &&
      payload[3] == expectedSource &&
      payload[3] != RF_NODE_ID &&
      payload[5] == expectedKind &&
      (expectedSequence == 0xFF || payload[4] == expectedSequence);
  if (ok) {
    sequence = payload[4];
  }
  return ok;
}

void sendThermonuinoPacket(uint8_t packetKind, uint8_t sequence) {
  const uint8_t payload[] = {'T', 'N', 'U', RF_NODE_ID, sequence, packetKind};
  cc1101TransferStrobe(CC1101_SIDLE);
  cc1101TransferStrobe(CC1101_SFTX);

  digitalWrite(PIN_RF_CSN, LOW);
  waitCc1101Ready(CC1101_READY_TIMEOUT_MS);
  SPI.transfer(CC1101_WRITE_BURST | CC1101_TXFIFO);
  SPI.transfer(sizeof(payload));
  for (uint8_t i = 0; i < sizeof(payload); i++) {
    SPI.transfer(payload[i]);
  }
  digitalWrite(PIN_RF_CSN, HIGH);
  cc1101TransferStrobe(CC1101_STX);
  delay(40);
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
      sendThermonuinoPacket(RF_PACKET_BEACON, beaconSequence);
      if (copy + 1 < RF_TX_COPIES_PER_ATTEMPT) {
        delay(RF_TX_COPY_GAP_MS);
      }
    }
    cc1101TransferStrobe(CC1101_SRX);
    delay(RF_RX_SETTLE_MS);

    const unsigned long rxStartedAt = millis();
    while ((uint32_t)(millis() - rxStartedAt) < RF_ACK_TIMEOUT_MS) {
      uint8_t ackSequence = 0;
      if (readThermonuinoPacket(RF_DIAL_NODE_ID, RF_PACKET_ACK, beaconSequence, ackSequence)) {
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
  if ((uint32_t)(millis() - lastRfBeaconAt) >= RF_BEACON_INTERVAL_MS) {
    runRfBeaconExchange();
    lastRfBeaconAt = millis();
  }
  updateInputLed();
  delay(5);
}
