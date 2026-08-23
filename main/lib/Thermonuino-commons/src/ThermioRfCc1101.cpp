#include "ThermioRfCc1101.h"

ThermioRfCc1101::ThermioRfCc1101(const Pins &pins, const SPISettings &settings)
    : pins_(pins), settings_(settings) {}

void ThermioRfCc1101::beginPins() {
  pinMode(pins_.csn, OUTPUT);
  pinMode(pins_.gdo0, INPUT);
  pinMode(pins_.mosi, OUTPUT);
  pinMode(pins_.miso, INPUT);
  pinMode(pins_.sck, OUTPUT);
  digitalWrite(pins_.csn, HIGH);
  digitalWrite(pins_.mosi, LOW);
  digitalWrite(pins_.sck, LOW);
}

void ThermioRfCc1101::wake() {
  digitalWrite(pins_.csn, LOW);
  delayMicroseconds(10);
  digitalWrite(pins_.csn, HIGH);
  delay(20);
}

void ThermioRfCc1101::sleep() {
  SPI.beginTransaction(settings_);
  strobe(Sidle);
  strobe(Spwd);
  SPI.endTransaction();
  digitalWrite(pins_.csn, HIGH);
  digitalWrite(pins_.mosi, LOW);
  digitalWrite(pins_.sck, LOW);
}

bool ThermioRfCc1101::waitReady(uint16_t timeoutMs) {
  const uint32_t startedAt = millis();
  while (digitalRead(pins_.miso) == HIGH) {
    if ((uint32_t)(millis() - startedAt) >= timeoutMs) {
      return false;
    }
  }
  return true;
}

uint8_t ThermioRfCc1101::strobe(uint8_t value) {
  digitalWrite(pins_.csn, LOW);
  waitReady(15);
  const uint8_t status = SPI.transfer(value);
  digitalWrite(pins_.csn, HIGH);
  return status;
}

bool ThermioRfCc1101::readStatus(uint8_t address, uint8_t &value) {
  digitalWrite(pins_.csn, LOW);
  if (!waitReady(15)) {
    digitalWrite(pins_.csn, HIGH);
    return false;
  }
  SPI.transfer(ReadBurst | address);
  value = SPI.transfer(0x00);
  digitalWrite(pins_.csn, HIGH);
  return true;
}

uint8_t ThermioRfCc1101::readStatusValue(uint8_t address) {
  uint8_t value = 0xFF;
  readStatus(address, value);
  return value;
}

void ThermioRfCc1101::writeRegister(uint8_t address, uint8_t value) {
  digitalWrite(pins_.csn, LOW);
  waitReady(15);
  SPI.transfer(address);
  SPI.transfer(value);
  digitalWrite(pins_.csn, HIGH);
}

void ThermioRfCc1101::reset() {
  digitalWrite(pins_.csn, HIGH);
  delayMicroseconds(5);
  digitalWrite(pins_.csn, LOW);
  waitReady(15);
  SPI.transfer(Sres);
  digitalWrite(pins_.csn, HIGH);
  delay(2);
}

bool ThermioRfCc1101::testSpi(uint16_t totalTimeoutMs) {
  const uint32_t startedAt = millis();
  SPI.beginTransaction(settings_);
  uint8_t partnum = 0xFF;
  uint8_t version1 = 0xFF;
  uint8_t version2 = 0xFF;
  const bool readOk =
      readStatus(Partnum, partnum) &&
      readStatus(Version, version1) &&
      readStatus(Version, version2);
  SPI.endTransaction();

  if ((uint32_t)(millis() - startedAt) >= totalTimeoutMs) {
    return false;
  }
  return readOk && partnum == 0x00 && version1 == version2 && version1 != 0x00 && version1 != 0xFF;
}

void ThermioRfCc1101::configureTestRadio(uint8_t maxPacketLen) {
  reset();
  strobe(Sidle);
  strobe(Sfrx);
  strobe(Sftx);

  writeRegister(0x02, 0x06);
  writeRegister(0x07, maxPacketLen);
  writeRegister(0x08, 0x05);
  writeRegister(0x0B, 0x06);
  writeRegister(0x0D, 0x10);
  writeRegister(0x0E, 0xB0);
  writeRegister(0x0F, 0x71);
  writeRegister(0x10, 0xF5);
  writeRegister(0x11, 0x83);
  writeRegister(0x12, 0x13);
  writeRegister(0x15, 0x15);
  writeRegister(0x18, 0x18);
  writeRegister(0x19, 0x16);
  writeRegister(0x23, 0xE9);
  writeRegister(0x24, 0x2A);
  writeRegister(0x25, 0x00);
  writeRegister(0x26, 0x1F);

  digitalWrite(pins_.csn, LOW);
  waitReady(15);
  SPI.transfer(WriteBurst | 0x3E);
  SPI.transfer(0xC0);
  digitalWrite(pins_.csn, HIGH);
}

bool ThermioRfCc1101::channelBusy(uint16_t listenMs) {
  flushRx();
  strobeRx();
  delay(listenMs);
  const uint8_t pktStatus = readStatusValue(Pktstatus);
  const uint8_t bytes = readStatusValue(Rxbytes) & 0x7F;
  flushRx();
  return (pktStatus & 0x10) == 0 || bytes >= 7;
}

void ThermioRfCc1101::strobeRx() {
  strobe(Srx);
}

void ThermioRfCc1101::idle() {
  strobe(Sidle);
}

void ThermioRfCc1101::flushRx() {
  strobe(Sidle);
  strobe(Sfrx);
}

void ThermioRfCc1101::flushTx() {
  strobe(Sidle);
  strobe(Sftx);
}

bool ThermioRfCc1101::rxOverflow() {
  return (readStatusValue(Rxbytes) & 0x80) != 0;
}

uint8_t ThermioRfCc1101::rxBytes() {
  return readStatusValue(Rxbytes) & 0x7F;
}

bool ThermioRfCc1101::writePacket(const uint8_t *packet, uint8_t length) {
  flushTx();
  digitalWrite(pins_.csn, LOW);
  if (!waitReady(15)) {
    digitalWrite(pins_.csn, HIGH);
    return false;
  }
  SPI.transfer(WriteBurst | Txfifo);
  SPI.transfer(length);
  for (uint8_t i = 0; i < length; i++) {
    SPI.transfer(packet[i]);
  }
  digitalWrite(pins_.csn, HIGH);
  strobe(Stx);
  return true;
}

uint8_t ThermioRfCc1101::readPacket(uint8_t *packet, uint8_t maxLength) {
  const uint8_t bytes = rxBytes();
  if (bytes < 2) {
    return 0;
  }

  digitalWrite(pins_.csn, LOW);
  if (!waitReady(15)) {
    digitalWrite(pins_.csn, HIGH);
    return 0;
  }
  SPI.transfer(ReadBurst | Rxfifo);
  uint8_t length = SPI.transfer(0x00);
  if (length > bytes - 1) {
    length = bytes - 1;
  }
  if (length > maxLength) {
    length = maxLength;
  }
  for (uint8_t i = 0; i < length; i++) {
    packet[i] = SPI.transfer(0x00);
  }
  digitalWrite(pins_.csn, HIGH);
  flushRx();
  strobeRx();
  return length;
}

void ThermioRfCc1101::waitTxComplete(uint16_t timeoutMs) {
  const uint32_t startedAt = millis();
  while (digitalRead(pins_.gdo0) == LOW) {
    if ((uint32_t)(millis() - startedAt) >= timeoutMs) {
      return;
    }
  }
  while (digitalRead(pins_.gdo0) == HIGH) {
    if ((uint32_t)(millis() - startedAt) >= timeoutMs) {
      return;
    }
  }
}
