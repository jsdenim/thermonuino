/*
  Banc de test USB <-> Thermonuino pilote-tic

  A televerser sur un Arduino Uno relie en USB a l'ordinateur.

  Cablage:
    Uno D10 (RX logiciel) <- TX du pilote-tic
    Uno D11 (TX logiciel) -> RX du pilote-tic
    Uno GND              <-> GND du pilote-tic

  Moniteur serie USB: 115200 bauds, fin de ligne NL ou CRLF.

  Commandes USB:
    help
    ping
    status
    learn
    auto
    off
    on
    set 30 0 255 127 64
    seq
    raw DC_LENGTH=30
    raw Z1_WORKLOAD=127
*/

#include <SoftwareSerial.h>
#include <string.h>

const uint8_t PIN_PILOTE_RX = 10;
const uint8_t PIN_PILOTE_TX = 11;
const unsigned long USB_BAUD = 115200;
const unsigned long PILOTE_BAUD = 9600;
const unsigned long SEQ_STEP_MS = 8000UL;

SoftwareSerial pilote(PIN_PILOTE_RX, PIN_PILOTE_TX);

char usbLine[96];
uint8_t usbLineLen = 0;
bool sequenceRunning = false;
uint8_t sequenceStep = 0;
unsigned long lastSequenceStepAt = 0;

void sendPiloteLine(const char *line) {
  pilote.println(line);
  Serial.print(F(">> "));
  Serial.println(line);
}

void printHelp() {
  Serial.println(F("Commandes:"));
  Serial.println(F("  ping, status, learn, auto, off, on"));
  Serial.println(F("  set <minutes> <z1> <z2> <z3> <z4>"));
  Serial.println(F("  seq"));
  Serial.println(F("  raw <ligne envoyee telle quelle au pilote>"));
}

void handleUsbCommand(char *line) {
  if (line[0] == '\0') {
    return;
  }

  if (strcmp(line, "help") == 0 || strcmp(line, "?") == 0) {
    printHelp();
    return;
  }
  if (strcmp(line, "ping") == 0) {
    sendPiloteLine("PING");
    return;
  }
  if (strcmp(line, "status") == 0) {
    sendPiloteLine("STATUS?");
    return;
  }
  if (strcmp(line, "learn") == 0) {
    sendPiloteLine("LEARN");
    return;
  }
  if (strcmp(line, "auto") == 0) {
    sendPiloteLine("AUTO");
    return;
  }
  if (strcmp(line, "off") == 0) {
    sendPiloteLine("ALL_OFF");
    return;
  }
  if (strcmp(line, "on") == 0) {
    sendPiloteLine("ALL_ON");
    return;
  }
  if (strcmp(line, "seq") == 0) {
    sequenceRunning = true;
    sequenceStep = 0;
    lastSequenceStepAt = 0;
    Serial.println(F("Sequence automatique lancee"));
    return;
  }
  if (strncmp(line, "raw ", 4) == 0) {
    sendPiloteLine(line + 4);
    return;
  }
  if (strncmp(line, "set ", 4) == 0) {
    char command[96];
    strncpy(command, "SET ", sizeof(command));
    strncat(command, line + 4, sizeof(command) - strlen(command) - 1);
    sendPiloteLine(command);
    return;
  }

  Serial.print(F("Commande USB inconnue: "));
  Serial.println(line);
  printHelp();
}

void readUsb() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      usbLine[usbLineLen] = '\0';
      handleUsbCommand(usbLine);
      usbLineLen = 0;
    } else if (usbLineLen < sizeof(usbLine) - 1) {
      usbLine[usbLineLen++] = c;
    } else {
      usbLineLen = 0;
      Serial.println(F("Ligne USB trop longue"));
    }
  }
}

void relayPiloteToUsb() {
  while (pilote.available() > 0) {
    Serial.write(pilote.read());
  }
}

void serviceSequence() {
  if (!sequenceRunning) {
    return;
  }

  unsigned long now = millis();
  if (lastSequenceStepAt != 0 && now - lastSequenceStepAt < SEQ_STEP_MS) {
    return;
  }

  lastSequenceStepAt = now;
  switch (sequenceStep) {
    case 0:
      sendPiloteLine("PING");
      break;
    case 1:
      sendPiloteLine("SET 1 255 0 0 0");
      break;
    case 2:
      sendPiloteLine("SET 1 0 255 0 0");
      break;
    case 3:
      sendPiloteLine("SET 1 0 0 255 0");
      break;
    case 4:
      sendPiloteLine("SET 1 0 0 0 255");
      break;
    case 5:
      sendPiloteLine("SET 1 127 127 127 127");
      break;
    case 6:
      sendPiloteLine("STATUS?");
      break;
    case 7:
      sendPiloteLine("SET 30 0 0 0 0");
      break;
    default:
      sequenceRunning = false;
      Serial.println(F("Sequence automatique terminee"));
      return;
  }

  sequenceStep++;
}

void setup() {
  Serial.begin(USB_BAUD);
  pilote.begin(PILOTE_BAUD);

  Serial.println(F("Banc de test pilote-tic pret"));
  Serial.println(F("USB 115200, pilote 9600"));
  printHelp();
}

void loop() {
  readUsb();
  relayPiloteToUsb();
  serviceSequence();
}
