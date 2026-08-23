/*
  Thermonuino - module porte ouverte

  ATmega328P 3.3 V / 8 MHz.
  PCB porte ouverte :
    - REED sur D2, actif LOW ;
    - bouton sur D3, actif HIGH ;
    - LED sur A3, active HIGH ;
    - CC1101 sur SPI, 3.3 V permanent, mis en SPWD hors communication.
*/

#include <EEPROM.h>
#include <SPI.h>

#include <ThermioRfCc1101.h>
#include <ThermioRfFrame.h>
#include <ThermioRfIds.h>
#include <ThermioSlaveLink.h>
#include <ThermioSlavePower.h>

constexpr uint8_t PIN_RF_GDO0 = 9;
constexpr uint8_t PIN_RF_CSN = 10;
constexpr uint8_t PIN_RF_MOSI = 11;
constexpr uint8_t PIN_RF_MISO = 12;
constexpr uint8_t PIN_RF_SCK = 13;
constexpr uint8_t PIN_DOOR_OPEN = 2;
constexpr uint8_t PIN_BUTTON = 3;
constexpr uint8_t PIN_LED = A3;

constexpr uint16_t RF_DEFAULT_NODE_ID = 0x0D01;
constexpr uint8_t RF_ASSOC_ZONE_COUNT = 5;
constexpr uint32_t RF_CONSOLE_LEARN_WINDOW_MS = 180000;
constexpr uint16_t RF_BEACON_INTERVAL_WATCHDOG_TICKS = 3; // ~24 s
constexpr uint16_t RF_STARTUP_AUTO_BEACON_DELAY_WATCHDOG_TICKS = 3; // ~24 s
constexpr uint16_t RF_OFFLINE_RETRY_WATCHDOG_TICKS = 5400; // ~12 h
constexpr uint16_t RF_CHANNEL_LISTEN_MS = 30;
constexpr uint16_t RF_ACK_TIMEOUT_MS = 2000;
constexpr uint16_t RF_RX_SETTLE_MS = 50;
constexpr uint16_t RF_TX_COMPLETE_TIMEOUT_MS = 350;
constexpr uint8_t RF_MAX_ATTEMPTS = 6;
constexpr uint8_t RF_TX_COPIES_PER_ATTEMPT = 3;
constexpr uint16_t RF_TX_COPY_GAP_MS = 60;
constexpr uint16_t RF_BACKOFF_MIN_MS = 100;
constexpr uint16_t RF_BACKOFF_SPAN_MS = 500;
constexpr uint16_t RF_BACKOFF_STEP_MS = 150;
constexpr uint16_t RF_RESULT_LED_MS = 1000;
constexpr uint16_t RF_RESULT_FAIL_ON_MS = 250;
constexpr uint16_t BUTTON_ACCEPTED_ON_MS = 40;
constexpr uint16_t PAIR_TX_BLINK_ON_MS = 70;
constexpr uint16_t PAIR_TX_BLINK_OFF_MS = 70;

constexpr ThermioRfIds::EepromSlot EEPROM_CONSOLE_ID = {
  0, 1, 2, 3, 0x54, 0x43
};

constexpr ThermioRfIds::EepromSlot EEPROM_NODE_ID = {
  4, 5, 6, 7, 0x54, 0x4E
};

ThermioRfCc1101::Pins rfPins = {
  PIN_RF_CSN,
  PIN_RF_GDO0,
  PIN_RF_MOSI,
  PIN_RF_MISO,
  PIN_RF_SCK
};

ThermioRfCc1101 radio(rfPins, SPISettings(1000000, MSBFIRST, SPI_MODE0));
ThermioSlaveLink link(RF_ASSOC_ZONE_COUNT);

uint32_t awakeWatchdogTicks = 0;
uint32_t autoBeaconEnabledAtWatchdogTick = 0;
uint8_t rfSequence = 0;
bool lastButtonPressed = false;
bool pendingPairRequest = false;
bool activePairRequest = false;
bool doorOpenState = false;
bool lastDoorOpenState = false;
uint8_t doorToggleCountSinceAck = 0;
bool rfResultActive = false;
bool rfResultAckReceived = false;
bool rfResultLedOn = false;
uint32_t rfResultUntil = 0;
uint32_t nextRfResultToggleAt = 0;

void ledOn() {
  digitalWrite(PIN_LED, HIGH);
}

void ledOff() {
  digitalWrite(PIN_LED, LOW);
}

void blinkLed(uint8_t count, uint16_t onMs, uint16_t offMs) {
  for (uint8_t i = 0; i < count; i++) {
    ledOn();
    delay(onMs);
    ledOff();
    delay(offMs);
  }
}

void clearLocalEeprom() {
  for (int address = 0; address < EEPROM.length(); address++) {
    EEPROM.update(address, 0xFF);
  }
  link.clearConsoleId();
}

void waitForNodeIdCreation() {
  while (digitalRead(PIN_BUTTON) == HIGH) {
    blinkLed(1, 80, 220);
  }

  while (digitalRead(PIN_BUTTON) != HIGH) {
    blinkLed(1, 40, 460);
  }

  const uint16_t nodeId = ThermioRfIds::generate(A0, RF_DEFAULT_NODE_ID);
  ThermioRfIds::save(EEPROM_NODE_ID, nodeId);
  link.setLocalId(nodeId);
  blinkLed(3, 120, 120);
  while (digitalRead(PIN_BUTTON) == HIGH) {
    delay(10);
  }
}

bool readDoorOpenState() {
  return digitalRead(PIN_DOOR_OPEN) != LOW;
}

void updateDoorState() {
  doorOpenState = readDoorOpenState();
  if (doorOpenState != lastDoorOpenState) {
    lastDoorOpenState = doorOpenState;
    if (doorToggleCountSinceAck < 255) {
      doorToggleCountSinceAck++;
    }
  }
}

uint16_t readBatteryMv() {
  return 3000;
}

void captureButtonRequest() {
  const bool buttonPressed = digitalRead(PIN_BUTTON) == HIGH;
  if (buttonPressed && !lastButtonPressed) {
    link.nextPairZone();
    link.forceRetryByUser();
    pendingPairRequest = true;
    blinkLed(1, BUTTON_ACCEPTED_ON_MS, 0);
  }
  lastButtonPressed = buttonPressed;
}

uint8_t buildReportPacket(uint8_t *packet, uint8_t sequence) {
  ThermioRfFrame::Header header;
  header.frameType = ThermioRfFrame::FrameReport;
  header.sourceId = link.localId();
  header.targetId = link.reportTargetId();
  header.sequence = sequence;
  header.ackSequence = 0xFF;
  header.payloadLen = ThermioRfFrame::ReportPayloadLen;
  ThermioRfFrame::writeHeader(packet, header);

  ThermioRfFrame::Report report;
  report.deviceType = ThermioRfFrame::DeviceDoor;
  report.batteryMv = readBatteryMv();
  report.pairZoneRequest = activePairRequest ? link.pairZoneRequest() : 0;
  report.adminRequest = activePairRequest ? ThermioRfFrame::AdminPair : ThermioRfFrame::AdminNone;
  report.tempCount = 0;
  report.presenceCount = 0;
  report.doorToggleCount = doorToggleCountSinceAck;
  report.doorOpen = doorOpenState;
  ThermioRfFrame::encodeReportPayload(packet + ThermioRfFrame::HeaderLen, report);
  return ThermioRfFrame::HeaderLen + ThermioRfFrame::ReportPayloadLen;
}

bool readAck(uint8_t expectedSequence) {
  if (radio.rxOverflow()) {
    radio.flushRx();
    radio.strobeRx();
    return false;
  }

  const uint8_t expectedLength = ThermioRfFrame::HeaderLen + ThermioRfFrame::ResponsePayloadLen;
  if (radio.rxBytes() < expectedLength + 1) {
    return false;
  }

  uint8_t packet[ThermioRfFrame::MaxPacketLen] = {0};
  const uint8_t length = radio.readPacket(packet, sizeof(packet));
  ThermioRfFrame::Header header;
  if (!ThermioRfFrame::readHeader(packet, length, header) ||
      header.frameType != ThermioRfFrame::FrameResponse ||
      header.targetId != link.localId() ||
      header.ackSequence != expectedSequence ||
      header.sourceId == link.localId()) {
    return false;
  }

  if (link.consoleIdKnown() && header.sourceId != link.consoleId()) {
    return false;
  }

  ThermioRfFrame::Response response;
  if (!ThermioRfFrame::decodeResponse(packet, length, response, RF_ASSOC_ZONE_COUNT)) {
    return false;
  }

  if (link.learnConsoleIdFromResponse(header.sourceId, millis(), RF_CONSOLE_LEARN_WINDOW_MS)) {
    ThermioRfIds::save(EEPROM_CONSOLE_ID, header.sourceId);
  }

  link.markAckReceived(response.assignedZone);
  doorToggleCountSinceAck = 0;
  return true;
}

void startRfResultIndicator(bool ackReceived) {
  rfResultActive = true;
  rfResultAckReceived = ackReceived;
  rfResultLedOn = ackReceived;
  rfResultUntil = millis() + RF_RESULT_LED_MS;
  nextRfResultToggleAt = millis();
  digitalWrite(PIN_LED, ackReceived ? HIGH : LOW);
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

  if (!rfResultLedOn && (int32_t)(millis() - nextRfResultToggleAt) >= 0) {
    rfResultLedOn = true;
    ledOn();
    nextRfResultToggleAt = millis() + RF_RESULT_FAIL_ON_MS;
  } else if (rfResultLedOn && (int32_t)(millis() - nextRfResultToggleAt) >= 0) {
    rfResultLedOn = false;
    ledOff();
  }
  return true;
}

void runRfExchange() {
  activePairRequest = pendingPairRequest;
  pendingPairRequest = false;
  if (activePairRequest) {
    blinkLed(3, PAIR_TX_BLINK_ON_MS, PAIR_TX_BLINK_OFF_MS);
  }

  radio.wake();
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  radio.configureTestRadio(ThermioRfFrame::MaxPacketLen);

  bool received = false;
  const uint8_t sequence = rfSequence++;
  for (uint8_t attempt = 0; attempt < RF_MAX_ATTEMPTS && !received; attempt++) {
    if (radio.channelBusy(RF_CHANNEL_LISTEN_MS)) {
      delay(random(RF_BACKOFF_MIN_MS, RF_BACKOFF_MIN_MS + RF_BACKOFF_SPAN_MS + 1) + attempt * RF_BACKOFF_STEP_MS);
    }

    for (uint8_t copy = 0; copy < RF_TX_COPIES_PER_ATTEMPT; copy++) {
      uint8_t packet[ThermioRfFrame::MaxPacketLen] = {0};
      const uint8_t length = buildReportPacket(packet, sequence);
      radio.writePacket(packet, length);
      radio.waitTxComplete(RF_TX_COMPLETE_TIMEOUT_MS);
      if (copy + 1 < RF_TX_COPIES_PER_ATTEMPT) {
        delay(RF_TX_COPY_GAP_MS);
      }
    }

    radio.strobeRx();
    delay(RF_RX_SETTLE_MS);
    const uint32_t rxStartedAt = millis();
    while ((uint32_t)(millis() - rxStartedAt) < RF_ACK_TIMEOUT_MS) {
      if (readAck(sequence)) {
        received = true;
        break;
      }
      delay(10);
    }

    if (!received) {
      delay(random(RF_BACKOFF_MIN_MS, RF_BACKOFF_MIN_MS + RF_BACKOFF_SPAN_MS + 1) + attempt * RF_BACKOFF_STEP_MS);
    }
  }

  radio.idle();
  SPI.endTransaction();
  radio.sleep();
  if (!received) {
    link.markAckMissed(awakeWatchdogTicks);
  }
  activePairRequest = false;
  startRfResultIndicator(received);
}

void updateInputLed() {
  if (updateRfResultIndicator()) {
    return;
  }

  const bool reedClosed = digitalRead(PIN_DOOR_OPEN) == LOW;
  const bool buttonPressed = digitalRead(PIN_BUTTON) == HIGH;
  digitalWrite(PIN_LED, reedClosed || buttonPressed ? HIGH : LOW);
}

void setup() {
  pinMode(PIN_LED, OUTPUT);
  ledOff();
  pinMode(PIN_DOOR_OPEN, INPUT_PULLUP);
  pinMode(PIN_BUTTON, INPUT);

  if (digitalRead(PIN_BUTTON) == HIGH) {
    clearLocalEeprom();
  }

  uint16_t nodeId = RF_DEFAULT_NODE_ID;
  if (!ThermioRfIds::load(EEPROM_NODE_ID, nodeId)) {
    waitForNodeIdCreation();
  } else {
    link.setLocalId(nodeId);
  }

  uint16_t consoleId = ThermioRfFrame::BroadcastId;
  if (ThermioRfIds::load(EEPROM_CONSOLE_ID, consoleId, link.localId())) {
    link.setConsoleId(consoleId);
  }

  doorOpenState = readDoorOpenState();
  lastDoorOpenState = doorOpenState;

  radio.beginPins();
  SPI.begin();
  radio.sleep();

  blinkLed(1, 80, 150);
  radio.wake();
  const bool rfOk = radio.testSpi();
  radio.sleep();
  blinkLed(rfOk ? 5 : 2, rfOk ? 100 : 350, rfOk ? 120 : 350);

  randomSeed(analogRead(A0) ^ micros());
  ThermioSlavePower::setupPortDPinChange(_BV(PCINT18) | _BV(PCINT19));
  ThermioSlavePower::setupWatchdog8s();
  autoBeaconEnabledAtWatchdogTick =
      awakeWatchdogTicks + RF_STARTUP_AUTO_BEACON_DELAY_WATCHDOG_TICKS;
}

void loop() {
  awakeWatchdogTicks += ThermioSlavePower::consumeWatchdogTicks();
  ThermioSlavePower::consumePinWake();

  updateDoorState();
  captureButtonRequest();

  const bool autoBeaconDue = link.autoReportDue(
      awakeWatchdogTicks,
      autoBeaconEnabledAtWatchdogTick,
      RF_BEACON_INTERVAL_WATCHDOG_TICKS);
  if (pendingPairRequest || autoBeaconDue) {
    link.markReportAttemptStarted(awakeWatchdogTicks);
    runRfExchange();
  }

  updateInputLed();
  if (!pendingPairRequest && !rfResultActive && digitalRead(PIN_BUTTON) == LOW) {
    ThermioSlavePower::sleepPowerDown();
  }
  delay(5);
}
