#pragma once

#include <Arduino.h>

namespace ThermioRfFrame {

constexpr uint8_t ProtocolVersion = 1;
constexpr uint8_t HeaderLen = 12;
constexpr uint8_t MaxPacketLen = 64;
constexpr uint8_t ReportPayloadLen = 34;
constexpr uint8_t ResponsePayloadLen = 19;
constexpr uint16_t BroadcastId = 0x0000;

enum FrameType : uint8_t {
  FrameReport = 1,
  FrameResponse = 2,
};

enum DeviceType : uint8_t {
  DeviceSonde = 1,
  DeviceDoor = 2,
};

enum AdminRequest : uint8_t {
  AdminNone = 0,
  AdminPair = 1,
  AdminAssignCurrentToZone = 2,
  AdminEnterPairingForCurrentZone = 3,
  AdminClearZoneLearning = 4,
  AdminClearAllLearning = 5,
};

enum ReportOffset : uint8_t {
  ReportDeviceType = 0,
  ReportBatteryMv = 1,
  ReportPairZoneRequest = 3,
  ReportAdminRequest = 4,
  ReportUserDeltaSteps = 5,
  ReportTempCount = 6,
  ReportTemperatures = 7,
  ReportPresenceCount = 31,
  ReportDoorToggleCount = 32,
  ReportDoorOpen = 33,
};

enum ResponseOffset : uint8_t {
  ResponseAssignedZone = 0,
  ResponseDateTime = 1,
  ResponseGlobalMode = 7,
  ResponseHeatActive = 8,
  ResponseZoneDoorOpen = 9,
  ResponseOutsideTemp = 10,
  ResponseUsualSetpoint = 12,
  ResponseCurrentSetpoint = 14,
  ResponseCommandFlags = 16,
  ResponseNextReportDelayS = 17,
};

enum ResponseCommandFlag : uint8_t {
  ResponseFlagHeatLastHour = 0x01,
};

struct Header {
  uint8_t frameType = 0;
  uint16_t sourceId = BroadcastId;
  uint16_t targetId = BroadcastId;
  uint8_t sequence = 0;
  uint8_t ackSequence = 0xFF;
  uint8_t payloadLen = 0;
};

struct Report {
  uint8_t deviceType = 0;
  uint16_t batteryMv = 0;
  uint8_t pairZoneRequest = 0;
  uint8_t adminRequest = AdminNone;
  int8_t userDeltaSteps = 0;
  uint8_t tempCount = 0;
  int16_t temperaturesDeciC[12] = {0};
  uint8_t presenceCount = 0;
  uint8_t doorToggleCount = 0;
  bool doorOpen = false;
};

struct Response {
  uint8_t assignedZone = 0;
  uint8_t dateTime[6] = {0};
  uint8_t globalMode = 0;
  bool heatActive = false;
  bool zoneDoorOpen = false;
  int16_t outsideTempDeciC = 0;
  int16_t usualSetpointDeciC = 0;
  int16_t currentSetpointDeciC = 0;
  uint8_t commandFlags = 0;
  uint16_t nextReportDelayS = 0;
};

uint16_t readU16(const uint8_t *buffer, uint8_t offset);
void writeU16(uint8_t *buffer, uint8_t offset, uint16_t value);
uint8_t expectedPayloadLen(uint8_t frameType);

bool readHeader(const uint8_t *packet, uint8_t length, Header &header);
uint8_t writeHeader(uint8_t *packet, const Header &header);

bool decodeReport(const uint8_t *packet, uint8_t length, Report &report, uint8_t maxZone);
bool decodeResponse(const uint8_t *packet, uint8_t length, Response &response, uint8_t maxZone);

uint8_t encodeReportPayload(uint8_t *payload, const Report &report);
uint8_t encodeResponsePayload(uint8_t *payload, const Response &response);

} // namespace ThermioRfFrame
