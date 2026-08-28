#ifndef THERMIO_EINK_097_H
#define THERMIO_EINK_097_H

#include <Arduino.h>
#include <SPI.h>

class ThermioEink097 {
public:
  struct Pins {
    uint8_t cs;
    uint8_t dc;
    uint8_t rst;
    uint8_t busy;
    uint8_t rfCsn;
  };

  typedef bool (*PixelReader)(uint16_t x, uint16_t y, void *context);

  static const uint16_t RamWidth = 88;
  static const uint16_t RamHeight = 184;
  static const uint16_t FrontWidth = 184;
  static const uint16_t FrontHeight = 88;
  static const uint16_t PhysicalWidth = 184;
  static const uint16_t PhysicalHeight = 88;
  static const uint8_t RamWidthBytes = RamWidth / 8;
  static const uint32_t DefaultBusyTimeoutMs = 12000;

  ThermioEink097(const Pins &pins,
                 const SPISettings &settings = SPISettings(500000, MSBFIRST, SPI_MODE0))
      : pins_(pins), settings_(settings) {
  }

  void begin() {
    pinMode(pins_.cs, OUTPUT);
    pinMode(pins_.dc, OUTPUT);
    pinMode(pins_.rst, OUTPUT);
    pinMode(pins_.busy, INPUT);
    pinMode(pins_.rfCsn, OUTPUT);

    digitalWrite(pins_.cs, HIGH);
    digitalWrite(pins_.dc, LOW);
    digitalWrite(pins_.rst, HIGH);
    digitalWrite(pins_.rfCsn, HIGH);
  }

  bool init(uint32_t busyTimeoutMs = DefaultBusyTimeoutMs) {
    reset();

    SPI.beginTransaction(settings_);
    command(0x12);
    const bool swResetOk = waitBusyLow(busyTimeoutMs);

    // GoodDisplay GDEM0097T61 official RAM geometry: 88 x 184.
    command(0x01);
    data((RamHeight - 1) & 0xFF);
    data((RamHeight - 1) >> 8);
    data(0x00);

    command(0x11);
    data(0x01);

    setRamArea();
    setRamPointer(0, RamHeight - 1);

    command(0x3C);
    data(0x05);

    command(0x18);
    data(0x80);

    command(0x21);
    data(0x00);
    data(0x80);
    SPI.endTransaction();

    return swResetOk;
  }

  bool writeFrontImage(PixelReader reader,
                       void *context,
                       uint32_t busyTimeoutMs = DefaultBusyTimeoutMs) {
    SPI.beginTransaction(settings_);
    setRamPointer(0, RamHeight - 1);
    command(0x26);
    for (uint16_t i = 0; i < (uint16_t)RamWidthBytes * RamHeight; i++) {
      data(0xFF);
    }

    setRamPointer(0, RamHeight - 1);
    command(0x24);
    for (uint16_t ramY = 0; ramY < RamHeight; ramY++) {
      for (uint8_t ramXByte = 0; ramXByte < RamWidthBytes; ramXByte++) {
        uint8_t value = 0xFF;
        for (uint8_t bit = 0; bit < 8; bit++) {
          const uint16_t ramX = (uint16_t)ramXByte * 8 + bit;
          uint16_t frontX;
          uint16_t frontY;
          mapRamToReadableFront(ramX, ramY, frontX, frontY);

          if (reader && reader(frontX, frontY, context)) {
            value &= ~(0x80 >> bit);
          }
        }
        data(value);
      }
    }

    const bool refreshOk = refreshInsideTransaction(busyTimeoutMs);
    SPI.endTransaction();
    return refreshOk;
  }

  bool clearWhite(uint32_t busyTimeoutMs = DefaultBusyTimeoutMs) {
    SPI.beginTransaction(settings_);
    setRamPointer(0, RamHeight - 1);
    command(0x24);
    for (uint16_t i = 0; i < (uint16_t)RamWidthBytes * RamHeight; i++) {
      data(0xFF);
    }
    const bool refreshOk = refreshInsideTransaction(busyTimeoutMs);
    SPI.endTransaction();
    return refreshOk;
  }

  void sleep() {
    SPI.beginTransaction(settings_);
    command(0x10);
    data(0x01);
    SPI.endTransaction();
    delay(100);
  }

  static void mapRamToReadableFront(uint16_t ramX,
                                    uint16_t ramY,
                                    uint16_t &frontX,
                                    uint16_t &frontY) {
    // The controller is addressed like GoodDisplay's demo (RAM 88 x 184).
    // The sonde screen is mounted horizontally, so the application draws in
    // the visible 184 x 88 orientation and this mapping rotates RAM to front.
    frontX = ramY;
    frontY = ramX;
  }

private:
  Pins pins_;
  SPISettings settings_;

  void reset() {
    digitalWrite(pins_.rst, HIGH);
    delay(20);
    digitalWrite(pins_.rst, LOW);
    delay(20);
    digitalWrite(pins_.rst, HIGH);
    delay(200);
  }

  bool waitBusyLow(uint32_t timeoutMs) {
    const uint32_t startedAt = millis();
    while (digitalRead(pins_.busy) == HIGH) {
      if ((uint32_t)(millis() - startedAt) >= timeoutMs) {
        return false;
      }
      delay(10);
    }
    return true;
  }

  void command(uint8_t value) {
    digitalWrite(pins_.rfCsn, HIGH);
    digitalWrite(pins_.dc, LOW);
    digitalWrite(pins_.cs, LOW);
    SPI.transfer(value);
    digitalWrite(pins_.cs, HIGH);
  }

  void data(uint8_t value) {
    digitalWrite(pins_.rfCsn, HIGH);
    digitalWrite(pins_.dc, HIGH);
    digitalWrite(pins_.cs, LOW);
    SPI.transfer(value);
    digitalWrite(pins_.cs, HIGH);
  }

  void setRamArea() {
    command(0x44);
    data(0x00);
    data(RamWidthBytes - 1);

    command(0x45);
    data((RamHeight - 1) & 0xFF);
    data((RamHeight - 1) >> 8);
    data(0x00);
    data(0x00);
  }

  void setRamPointer(uint8_t xByte, uint16_t y) {
    command(0x4E);
    data(xByte);
    command(0x4F);
    data(y & 0xFF);
    data(y >> 8);
  }

  bool refreshInsideTransaction(uint32_t busyTimeoutMs) {
    command(0x22);
    data(0xF7);
    command(0x20);
    return waitBusyLow(busyTimeoutMs);
  }
};

#endif
