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
constexpr uint32_t RF_ASSOCIATION_SAVE_DELAY_MS = 20000;
constexpr uint32_t RF_RX_REFRESH_INTERVAL_MS = 500;
constexpr uint32_t RF_TX_COMPLETE_TIMEOUT_MS = 350;
constexpr uint32_t RF_ACK_REPLY_DELAY_MS = 300;
constexpr uint8_t RF_ACK_TX_COUNT = 3;
constexpr uint16_t RF_ACK_TX_GAP_MS = 150;

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
uint32_t nextRfBlinkAt = 0;
ModeValue stableMode = MODE_NONE;

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

uint8_t ledForZone(uint8_t zone) {
  return zone == RF_OUTSIDE_ZONE ? LED_CENTRE : zone - 1;
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
  response.globalMode = 0;
  response.heatActive = false;
  response.zoneDoorOpen = lastReport.doorOpen;
  response.outsideTempDeciC = 120;
  response.usualSetpointDeciC = 190;
  response.currentSetpointDeciC = 190;
  response.commandFlags = 0;
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

void startRfReceivedBlink(uint8_t zone) {
  rfBlinkActive = true;
  rfBlinkStep = 0;
  rfBlinkZone = zone;
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
  setPixel(ledForZone(rfBlinkZone), ledOn ? rgb(0, 80, 80) : rgb(0, 0, 0));
  rfBlinkStep++;
  nextRfBlinkAt = millis() + 120;
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

uint32_t colorForMode(ModeValue mode) {
  switch (mode) {
    case MODE_NORMAL:
      return rgb(255, 110, 0);
    case MODE_MOINS:
      return rgb(0, 80, 255);
    case MODE_PLUS:
      return rgb(255, 0, 0);
    case MODE_VACANCES:
      return rgb(140, 0, 255);
    case MODE_STOP:
      return rgb(0, 0, 0);
    case MODE_DOUCHE:
      return rgb(255, 0, 0);
    case MODE_INVALID:
    case MODE_NONE:
    default:
      return rgb(255, 255, 255);
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
  }

  sendAckBurst(lastPacketSourceId, reportSequence);
  if (!duplicate) {
    startRfReceivedBlink(zoneForSlave(lastPacketSourceId));
  }
  radio.strobeRx();
}

void setup() {
  for (const ModeInput &input : modeInputs) {
    pinMode(input.pin, INPUT);
  }

  loadOrCreateConsoleId();
  loadAssociationTableFromEeprom();

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
  leds.show();
}

void loop() {
  updateRf();
  savePendingAssociationIfDue();

  stableMode = readRawMode();
  setPixel(LED_MODE, colorForMode(stableMode));
  updateRfReceivedBlink();
  updateAssociationLeds();
  leds.show();
  delay(10);
}
