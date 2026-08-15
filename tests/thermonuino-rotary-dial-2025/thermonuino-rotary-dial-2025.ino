// --------------------------------------------------------------
// File: Thermonuino.ino (point d’entrée)
// --------------------------------------------------------------
#include "Services.h"

#include <SoftwareSerial.h>
#include "Logging.h"
SoftwareSerial debugSerial(12, 11);


void setup(){

  delay(3000);
  debugSerial.begin(9600);
  logBegin(debugSerial); 

  SvcPins_init();
  SvcLED_init();
  SvcI2C_init();
  SvcRF_init();
  SvcState_init();
  SvcUI_init();
  SvcStepper_init();
  SvcProto_init();
  SvcClock_init();
  SvcDisplay_boot();

  info("[BOOT "+hhmm()+"] Thermonuino v0.2 " __DATE__ " " __TIME__);
  info("[CFG] baud="+String(BAUD_ACTIONNEUR)+", steps/tick="+String(STEPS_PER_TICK));
  info("[CFG] setpoint="+String((g_setpoint_idx+32)/4.0,2));
  info("[CFG] offsets="+String(g_zone_offset_q[0]/4.0,2)+","+String(g_zone_offset_q[1]/4.0,2)+","+String(g_zone_offset_q[2]/4.0,2)+","+String(g_zone_offset_q[3]/4.0,2));
}


void loop(){
  SvcTemp_poll();
  SvcRegul_poll();
  SvcProto_poll();
  SvcLED_poll();
  SvcVacances_poll();
  SvcUI_poll();
  SvcRF_poll();
}

