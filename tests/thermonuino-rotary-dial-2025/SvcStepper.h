#pragma once
#include "Services.h"

extern RampProfile g_ramp;
extern int32_t g_mech_zero_steps;
extern int32_t g_current_steps;

void SvcStepper_init();
void Stepper_displayTq(int16_t Tq);
void Stepper_displayStar();
void Stepper_displayStop();