#include "SvcUI.h"
ClickEncoder* g_enc = nullptr;

uint32_t g_ui_lastMs = 0;
int16_t g_encAccum = 0;

static void isr() {
  g_enc->service();
}


void SvcUI_init() {
  //pinMode(PIN_ROT_CLK, INPUT_PULLUP);
  //pinMode(PIN_ROT_DT, INPUT_PULLUP);
  pinMode(PIN_ROT_SW, INPUT_PULLUP);
  g_enc = new ClickEncoder(PIN_ROT_CLK, PIN_ROT_DT, PIN_ROT_SW, 1, LOW);
  Timer1.initialize(1000);
  Timer1.attachInterrupt(isr);

  info("[UI] init"); 
}


static int16_t currentDisplay_Tq();  // fwd decl (impl. côté Temp/Config)

int16_t rotLast = 0;
void SvcUI_poll() {
  int16_t delta = g_enc->getValue();
  if (delta != rotLast) {
    
  	SvcRF_enable(false);
    g_ui = UiState::Adjust;
    info("[UI] state=Adjust"); 
    g_ui_lastMs = millis();
    if(delta > rotLast){
      g_encAccum += 1;
    } else {
      g_encAccum -= 1;
    }
    rotLast = delta;
    debug("[UI] g_encAccum="+String(g_encAccum));
    int16_t idx = (int16_t)g_setpoint_idx + g_encAccum;
    if (idx < 0) idx = 0;
    if (idx > 80) idx = 80;
    g_setpoint_idx = (uint8_t)idx;
    info("[SET] global="+String((g_setpoint_idx+32)/4.0,2));
  }
  auto b = g_enc->getButton();
  if (b == ClickEncoder::Clicked) {
    g_ui = UiState::Menu;
    SvcRF_enable(false);
    info("[UI] state=Menu"); 
    g_ui_lastMs = millis();
    Stepper_displayStar();
  }
  if (g_ui != UiState::Idle && millis() - g_ui_lastMs > 15000){
    g_ui = UiState::Idle;
    info("[UI] state=Idle"); 
    SvcRF_enable(true);
  } 
  c'est lui qui envoi 34 -34
  Stepper_displayTq(currentDisplay_Tq());
}

// Affiche la valeur initiale dans la fenêtre au boot.
// - Suppose que SvcI2C_init() et SvcState_init() ont déjà été appelés,
//   pour que currentDisplay_Tq() puisse fournir une mesure cohérente.
void SvcDisplay_boot() {
  // Version simple et sûre : aller directement à la valeur courante (Idle).
  // (currentDisplay_Tq() renverra la T° du capteur d’affichage,
  //  ou la consigne si on démarre en Adjust — par défaut Idle)
  Stepper_displayTq(currentDisplay_Tq());
}