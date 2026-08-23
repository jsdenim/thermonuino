#pragma once

#include <Arduino.h>
#include <SPI.h>

class ThermioRfCc1101 {
public:
  struct Pins {
    uint8_t csn;
    uint8_t gdo0;
    uint8_t mosi;
    uint8_t miso;
    uint8_t sck;
  };

  explicit ThermioRfCc1101(const Pins &pins, const SPISettings &settings);

  void beginPins();
  void wake();
  void sleep();
  bool testSpi(uint16_t totalTimeoutMs = 120);
  void configureTestRadio(uint8_t maxPacketLen);
  bool channelBusy(uint16_t listenMs);

  void strobeRx();
  void idle();
  void flushRx();
  void flushTx();
  bool rxOverflow();
  uint8_t rxBytes();

  bool writePacket(const uint8_t *packet, uint8_t length);
  uint8_t readPacket(uint8_t *packet, uint8_t maxLength);

  void waitTxComplete(uint16_t timeoutMs);

private:
  static constexpr uint8_t ReadBurst = 0xC0;
  static constexpr uint8_t WriteBurst = 0x40;
  static constexpr uint8_t Partnum = 0x30;
  static constexpr uint8_t Version = 0x31;
  static constexpr uint8_t Txfifo = 0x3F;
  static constexpr uint8_t Rxfifo = 0x3F;
  static constexpr uint8_t Pktstatus = 0x38;
  static constexpr uint8_t Rxbytes = 0x3B;
  static constexpr uint8_t Sres = 0x30;
  static constexpr uint8_t Srx = 0x34;
  static constexpr uint8_t Stx = 0x35;
  static constexpr uint8_t Sidle = 0x36;
  static constexpr uint8_t Spwd = 0x39;
  static constexpr uint8_t Sfrx = 0x3A;
  static constexpr uint8_t Sftx = 0x3B;

  Pins pins_;
  SPISettings settings_;

  bool waitReady(uint16_t timeoutMs);
  uint8_t strobe(uint8_t value);
  bool readStatus(uint8_t address, uint8_t &value);
  uint8_t readStatusValue(uint8_t address);
  void writeRegister(uint8_t address, uint8_t value);
  void reset();
};
