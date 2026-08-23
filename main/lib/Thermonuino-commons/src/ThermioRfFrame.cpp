#include "ThermioRfFrame.h"

namespace ThermioRfFrame {

uint16_t readU16(const uint8_t *buffer, uint8_t offset) {
  return (uint16_t)buffer[offset] | ((uint16_t)buffer[offset + 1] << 8);
}

void writeU16(uint8_t *buffer, uint8_t offset, uint16_t value) {
  buffer[offset] = value & 0xFF;
  buffer[offset + 1] = value >> 8;
}

uint8_t expectedPayloadLen(uint8_t frameType) {
  return frameType == FrameResponse ? ResponsePayloadLen : ReportPayloadLen;
}

bool readHeader(const uint8_t *packet, uint8_t length, Header &header) {
  if (length < HeaderLen ||
      packet[0] != 'T' ||
      packet[1] != 'N' ||
      packet[2] != 'U' ||
      packet[3] != ProtocolVersion) {
    return false;
  }

  header.frameType = packet[4];
  header.sourceId = readU16(packet, 5);
  header.targetId = readU16(packet, 7);
  header.sequence = packet[9];
  header.ackSequence = packet[10];
  header.payloadLen = packet[11];
  return length == HeaderLen + header.payloadLen &&
      header.payloadLen == expectedPayloadLen(header.frameType);
}

uint8_t writeHeader(uint8_t *packet, const Header &header) {
  packet[0] = 'T';
  packet[1] = 'N';
  packet[2] = 'U';
  packet[3] = ProtocolVersion;
  packet[4] = header.frameType;
  writeU16(packet, 5, header.sourceId);
  writeU16(packet, 7, header.targetId);
  packet[9] = header.sequence;
  packet[10] = header.ackSequence;
  packet[11] = header.payloadLen;
  return HeaderLen;
}

bool decodeReport(const uint8_t *packet, uint8_t length, Report &report, uint8_t maxZone) {
  Header header;
  if (!readHeader(packet, length, header) || header.frameType != FrameReport) {
    return false;
  }

  const uint8_t *payload = packet + HeaderLen;
  const int8_t userDeltaSteps = (int8_t)payload[ReportUserDeltaSteps];
  const uint8_t tempCount = payload[ReportTempCount];
  const uint8_t pairZoneRequest = payload[ReportPairZoneRequest];
  if (pairZoneRequest > maxZone ||
      tempCount > 12 ||
      payload[ReportDoorOpen] > 1 ||
      userDeltaSteps < -8 ||
      userDeltaSteps > 8) {
    return false;
  }

  report.deviceType = payload[ReportDeviceType];
  report.batteryMv = readU16(payload, ReportBatteryMv);
  report.pairZoneRequest = pairZoneRequest;
  report.adminRequest = payload[ReportAdminRequest];
  report.userDeltaSteps = userDeltaSteps;
  report.tempCount = tempCount;
  for (uint8_t i = 0; i < tempCount; i++) {
    report.temperaturesDeciC[i] = (int16_t)readU16(payload, ReportTemperatures + i * 2);
  }
  report.presenceCount = payload[ReportPresenceCount];
  report.doorToggleCount = payload[ReportDoorToggleCount];
  report.doorOpen = payload[ReportDoorOpen] != 0;
  return true;
}

bool decodeResponse(const uint8_t *packet, uint8_t length, Response &response, uint8_t maxZone) {
  Header header;
  if (!readHeader(packet, length, header) || header.frameType != FrameResponse) {
    return false;
  }

  const uint8_t *payload = packet + HeaderLen;
  const uint8_t assignedZone = payload[ResponseAssignedZone];
  const uint16_t nextReportDelayS = readU16(payload, ResponseNextReportDelayS);
  if (assignedZone > maxZone || nextReportDelayS == 0) {
    return false;
  }

  response.assignedZone = assignedZone;
  for (uint8_t i = 0; i < 6; i++) {
    response.dateTime[i] = payload[ResponseDateTime + i];
  }
  response.globalMode = payload[ResponseGlobalMode];
  response.heatActive = payload[ResponseHeatActive] != 0;
  response.zoneDoorOpen = payload[ResponseZoneDoorOpen] != 0;
  response.outsideTempDeciC = (int16_t)readU16(payload, ResponseOutsideTemp);
  response.usualSetpointDeciC = (int16_t)readU16(payload, ResponseUsualSetpoint);
  response.currentSetpointDeciC = (int16_t)readU16(payload, ResponseCurrentSetpoint);
  response.commandFlags = payload[ResponseCommandFlags];
  response.nextReportDelayS = nextReportDelayS;
  return true;
}

uint8_t encodeReportPayload(uint8_t *payload, const Report &report) {
  payload[ReportDeviceType] = report.deviceType;
  writeU16(payload, ReportBatteryMv, report.batteryMv);
  payload[ReportPairZoneRequest] = report.pairZoneRequest;
  payload[ReportAdminRequest] = report.adminRequest;
  payload[ReportUserDeltaSteps] = (uint8_t)report.userDeltaSteps;
  payload[ReportTempCount] = report.tempCount;
  for (uint8_t i = 0; i < report.tempCount && i < 12; i++) {
    writeU16(payload, ReportTemperatures + i * 2, (uint16_t)report.temperaturesDeciC[i]);
  }
  payload[ReportPresenceCount] = report.presenceCount;
  payload[ReportDoorToggleCount] = report.doorToggleCount;
  payload[ReportDoorOpen] = report.doorOpen ? 1 : 0;
  return ReportPayloadLen;
}

uint8_t encodeResponsePayload(uint8_t *payload, const Response &response) {
  payload[ResponseAssignedZone] = response.assignedZone;
  for (uint8_t i = 0; i < 6; i++) {
    payload[ResponseDateTime + i] = response.dateTime[i];
  }
  payload[ResponseGlobalMode] = response.globalMode;
  payload[ResponseHeatActive] = response.heatActive ? 1 : 0;
  payload[ResponseZoneDoorOpen] = response.zoneDoorOpen ? 1 : 0;
  writeU16(payload, ResponseOutsideTemp, (uint16_t)response.outsideTempDeciC);
  writeU16(payload, ResponseUsualSetpoint, (uint16_t)response.usualSetpointDeciC);
  writeU16(payload, ResponseCurrentSetpoint, (uint16_t)response.currentSetpointDeciC);
  payload[ResponseCommandFlags] = response.commandFlags;
  writeU16(payload, ResponseNextReportDelayS, response.nextReportDelayS);
  return ResponsePayloadLen;
}

} // namespace ThermioRfFrame
