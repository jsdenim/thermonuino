/*
  Thermonuino - console

  Premier sketch de production console :
    - ID console en EEPROM interne ;
    - reception RF CC1101 permanente ;
    - association des esclaves, 5 affectations dont exterieur ;
    - table courte EEPROM interne : 2 esclaves par affectation ;
    - ACK applicatif commun vers les esclaves.

  La logique chauffage/metier sera branchee ensuite.
*/

#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>
#include <SPI.h>
#include <stdlib.h>

#include <ThermioRfCc1101.h>
#include <ThermioRfFrame.h>
#include <ThermioRfIds.h>

constexpr uint8_t PIN_LED_CHAIN_DATA = A3;
constexpr uint8_t LED_COUNT = 6;
constexpr uint8_t LED_SALON = 0;
constexpr uint8_t LED_CHAMBRE = 1;
constexpr uint8_t LED_BUREAU = 2;
constexpr uint8_t LED_SDB = 3;
constexpr uint8_t LED_CENTRE = 4;
constexpr uint8_t LED_MODE = 5;

constexpr uint8_t PIN_CC1101_CSN = 2;
constexpr uint8_t PIN_CC1101_GDO0 = A2;
constexpr uint8_t PIN_CC1101_MOSI = 11;
constexpr uint8_t PIN_CC1101_MISO = 12;
constexpr uint8_t PIN_CC1101_SCK = 13;

constexpr uint8_t PIN_MODE_DOUCHE = A0;
constexpr uint8_t PIN_MODE_STOP = A1;
constexpr uint8_t PIN_MODE_PLUS = 10;
constexpr uint8_t PIN_MODE_NORMAL = 9;
constexpr uint8_t PIN_MODE_MOINS = 8;
constexpr uint8_t PIN_MODE_VAC = 7;

constexpr uint16_t RF_DEFAULT_CONSOLE_ID = 0x0C01;
constexpr uint8_t RF_ASSOC_ZONE_COUNT = 5;
constexpr uint8_t RF_OUTSIDE_ZONE = 5;
constexpr uint8_t RF_ASSOC_SLAVES_PER_ZONE = 2;
constexpr uint8_t RF_MAX_ASSOCIATED_SLAVES = RF_ASSOC_ZONE_COUNT * RF_ASSOC_SLAVES_PER_ZONE;
constexpr uint8_t RF_ASSOC_ENTRY_LEN = 4;
constexpr uint32_t RF_ASSOCIATION_WINDOW_MS = 180000;
constexpr uint32_t RF_ASSOCIATION_SAVE_DELAY_MS = 10000;
constexpr uint32_t RF_RX_REFRESH_INTERVAL_MS = 500;
constexpr uint32_t RF_TX_COMPLETE_TIMEOUT_MS = 350;
constexpr uint32_t RF_ACK_REPLY_DELAY_MS = 300;
constexpr uint8_t RF_ACK_TX_COUNT = 3;
constexpr uint16_t RF_ACK_TX_GAP_MS = 150;
constexpr uint8_t PILOTE_ZONE_COUNT = 4;
constexpr uint8_t PILOTE_DEFAULT_CYCLE_MINUTES = 30;
constexpr unsigned long PILOTE_SERIAL_BAUD = 9600;
constexpr uint16_t MODE_DEBOUNCE_MS = 35;
constexpr uint16_t PLUS_MINUS_NORMAL_RETURN_MS = 3000;
constexpr uint32_t DOUCHE_DURATION_MS = 30UL * 60UL * 1000UL;
constexpr uint8_t ZONE_SDB = 4;
constexpr int16_t SETPOINT_NORMAL_DECI_C = 190;
constexpr int16_t SETPOINT_VACANCE_DECI_C = 170;
constexpr int16_t FALLBACK_MEASURED_TEMP_DECI_C = 180;
constexpr uint16_t FALLBACK_ZONE_POWER_VA = 1500;
constexpr uint16_t MIN_REQUEST_POWER_VA = 300;
constexpr uint16_t MAX_REQUEST_POWER_VA = 6000;
constexpr uint16_t REQUEST_POWER_PER_DECI_C_VA = 180;
constexpr uint8_t HEATING_HYSTERESIS_DECI_C = 2;
constexpr int8_t MODE_DELTA_STEP_C = 1;
constexpr int8_t DOUCHE_SDB_DELTA_C = 2;
constexpr int8_t DOUCHE_OTHER_DELTA_C = -1;
constexpr uint32_t HEAT_LAST_HOUR_MS = 60UL * 60UL * 1000UL;
constexpr uint32_t HEAT_LAST_DAY_MS = 24UL * 60UL * 60UL * 1000UL;

enum ResponseGlobalMode : uint8_t {
  RESPONSE_MODE_NORMAL = 0,
  RESPONSE_MODE_PLUS = 1,
  RESPONSE_MODE_MOINS = 2,
  RESPONSE_MODE_DOUCHE = 3,
  RESPONSE_MODE_STOP = 4,
  RESPONSE_MODE_VACANCE = 5,
};

constexpr ThermioRfIds::EepromSlot EEPROM_CONSOLE_ID = {
  0, 1, 2, 3, 0x54, 0x43
};

constexpr int EEPROM_ASSOC_MAGIC_0 = 4;
constexpr int EEPROM_ASSOC_MAGIC_1 = 5;
constexpr int EEPROM_ASSOC_FIRST = 6;
constexpr uint8_t EEPROM_ASSOC_MAGIC_VALUE_0 = 0x54;
constexpr uint8_t EEPROM_ASSOC_MAGIC_VALUE_1 = 0x41;

struct AssociatedSlave {
  uint16_t nodeId;
  uint8_t deviceType;
  uint8_t zone;
};

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

struct ZoneState {
  uint8_t workload;
  uint16_t powerVa;
  uint32_t lastHeatAt;
  int16_t measuredTempDeciC;
  bool hasTemperature;
  bool heatSeen;
  bool doorOpen;
  bool sondeLowBattery;
  bool doorLowBattery;
  bool sondeMissing;
  bool doorMissing;
};

const ModeInput modeInputs[] = {
  {PIN_MODE_DOUCHE, MODE_DOUCHE},
  {PIN_MODE_STOP, MODE_STOP},
  {PIN_MODE_PLUS, MODE_PLUS},
  {PIN_MODE_NORMAL, MODE_NORMAL},
  {PIN_MODE_MOINS, MODE_MOINS},
  {PIN_MODE_VAC, MODE_VACANCES},
};

ThermioRfCc1101::Pins rfPins = {
  PIN_CC1101_CSN,
  PIN_CC1101_GDO0,
  PIN_CC1101_MOSI,
  PIN_CC1101_MISO,
  PIN_CC1101_SCK
};

const SPISettings RF_SPI_SETTINGS(1000000, MSBFIRST, SPI_MODE0);

Adafruit_NeoPixel leds(LED_COUNT, PIN_LED_CHAIN_DATA, NEO_GRB + NEO_KHZ800);
ThermioRfCc1101 radio(rfPins, RF_SPI_SETTINGS);
AssociatedSlave associatedSlaves[RF_MAX_ASSOCIATED_SLAVES] = {};

uint16_t consoleId = RF_DEFAULT_CONSOLE_ID;
uint8_t rfSequence = 0;
uint32_t lastRfRxRefreshAt = 0;
bool associationActive = false;
uint16_t associationNodeId = ThermioRfFrame::BroadcastId;
uint8_t associationDeviceType = 0;
uint8_t associationZone = 1;
uint32_t associationSaveAt = 0;
bool associationConfirmActive = false;
uint8_t associationConfirmZone = 1;
uint32_t associationConfirmUntil = 0;
bool hasLastReportSequence = false;
uint16_t lastReportSourceId = ThermioRfFrame::BroadcastId;
uint8_t lastReportSequence = 0;
uint16_t lastPacketSourceId = ThermioRfFrame::BroadcastId;
ThermioRfFrame::Report lastReport;
bool rfBlinkActive = false;
uint8_t rfBlinkStep = 0;
uint8_t rfBlinkZone = 1;
uint32_t rfBlinkColor = 0;
uint32_t nextRfBlinkAt = 0;
ModeValue stableMode = MODE_NONE;
ModeValue lastRawMode = MODE_NONE;
ModeValue lastModeBeforeNormal = MODE_NORMAL;
unsigned long modeChangedAt = 0;
uint32_t enteredNormalAt = 0;
uint32_t doucheUntil = 0;
bool doucheWasActive = false;
int8_t plusMinusOffsetC = 0;
ZoneState zones[PILOTE_ZONE_COUNT] = {};
char piloteLine[96];
uint8_t piloteLineLen = 0;
uint8_t centerColorIndex = 0;

uint32_t rgb(uint8_t red, uint8_t green, uint8_t blue) {
  return leds.Color(red, green, blue);
}

void setPixel(uint8_t index, uint32_t color) {
  leds.setPixelColor(index, color);
}

void clearAllLeds() {
  for (uint8_t i = 0; i < LED_COUNT; i++) {
    setPixel(i, rgb(0, 0, 0));
  }
}

uint32_t centerTestColor() {
  switch (centerColorIndex % 6) {
    case 0:
      return rgb(255, 0, 0);
    case 1:
      return rgb(255, 110, 0);
    case 2:
      return rgb(255, 255, 0);
    case 3:
      return rgb(0, 255, 0);
    case 4:
      return rgb(0, 80, 255);
    default:
      return rgb(140, 0, 255);
  }
}

uint8_t ledForZone(uint8_t zone) {
  return zone == RF_OUTSIDE_ZONE ? LED_CENTRE : zone - 1;
}

uint8_t workloadForZone(uint8_t zone) {
  if (zone < 1 || zone > PILOTE_ZONE_COUNT) {
    return 0;
  }
  return zones[zone - 1].doorOpen ? 0 : zones[zone - 1].workload;
}

bool doorOpenForZone(uint8_t zone) {
  if (zone < 1 || zone > PILOTE_ZONE_COUNT) {
    return false;
  }
  return zones[zone - 1].doorOpen;
}

void updateHeatingHistory(uint32_t now) {
  for (uint8_t i = 0; i < PILOTE_ZONE_COUNT; i++) {
    if (workloadForZone(i + 1) > 0) {
      zones[i].lastHeatAt = now;
      zones[i].heatSeen = true;
    }
  }
}

bool heatSeenWithin(uint8_t zone, uint32_t now, uint32_t windowMs) {
  if (zone < 1 || zone > PILOTE_ZONE_COUNT) {
    return false;
  }
  const ZoneState &state = zones[zone - 1];
  return state.heatSeen && (uint32_t)(now - state.lastHeatAt) <= windowMs;
}

bool doucheActive() {
  return stableMode == MODE_DOUCHE && (int32_t)(millis() - doucheUntil) < 0;
}

int16_t measuredTempForZone(uint8_t zone) {
  if (zone < 1 || zone > PILOTE_ZONE_COUNT) {
    return FALLBACK_MEASURED_TEMP_DECI_C;
  }
  const ZoneState &state = zones[zone - 1];
  return state.hasTemperature ? state.measuredTempDeciC : FALLBACK_MEASURED_TEMP_DECI_C;
}

uint16_t installedPowerForZone(uint8_t zone) {
  if (zone < 1 || zone > PILOTE_ZONE_COUNT) {
    return FALLBACK_ZONE_POWER_VA;
  }
  return zones[zone - 1].powerVa > 0 ? zones[zone - 1].powerVa : FALLBACK_ZONE_POWER_VA;
}

uint8_t responseModeValue() {
  switch (stableMode) {
    case MODE_PLUS:
      return RESPONSE_MODE_PLUS;
    case MODE_MOINS:
      return RESPONSE_MODE_MOINS;
    case MODE_DOUCHE:
      return RESPONSE_MODE_DOUCHE;
    case MODE_STOP:
      return RESPONSE_MODE_STOP;
    case MODE_VACANCES:
      return RESPONSE_MODE_VACANCE;
    case MODE_NORMAL:
    default:
      return RESPONSE_MODE_NORMAL;
  }
}

int16_t usualSetpointForZone(uint8_t zone) {
  return (zone >= 1 && zone <= PILOTE_ZONE_COUNT) ? SETPOINT_NORMAL_DECI_C : 0;
}

int16_t currentSetpointForZone(uint8_t zone) {
  if (zone < 1 || zone > PILOTE_ZONE_COUNT) {
    return 0;
  }
  if (stableMode == MODE_STOP) {
    return 0;
  }
  if (stableMode == MODE_VACANCES) {
    return SETPOINT_VACANCE_DECI_C;
  }
  int16_t setpoint = usualSetpointForZone(zone);
  if (stableMode == MODE_PLUS || stableMode == MODE_MOINS) {
    setpoint += (int16_t)plusMinusOffsetC * 10;
  } else if (doucheActive()) {
    setpoint += (zone == ZONE_SDB ? DOUCHE_SDB_DELTA_C : DOUCHE_OTHER_DELTA_C) * 10;
  }
  return setpoint;
}

uint8_t workloadFromRequestedPower(uint16_t requestedPowerVa, uint16_t installedPowerVa) {
  if (requestedPowerVa == 0) {
    return 0;
  }
  if (installedPowerVa == 0) {
    return 255;
  }
  const uint32_t scaled = (uint32_t)requestedPowerVa * 255UL + installedPowerVa / 2;
  return (uint8_t)min<uint32_t>(255UL, scaled / installedPowerVa);
}

uint8_t computeRegulatedWorkloadForZone(uint8_t zone) {
  if (zone < 1 || zone > PILOTE_ZONE_COUNT || stableMode == MODE_STOP || doorOpenForZone(zone)) {
    return 0;
  }

  const int16_t setpoint = currentSetpointForZone(zone);
  const int16_t measured = measuredTempForZone(zone);
  if (setpoint <= 0 || measured >= setpoint - HEATING_HYSTERESIS_DECI_C) {
    return 0;
  }

  const uint16_t deltaDeciC = (uint16_t)(setpoint - measured);
  uint16_t requestedPowerVa = deltaDeciC * REQUEST_POWER_PER_DECI_C_VA;
  requestedPowerVa = max<uint16_t>(MIN_REQUEST_POWER_VA, requestedPowerVa);
  requestedPowerVa = min<uint16_t>(MAX_REQUEST_POWER_VA, requestedPowerVa);
  return workloadFromRequestedPower(requestedPowerVa, installedPowerForZone(zone));
}

void recomputeZoneWorkloads() {
  for (uint8_t i = 0; i < PILOTE_ZONE_COUNT; i++) {
    zones[i].workload = computeRegulatedWorkloadForZone(i + 1);
  }

  if (stableMode == MODE_STOP) {
    for (uint8_t i = 0; i < PILOTE_ZONE_COUNT; i++) {
      zones[i].workload = 0;
    }
    return;
  }

}

uint16_t generateConsoleId() {
  return ThermioRfIds::generate(PIN_MODE_DOUCHE, RF_DEFAULT_CONSOLE_ID);
}

void loadOrCreateConsoleId() {
  if (!ThermioRfIds::load(EEPROM_CONSOLE_ID, consoleId)) {
    consoleId = generateConsoleId();
    ThermioRfIds::save(EEPROM_CONSOLE_ID, consoleId);
  }
}

void initializeZoneStates() {
  for (uint8_t i = 0; i < PILOTE_ZONE_COUNT; i++) {
    zones[i].workload = 0;
    zones[i].powerVa = FALLBACK_ZONE_POWER_VA;
    zones[i].lastHeatAt = 0;
    zones[i].measuredTempDeciC = FALLBACK_MEASURED_TEMP_DECI_C;
    zones[i].hasTemperature = false;
    zones[i].heatSeen = false;
    zones[i].doorOpen = false;
    zones[i].sondeLowBattery = false;
    zones[i].doorLowBattery = false;
    zones[i].sondeMissing = false;
    zones[i].doorMissing = false;
  }
}

int eepromAssocAddress(uint8_t index) {
  return EEPROM_ASSOC_FIRST + index * RF_ASSOC_ENTRY_LEN;
}

void clearAssociationTableRam() {
  for (uint8_t i = 0; i < RF_MAX_ASSOCIATED_SLAVES; i++) {
    associatedSlaves[i].nodeId = ThermioRfFrame::BroadcastId;
    associatedSlaves[i].deviceType = 0;
    associatedSlaves[i].zone = 0;
  }
}

void loadAssociationTableFromEeprom() {
  clearAssociationTableRam();
  const bool magicOk =
      EEPROM.read(EEPROM_ASSOC_MAGIC_0) == EEPROM_ASSOC_MAGIC_VALUE_0 &&
      EEPROM.read(EEPROM_ASSOC_MAGIC_1) == EEPROM_ASSOC_MAGIC_VALUE_1;
  if (!magicOk) {
    return;
  }

  for (uint8_t i = 0; i < RF_MAX_ASSOCIATED_SLAVES; i++) {
    const int address = eepromAssocAddress(i);
    const uint16_t nodeId =
        (uint16_t)EEPROM.read(address) |
        ((uint16_t)EEPROM.read(address + 1) << 8);
    const uint8_t deviceType = EEPROM.read(address + 2);
    const uint8_t zone = EEPROM.read(address + 3);
    if (ThermioRfIds::isValid(nodeId, consoleId) && zone >= 1 && zone <= RF_ASSOC_ZONE_COUNT) {
      associatedSlaves[i].nodeId = nodeId;
      associatedSlaves[i].deviceType = deviceType;
      associatedSlaves[i].zone = zone;
    }
  }
}

void saveAssociationEntry(uint8_t index) {
  const int address = eepromAssocAddress(index);
  EEPROM.update(address, associatedSlaves[index].nodeId & 0xFF);
  EEPROM.update(address + 1, associatedSlaves[index].nodeId >> 8);
  EEPROM.update(address + 2, associatedSlaves[index].deviceType);
  EEPROM.update(address + 3, associatedSlaves[index].zone);
  EEPROM.update(EEPROM_ASSOC_MAGIC_0, EEPROM_ASSOC_MAGIC_VALUE_0);
  EEPROM.update(EEPROM_ASSOC_MAGIC_1, EEPROM_ASSOC_MAGIC_VALUE_1);
}

void clearAssociationEntry(uint8_t index) {
  associatedSlaves[index].nodeId = ThermioRfFrame::BroadcastId;
  associatedSlaves[index].deviceType = 0;
  associatedSlaves[index].zone = 0;
  saveAssociationEntry(index);
}

int findAssociatedSlave(uint16_t nodeId) {
  for (uint8_t i = 0; i < RF_MAX_ASSOCIATED_SLAVES; i++) {
    if (associatedSlaves[i].nodeId == nodeId) {
      return i;
    }
  }
  return -1;
}

int findAssociationSlotForZone(uint8_t zone, int preferredIndex) {
  uint8_t zoneCount = 0;
  int firstZoneIndex = -1;
  for (uint8_t i = 0; i < RF_MAX_ASSOCIATED_SLAVES; i++) {
    if (associatedSlaves[i].zone == zone) {
      zoneCount++;
      if (firstZoneIndex < 0) {
        firstZoneIndex = i;
      }
    }
  }

  if (preferredIndex >= 0 &&
      (associatedSlaves[preferredIndex].zone == zone || zoneCount < RF_ASSOC_SLAVES_PER_ZONE)) {
    return preferredIndex;
  }
  if (zoneCount >= RF_ASSOC_SLAVES_PER_ZONE && firstZoneIndex >= 0) {
    return firstZoneIndex;
  }
  for (uint8_t i = 0; i < RF_MAX_ASSOCIATED_SLAVES; i++) {
    if (associatedSlaves[i].nodeId == ThermioRfFrame::BroadcastId) {
      return i;
    }
  }
  return 0;
}

uint8_t zoneForSlave(uint16_t nodeId) {
  if (associationActive && associationNodeId == nodeId) {
    return associationZone;
  }

  const int index = findAssociatedSlave(nodeId);
  return index >= 0 ? associatedSlaves[index].zone : 1;
}

void beginOrRefreshAssociation(uint16_t nodeId, uint8_t deviceType) {
  if (nodeId == ThermioRfFrame::BroadcastId || nodeId == consoleId) {
    return;
  }

  if (!associationActive || associationNodeId != nodeId) {
    associationActive = true;
    associationNodeId = nodeId;
    associationDeviceType = deviceType;
    associationZone = 1;
  }
  associationSaveAt = millis() + RF_ASSOCIATION_SAVE_DELAY_MS;
}

void savePendingAssociationIfDue() {
  if (!associationActive || (int32_t)(millis() - associationSaveAt) < 0) {
    return;
  }

  const int preferredIndex = findAssociatedSlave(associationNodeId);
  const int index = findAssociationSlotForZone(associationZone, preferredIndex);
  associatedSlaves[index].nodeId = associationNodeId;
  associatedSlaves[index].deviceType = associationDeviceType;
  associatedSlaves[index].zone = associationZone;
  saveAssociationEntry(index);

  for (uint8_t i = 0; i < RF_MAX_ASSOCIATED_SLAVES; i++) {
    if (i != index && associatedSlaves[i].nodeId == associationNodeId) {
      clearAssociationEntry(i);
    }
  }

  associationConfirmActive = true;
  associationConfirmZone = associationZone;
  associationConfirmUntil = millis() + 5000;
  associationActive = false;
}

bool sourceAccepted(uint16_t sourceId, uint16_t targetId) {
  const bool associationWindowOpen = (uint32_t)millis() < RF_ASSOCIATION_WINDOW_MS;
  const bool sourceKnown =
      findAssociatedSlave(sourceId) >= 0 ||
      (associationActive && associationNodeId == sourceId);
  return sourceId != consoleId &&
      (sourceKnown || (targetId == ThermioRfFrame::BroadcastId && associationWindowOpen));
}

bool targetAccepted(uint16_t targetId) {
  return targetId == consoleId ||
      (targetId == ThermioRfFrame::BroadcastId && (uint32_t)millis() < RF_ASSOCIATION_WINDOW_MS);
}

bool readReport(uint8_t &sequence) {
  if (radio.rxOverflow()) {
    radio.flushRx();
    radio.strobeRx();
    return false;
  }

  const uint8_t expectedLength = ThermioRfFrame::HeaderLen + ThermioRfFrame::ReportPayloadLen;
  if (radio.rxBytes() < expectedLength + 1) {
    return false;
  }

  uint8_t packet[ThermioRfFrame::MaxPacketLen] = {0};
  const uint8_t length = radio.readPacket(packet, sizeof(packet));
  ThermioRfFrame::Header header;
  if (!ThermioRfFrame::readHeader(packet, length, header) ||
      header.frameType != ThermioRfFrame::FrameReport ||
      !sourceAccepted(header.sourceId, header.targetId) ||
      !targetAccepted(header.targetId)) {
    return false;
  }

  ThermioRfFrame::Report report;
  if (!ThermioRfFrame::decodeReport(packet, length, report, RF_ASSOC_ZONE_COUNT)) {
    return false;
  }

  lastPacketSourceId = header.sourceId;
  lastReport = report;
  sequence = header.sequence;
  return true;
}

uint8_t buildResponsePacket(uint8_t *packet, uint16_t targetId, uint8_t sequence, uint8_t ackSequence) {
  ThermioRfFrame::Header header;
  header.frameType = ThermioRfFrame::FrameResponse;
  header.sourceId = consoleId;
  header.targetId = targetId;
  header.sequence = sequence;
  header.ackSequence = ackSequence;
  header.payloadLen = ThermioRfFrame::ResponsePayloadLen;
  ThermioRfFrame::writeHeader(packet, header);

  ThermioRfFrame::Response response;
  response.assignedZone = zoneForSlave(targetId);
  response.dateTime[0] = 26;
  response.dateTime[1] = 8;
  response.dateTime[2] = 23;
  response.dateTime[3] = 12;
  response.dateTime[4] = 0;
  response.dateTime[5] = 0;
  response.globalMode = responseModeValue();
  const uint32_t now = millis();
  response.heatActive = heatSeenWithin(response.assignedZone, now, HEAT_LAST_DAY_MS);
  response.zoneDoorOpen = doorOpenForZone(response.assignedZone);
  response.outsideTempDeciC = 120;
  response.usualSetpointDeciC = usualSetpointForZone(response.assignedZone);
  response.currentSetpointDeciC = currentSetpointForZone(response.assignedZone);
  response.commandFlags = heatSeenWithin(response.assignedZone, now, HEAT_LAST_HOUR_MS) ?
      ThermioRfFrame::ResponseFlagHeatLastHour : 0;
  response.nextReportDelayS = 3600;
  ThermioRfFrame::encodeResponsePayload(packet + ThermioRfFrame::HeaderLen, response);
  return ThermioRfFrame::HeaderLen + ThermioRfFrame::ResponsePayloadLen;
}

void sendAckBurst(uint16_t targetId, uint8_t ackSequence) {
  delay(RF_ACK_REPLY_DELAY_MS);
  for (uint8_t ack = 0; ack < RF_ACK_TX_COUNT; ack++) {
    setPixel(LED_SDB, rgb(255, 110, 0));
    leds.show();
    uint8_t packet[ThermioRfFrame::MaxPacketLen] = {0};
    const uint8_t length = buildResponsePacket(packet, targetId, rfSequence++, ackSequence);
    radio.writePacket(packet, length);
    radio.waitTxComplete(RF_TX_COMPLETE_TIMEOUT_MS);
    if (ack + 1 < RF_ACK_TX_COUNT) {
      delay(RF_ACK_TX_GAP_MS);
    }
  }
  setPixel(LED_SDB, rgb(0, 255, 0));
  leds.show();
}

void startRfReceivedBlink(uint8_t zone, uint8_t deviceType) {
  rfBlinkActive = true;
  rfBlinkStep = 0;
  rfBlinkZone = zone;
  rfBlinkColor = deviceType == ThermioRfFrame::DeviceDoor ? rgb(140, 0, 255) : rgb(255, 0, 0);
  nextRfBlinkAt = 0;
}

void updateRfReceivedBlink() {
  if (!rfBlinkActive || (int32_t)(millis() - nextRfBlinkAt) < 0) {
    return;
  }
  if (rfBlinkStep >= 6) {
    rfBlinkActive = false;
    clearAllLeds();
    return;
  }

  const bool ledOn = (rfBlinkStep % 2) == 0;
  clearAllLeds();
  setPixel(ledForZone(rfBlinkZone), ledOn ? rfBlinkColor : rgb(0, 0, 0));
  rfBlinkStep++;
  nextRfBlinkAt = millis() + 100;
}

void updateAssociationLeds() {
  if (associationConfirmActive) {
    if ((int32_t)(millis() - associationConfirmUntil) >= 0) {
      associationConfirmActive = false;
      clearAllLeds();
      return;
    }

    clearAllLeds();
    setPixel(ledForZone(associationConfirmZone), rgb(255, 0, 120));
    return;
  }

  if (!associationActive) {
    return;
  }

  const bool blinkOn = (millis() % 500) < 250;
  for (uint8_t zone = 1; zone <= RF_ASSOC_ZONE_COUNT; zone++) {
    setPixel(ledForZone(zone), zone == associationZone && blinkOn ? rgb(255, 0, 120) : rgb(0, 0, 0));
  }
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
  return activeCount > 1 ? MODE_INVALID : activeMode;
}

void initializeModeSelection() {
  const ModeValue rawMode = readRawMode();
  stableMode = (rawMode != MODE_NONE && rawMode != MODE_INVALID) ? rawMode : MODE_NORMAL;
  lastRawMode = stableMode;
  lastModeBeforeNormal = MODE_NORMAL;
  modeChangedAt = millis();
  enteredNormalAt = millis();
  plusMinusOffsetC = stableMode == MODE_MOINS ? -MODE_DELTA_STEP_C :
      stableMode == MODE_PLUS ? MODE_DELTA_STEP_C : 0;
  if (stableMode == MODE_DOUCHE) {
    doucheUntil = millis() + DOUCHE_DURATION_MS;
  }
  recomputeZoneWorkloads();
  doucheWasActive = doucheActive();
}

uint32_t colorForMode(ModeValue mode) {
  switch (mode) {
    case MODE_NORMAL:
      return rgb(0, 255, 0);
    case MODE_MOINS:
      return rgb(0, 80, 255);
    case MODE_PLUS:
      return rgb(255, 110, 0);
    case MODE_VACANCES:
      return (millis() % 10000UL) < 1000UL ? rgb(0, 80, 255) : rgb(0, 0, 0);
    case MODE_STOP:
      return rgb(0, 0, 0);
    case MODE_DOUCHE:
      return (millis() % 500UL) < 250UL ? rgb(255, 110, 0) : rgb(0, 255, 0);
    case MODE_INVALID:
    case MODE_NONE:
    default:
      return rgb(255, 255, 255);
  }
}

void sendPiloteSet() {
  Serial.print(F("SET "));
  Serial.print(PILOTE_DEFAULT_CYCLE_MINUTES);
  for (uint8_t i = 0; i < PILOTE_ZONE_COUNT; i++) {
    Serial.print(' ');
    Serial.print(workloadForZone(i + 1));
  }
  Serial.println();
}

void applyModeSelection(ModeValue mode) {
  const ModeValue previousMode = stableMode;
  stableMode = mode;

  if (mode == MODE_NORMAL) {
    lastModeBeforeNormal = previousMode;
    enteredNormalAt = millis();
    plusMinusOffsetC = 0;
  } else if (mode == MODE_PLUS || mode == MODE_MOINS) {
    const int8_t direction = mode == MODE_PLUS ? MODE_DELTA_STEP_C : -MODE_DELTA_STEP_C;
    const bool additiveReturn =
        previousMode == MODE_NORMAL &&
        lastModeBeforeNormal == mode &&
        (uint32_t)(millis() - enteredNormalAt) <= PLUS_MINUS_NORMAL_RETURN_MS;
    if (additiveReturn) {
      plusMinusOffsetC += direction;
    } else {
      plusMinusOffsetC = direction;
    }
  } else {
    plusMinusOffsetC = 0;
  }

  if (mode == MODE_DOUCHE && previousMode != MODE_DOUCHE) {
    doucheUntil = millis() + DOUCHE_DURATION_MS;
  }

  recomputeZoneWorkloads();
  doucheWasActive = doucheActive();
  sendPiloteSet();
}

void refreshTimedModeEffects() {
  if (stableMode != MODE_DOUCHE) {
    doucheWasActive = false;
    return;
  }
  const bool active = doucheActive();
  if (active != doucheWasActive) {
    doucheWasActive = active;
    recomputeZoneWorkloads();
    sendPiloteSet();
  }
}

void updateModeInput() {
  const ModeValue rawMode = readRawMode();
  const unsigned long now = millis();
  if (rawMode == MODE_NONE || rawMode == MODE_INVALID) {
    return;
  }
  if (rawMode != lastRawMode) {
    lastRawMode = rawMode;
    modeChangedAt = now;
  }
  if ((uint32_t)(now - modeChangedAt) < MODE_DEBOUNCE_MS || rawMode == stableMode) {
    return;
  }

  applyModeSelection(rawMode);
}

void handlePiloteLine(char *line) {
  if (strncmp(line, "TIMESTAMP=", 10) == 0 && strcmp(line + 10, "NA") != 0) {
    centerColorIndex++;
    return;
  }

  if (line[0] == 'Z' &&
      line[1] >= '1' &&
      line[1] <= '4' &&
      strncmp(line + 2, "_PUISSANCE=", 11) == 0) {
    zones[line[1] - '1'].powerVa = (uint16_t)atoi(line + 13);
  }
}

uint32_t colorForZoneState(uint8_t zoneIndex) {
  const uint32_t phaseMs = millis() % 7000UL;
  if (zones[zoneIndex].sondeMissing) {
    return rgb(255, 0, 0);
  }
  if (zones[zoneIndex].doorMissing) {
    return rgb(140, 0, 255);
  }
  if (zones[zoneIndex].sondeLowBattery && phaseMs >= 6000UL) {
    return rgb(255, 0, 0);
  }
  if (zones[zoneIndex].doorLowBattery && phaseMs >= 6000UL) {
    return rgb(140, 0, 255);
  }
  if (zones[zoneIndex].doorOpen) {
    return rgb(0, 80, 255);
  }
  if (zones[zoneIndex].workload > 0) {
    return rgb(255, 180, 0);
  }
  return rgb(0, 0, 0);
}

void renderHeatingStateLeds() {
  for (uint8_t i = 0; i < PILOTE_ZONE_COUNT; i++) {
    setPixel(i, colorForZoneState(i));
  }
}

void readPiloteSerial() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      piloteLine[piloteLineLen] = '\0';
      handlePiloteLine(piloteLine);
      piloteLineLen = 0;
    } else if (piloteLineLen < sizeof(piloteLine) - 1) {
      piloteLine[piloteLineLen++] = c;
    } else {
      piloteLineLen = 0;
    }
  }
}

void updateZoneStateFromReport(uint16_t sourceId, const ThermioRfFrame::Report &report) {
  const uint8_t zone = zoneForSlave(sourceId);
  if (zone < 1 || zone > PILOTE_ZONE_COUNT) {
    return;
  }
  if (report.deviceType == ThermioRfFrame::DeviceDoor) {
    zones[zone - 1].doorOpen = report.doorOpen;
    recomputeZoneWorkloads();
    sendPiloteSet();
  } else if (report.deviceType == ThermioRfFrame::DeviceSonde && report.tempCount > 0) {
    zones[zone - 1].measuredTempDeciC = report.temperaturesDeciC[report.tempCount - 1];
    zones[zone - 1].hasTemperature = true;
    recomputeZoneWorkloads();
    sendPiloteSet();
  }
}

void updateRf() {
  if ((uint32_t)(millis() - lastRfRxRefreshAt) >= RF_RX_REFRESH_INTERVAL_MS) {
    lastRfRxRefreshAt = millis();
    radio.strobeRx();
  }

  uint8_t reportSequence = 0;
  if (!readReport(reportSequence)) {
    return;
  }

  const bool duplicate =
      hasLastReportSequence &&
      reportSequence == lastReportSequence &&
      lastPacketSourceId == lastReportSourceId;

  if (!duplicate) {
    hasLastReportSequence = true;
    lastReportSequence = reportSequence;
    lastReportSourceId = lastPacketSourceId;

    if (findAssociatedSlave(lastPacketSourceId) < 0 &&
        (uint32_t)millis() < RF_ASSOCIATION_WINDOW_MS) {
      beginOrRefreshAssociation(lastPacketSourceId, lastReport.deviceType);
    }
    if (!associationActive &&
        lastReport.adminRequest == ThermioRfFrame::AdminPair &&
        (uint32_t)millis() < RF_ASSOCIATION_WINDOW_MS) {
      beginOrRefreshAssociation(lastPacketSourceId, lastReport.deviceType);
    }
    if (associationActive &&
        associationNodeId == lastPacketSourceId &&
        lastReport.adminRequest == ThermioRfFrame::AdminPair) {
      if (lastReport.pairZoneRequest >= 1 && lastReport.pairZoneRequest <= RF_ASSOC_ZONE_COUNT) {
        associationZone = lastReport.pairZoneRequest;
        associationSaveAt = millis() + RF_ASSOCIATION_SAVE_DELAY_MS;
      }
    }
    updateZoneStateFromReport(lastPacketSourceId, lastReport);
  }

  sendAckBurst(lastPacketSourceId, reportSequence);
  if (!duplicate) {
    startRfReceivedBlink(zoneForSlave(lastPacketSourceId), lastReport.deviceType);
  }
  radio.strobeRx();
}

void setup() {
  for (const ModeInput &input : modeInputs) {
    pinMode(input.pin, INPUT);
  }

  loadOrCreateConsoleId();
  loadAssociationTableFromEeprom();
  initializeZoneStates();
  Serial.begin(PILOTE_SERIAL_BAUD);
  initializeModeSelection();

  leds.setBrightness(3);
  leds.begin();
  leds.clear();
  setPixel(LED_MODE, rgb(255, 255, 255));
  leds.show();

  radio.beginPins();
  SPI.begin();
  radio.wake();
  const bool rfOk = radio.testSpi();
  setPixel(LED_SDB, rfOk ? rgb(0, 255, 0) : rgb(255, 0, 0));
  SPI.beginTransaction(RF_SPI_SETTINGS);
  radio.configureTestRadio(ThermioRfFrame::MaxPacketLen);
  radio.strobeRx();
  lastRfRxRefreshAt = millis();
  sendPiloteSet();
  leds.show();
}

void loop() {
  readPiloteSerial();
  updateHeatingHistory(millis());
  updateRf();
  savePendingAssociationIfDue();

  refreshTimedModeEffects();
  updateModeInput();
  renderHeatingStateLeds();
  setPixel(LED_MODE, colorForMode(stableMode));
  setPixel(LED_CENTRE, centerTestColor());
  updateRfReceivedBlink();
  updateAssociationLeds();
  leds.show();
  delay(10);
}
