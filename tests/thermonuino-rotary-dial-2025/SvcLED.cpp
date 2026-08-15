#include "SvcLED.h"
#include "SvcClock.h" 

static Adafruit_NeoPixel led(1, PIN_WS2812, NEO_GRB + NEO_KHZ800);
static uint8_t bright(uint8_t h) {
  return (h >= 7 && h < 18) ? 128 : 24;
}
static void setRGB(uint8_t r, uint8_t g, uint8_t b) {
  led.setPixelColor(0, led.Color(r, g, b));
  led.show();
}


void SvcLED_init() {
  led.begin();
  led.setBrightness(32);
  setRGB(252, 94, 160);
}
void SvcLED_poll() {
  static uint32_t last = 0;
  if (millis() - last < 50) return;
  last = millis();
  uint8_t hour = SvcClock_hourLocal(/* tzOffsetHours = */ 0);
  led.setBrightness(bright(hour));

  // Log une seule fois à chaque bascule jour/nuit
  static bool day = false;
  bool nowDay = (hour >= LED_DAY_START && hour < LED_DAY_END);
  if (nowDay != day) {
    info(String("[LED] ") + (nowDay ? "day" : "night"));
    day = nowDay;
  }

  bool strong = false, err = false;
  for (uint8_t z = 0; z < 4; z++) {
    if (g_ctl[z].duty > 200) strong = true;
    uint8_t s = g_zone_sensor[z];
    if (s < 3) { /* erreur RF ? */
    }
  }
  static bool blink = false;
  blink = !blink;
  if (err) {
    if (blink) setRGB(255, 0, 128);
    else setRGB(0, 0, 0);
    return;
  }
  if (strong) {
    setRGB(255, 96, 0);
    return;
  }
  setRGB(0, 160, 0);
}