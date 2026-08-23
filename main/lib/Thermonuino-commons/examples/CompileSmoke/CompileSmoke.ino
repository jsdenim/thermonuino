#include <SPI.h>

#include <ThermioRfCc1101.h>
#include <ThermioRfFrame.h>
#include <ThermioRfIds.h>
#include <ThermioSlaveLink.h>
#include <ThermioSlavePower.h>

ThermioRfCc1101::Pins rfPins = {
  10, // CSN
  9,  // GDO0
  11, // MOSI
  12, // MISO
  13  // SCK
};

ThermioRfCc1101 radio(rfPins, SPISettings(1000000, MSBFIRST, SPI_MODE0));
ThermioSlaveLink link(5);

void setup() {
  uint8_t packet[ThermioRfFrame::MaxPacketLen] = {0};
  ThermioRfFrame::Header header;
  header.frameType = ThermioRfFrame::FrameReport;
  header.sourceId = 1;
  header.targetId = ThermioRfFrame::BroadcastId;
  header.payloadLen = ThermioRfFrame::ReportPayloadLen;
  ThermioRfFrame::writeHeader(packet, header);

  ThermioRfFrame::Report report;
  report.deviceType = ThermioRfFrame::DeviceDoor;
  ThermioRfFrame::encodeReportPayload(packet + ThermioRfFrame::HeaderLen, report);

  link.setLocalId(1);
}

void loop() {}
