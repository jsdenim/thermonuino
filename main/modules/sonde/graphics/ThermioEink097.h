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

    configureBase();
    command(0x3C);
    data(0x05);
    SPI.endTransaction();

    return swResetOk;
  }

  bool writeFrontImage(PixelReader reader,
                       void *context,
                       uint32_t busyTimeoutMs = DefaultBusyTimeoutMs) {
    SPI.beginTransaction(settings_);
    writeRamImage(0x26, reader, context);
    writeRamImage(0x24, reader, context);
    const bool refreshOk = refreshInsideTransaction(0xF7, busyTimeoutMs);
    SPI.endTransaction();
    return refreshOk;
  }

  bool writeFrontImagePartial(PixelReader reader,
                              void *context,
                              uint16_t frontX,
                              uint16_t frontY,
                              uint16_t frontW,
                              uint16_t frontH,
                              uint32_t busyTimeoutMs = DefaultBusyTimeoutMs) {
    if (frontW == 0 || frontH == 0 ||
        frontX >= FrontWidth || frontY >= FrontHeight) {
      return true;
    }

    if (frontX + frontW > FrontWidth) {
      frontW = FrontWidth - frontX;
    }
    if (frontY + frontH > FrontHeight) {
      frontH = FrontHeight - frontY;
    }

    SPI.beginTransaction(settings_);
    writeRamImageWindow(0x24, reader, context, frontX, frontY, frontW, frontH);
    const bool refreshOk = refreshInsideTransaction(0xFF, busyTimeoutMs);
    SPI.endTransaction();
    return refreshOk;
  }

  bool writeFrontImagePartial(PixelReader oldReader,
                              void *oldContext,
                              PixelReader newReader,
                              void *newContext,
                              uint16_t frontX,
                              uint16_t frontY,
                              uint16_t frontW,
                              uint16_t frontH,
                              uint32_t busyTimeoutMs = DefaultBusyTimeoutMs) {
    if (frontW == 0 || frontH == 0 ||
        frontX >= FrontWidth || frontY >= FrontHeight) {
      return true;
    }

    if (frontX + frontW > FrontWidth) {
      frontW = FrontWidth - frontX;
    }
    if (frontY + frontH > FrontHeight) {
      frontH = FrontHeight - frontY;
    }

    SPI.beginTransaction(settings_);
    writeRamImageWindow(0x26, oldReader, oldContext, frontX, frontY, frontW, frontH);
    writeRamImageWindow(0x24, newReader, newContext, frontX, frontY, frontW, frontH);
    const bool refreshOk = refreshInsideTransaction(0xFF, busyTimeoutMs);
    SPI.endTransaction();
    return refreshOk;
  }

  bool wakeForPartialUpdate(uint32_t busyTimeoutMs = DefaultBusyTimeoutMs) {
    reset();

    SPI.beginTransaction(settings_);
    const bool busyOk = waitBusyLow(busyTimeoutMs);

    // GoodDisplay's partial update path uses a different border waveform.
    command(0x3C);
    data(0x80);

    setRamArea();
    setRamPointer(0, RamHeight - 1);
    SPI.endTransaction();
    return busyOk;
  }

  bool clearWhite(uint32_t busyTimeoutMs = DefaultBusyTimeoutMs) {
    SPI.beginTransaction(settings_);
    setRamArea();
    setRamPointer(0, RamHeight - 1);
    command(0x26);
    for (uint16_t i = 0; i < (uint16_t)RamWidthBytes * RamHeight; i++) {
      data(0xFF);
    }

    setRamPointer(0, RamHeight - 1);
    command(0x24);
    for (uint16_t i = 0; i < (uint16_t)RamWidthBytes * RamHeight; i++) {
      data(0xFF);
    }

    const bool refreshOk = refreshInsideTransaction(0xF7, busyTimeoutMs);
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

  void configureBase() {
    // GoodDisplay GDEM0097T61 official RAM geometry: 88 x 184.
    command(0x01);
    data((RamHeight - 1) & 0xFF);
    data((RamHeight - 1) >> 8);
    data(0x00);

    command(0x11);
    data(0x01);

    setRamArea();
    setRamPointer(0, RamHeight - 1);

    command(0x18);
    data(0x80);

    command(0x21);
    data(0x00);
    data(0x80);
  }

  void writeRamImage(uint8_t ramCommand, PixelReader reader, void *context) {
    setRamArea();
    setRamPointer(0, RamHeight - 1);
    command(ramCommand);
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
  }

  void writeRamImageWindow(uint8_t ramCommand,
                           PixelReader reader,
                           void *context,
                           uint16_t frontX,
                           uint16_t frontY,
                           uint16_t frontW,
                           uint16_t frontH) {
    const uint16_t ramXMin = frontY;
    const uint16_t ramXMax = frontY + frontH - 1;
    const uint16_t ramYMin = frontX;
    const uint16_t ramYMax = frontX + frontW - 1;
    const uint8_t ramXByteStart = ramXMin / 8;
    const uint8_t ramXByteEnd = ramXMax / 8;

    command(0x44);
    data(ramXByteStart);
    data(ramXByteEnd);

    command(0x45);
    data(ramYMax & 0xFF);
    data(ramYMax >> 8);
    data(ramYMin & 0xFF);
    data(ramYMin >> 8);

    setRamPointer(ramXByteStart, ramYMax);
    command(ramCommand);
    for (uint16_t ramY = ramYMin; ramY <= ramYMax; ramY++) {
      for (uint8_t ramXByte = ramXByteStart; ramXByte <= ramXByteEnd; ramXByte++) {
        uint8_t value = 0xFF;
        for (uint8_t bit = 0; bit < 8; bit++) {
          const uint16_t ramX = (uint16_t)ramXByte * 8 + bit;
          uint16_t mappedFrontX;
          uint16_t mappedFrontY;
          mapRamToReadableFront(ramX, ramY, mappedFrontX, mappedFrontY);
          if (reader && reader(mappedFrontX, mappedFrontY, context)) {
            value &= ~(0x80 >> bit);
          }
        }
        data(value);
      }
    }

  }

  bool refreshInsideTransaction(uint8_t updateControl, uint32_t busyTimeoutMs) {
    command(0x22);
    data(updateControl);
    command(0x20);
    return waitBusyLow(busyTimeoutMs);
  }
};

#endif
