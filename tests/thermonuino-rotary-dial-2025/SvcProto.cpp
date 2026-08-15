#include "SvcProto.h"
void SvcProto_init() {
  Serial.begin(BAUD_ACTIONNEUR);
}
void SvcProto_poll() {
  static uint32_t last = 0;
  if (millis() - last < TX_PERIOD_MS) return;
  last = millis();
  Statut st{};
  st.datetime = SvcClock_now();
  st.consigne[0] = g_ctl[0].duty;
  st.consigne[1] = g_ctl[1].duty;
  st.consigne[2] = g_ctl[2].duty;
  st.consigne[3] = g_ctl[3].duty;
  Proto::sendStatut(Serial, st);
}