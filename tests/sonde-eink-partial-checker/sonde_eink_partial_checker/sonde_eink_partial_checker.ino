/*
  Diagnostic eInk 0.97" - jauge segmentee.

  SENS1 ajoute un segment, SENS2 retire un segment.
  Le bouton central force un refresh complet de resynchronisation.
*/

#include <SPI.h>

#include "/Users/leyry/Documents/Electronique/Thermonuino/main/modules/sonde/graphics/ThermioEink097.h"

constexpr uint8_t PIN_LED = 5;
constexpr uint8_t PIN_RF_CSN = 10;
constexpr uint8_t PIN_EPD_BUSY = A2;
constexpr uint8_t PIN_EPD_RST = 4;
constexpr uint8_t PIN_EPD_DC = 3;
constexpr uint8_t PIN_EPD_CS = 6;
constexpr uint8_t PIN_CMD_PLUS = 0;
constexpr uint8_t PIN_CMD_MINUS = A1;
constexpr uint8_t PIN_CMD_BTN = A3;

constexpr uint8_t GAUGE_X = 8;
constexpr uint8_t GAUGE_Y = 32;
constexpr uint8_t GAUGE_H = 24;
constexpr uint8_t SEGMENT_W = 10;
constexpr uint8_t SEGMENT_GAP = 3;
constexpr uint8_t SEGMENT_PITCH = SEGMENT_W + SEGMENT_GAP;
constexpr uint8_t SEGMENT_COUNT = 13;
constexpr uint8_t GAUGE_W = SEGMENT_COUNT * SEGMENT_PITCH - SEGMENT_GAP;
constexpr uint8_t REFRESH_X = 0;
constexpr uint8_t REFRESH_Y = GAUGE_Y;
constexpr uint8_t REFRESH_W = ThermioEink097::FrontWidth;
constexpr uint8_t REFRESH_H = GAUGE_H;
constexpr uint16_t DEBOUNCE_MS = 35;

const ThermioEink097::Pins einkPins = {
  PIN_EPD_CS,
  PIN_EPD_DC,
  PIN_EPD_RST,
  PIN_EPD_BUSY,
  PIN_RF_CSN
};

ThermioEink097 eink(einkPins);

uint8_t filledSegments = 0;
uint8_t displayedSegments = 0;

struct ButtonState {
  uint8_t pin;
  bool stable;
  bool raw;
  uint32_t changedAt;
};

ButtonState plusButton = {PIN_CMD_PLUS, HIGH, HIGH, 0};
ButtonState minusButton = {PIN_CMD_MINUS, HIGH, HIGH, 0};
ButtonState centerButton = {PIN_CMD_BTN, HIGH, HIGH, 0};

void ledOn() {
  digitalWrite(PIN_LED, HIGH);
}

void ledOff() {
  digitalWrite(PIN_LED, LOW);
}

bool gaugePixel(uint16_t x, uint16_t y, void *context) {
  const uint8_t segments = context ? *((uint8_t *)context) : filledSegments;

  if (x == 0 || y == 0 ||
      x == ThermioEink097::FrontWidth - 1 ||
      y == ThermioEink097::FrontHeight - 1) {
    return true;
  }

  if (x < GAUGE_X || x >= GAUGE_X + GAUGE_W ||
      y < GAUGE_Y || y >= GAUGE_Y + GAUGE_H) {
    return false;
  }

  const uint8_t localX = x - GAUGE_X;
  const uint8_t localY = y - GAUGE_Y;
  const uint8_t segment = localX / SEGMENT_PITCH;
  const uint8_t segmentX = localX % SEGMENT_PITCH;

  if (segment >= SEGMENT_COUNT || segmentX >= SEGMENT_W) {
    return false;
  }

  if (localY == 0 || localY == GAUGE_H - 1 ||
      segmentX == 0 || segmentX == SEGMENT_W - 1) {
    return true;
  }

  return segment < segments;
}

void fullRefresh() {
  ledOn();
  digitalWrite(PIN_RF_CSN, HIGH);
  const bool ok = eink.init() && eink.writeFrontImage(gaugePixel, nullptr);
  eink.sleep();
  if (ok) {
    displayedSegments = filledSegments;
    ledOff();
  }
}

void partialRefreshGauge() {
  ledOn();
  digitalWrite(PIN_RF_CSN, HIGH);
  const uint8_t oldSegments = displayedSegments;
  const uint8_t newSegments = filledSegments;
  const bool ok = eink.wakeForPartialUpdate() &&
      eink.writeFrontImagePartial(gaugePixel,
                                  (void *)&oldSegments,
                                  gaugePixel,
                                  (void *)&newSegments,
                                  REFRESH_X,
                                  REFRESH_Y,
                                  REFRESH_W,
                                  REFRESH_H);
  eink.sleep();
  if (ok) {
    displayedSegments = filledSegments;
    ledOff();
  }
}

bool pressed(ButtonState &button) {
  const uint32_t now = millis();
  const bool raw = digitalRead(button.pin);

  if (raw != button.raw) {
    button.raw = raw;
    button.changedAt = now;
  }

  if ((uint32_t)(now - button.changedAt) < DEBOUNCE_MS) {
    return false;
  }

  if (raw != button.stable) {
    button.stable = raw;
    return raw == LOW;
  }

  return false;
}

void setup() {
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_RF_CSN, OUTPUT);
  pinMode(PIN_CMD_PLUS, INPUT_PULLUP);
  pinMode(PIN_CMD_MINUS, INPUT_PULLUP);
  pinMode(PIN_CMD_BTN, INPUT_PULLUP);

  ledOff();
  digitalWrite(PIN_RF_CSN, HIGH);
  SPI.begin();
  eink.begin();
  fullRefresh();
}

void loop() {
  if (pressed(plusButton)) {
    if (filledSegments < SEGMENT_COUNT) {
      filledSegments++;
    }
    partialRefreshGauge();
  }

  if (pressed(minusButton)) {
    if (filledSegments > 0) {
      filledSegments--;
    }
    partialRefreshGauge();
  }

  if (pressed(centerButton)) {
    fullRefresh();
  }
}
