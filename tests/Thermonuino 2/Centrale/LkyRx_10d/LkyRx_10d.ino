/***********************************************************************
                        Récepteur TIC Linky
                        Mode historique

V03  : External SoftwareSerial. Tested OK on 07/03/18.
V04  : Replaced available() by new(). Tested Ok on 08/03/18.
V05  : Internal SoftwareSerial. Cf special construction syntax.
V06  : Separate compilation version.

V10a : Parametric version, initial. Tested OK on 23/10/19.
V10b : fixed bug in ptecIsNew(). Tested OK on 24/10/19.
V10c : added LKYSIMINPUT mode. Tested OK (partial) on 31/10/19.
V10d : adapted to Arduino Uno and Mega. Tested Ok on 27/04/20.

***********************************************************************
Configuration parameters are defined in LinkyHistTIC.h

***********************************************************************/

/***************************** Includes *******************************/
#include <string.h>
#include <Streaming.h>

#define LKY_Base true
#define LKYSOFTSERIAL true

#include "LinkyHistTIC.h"


/****************************** Defines *******************************/



/****************************** Constants *****************************/
const uint8_t pin_LkyRx = 8;
const uint8_t pin_LkyTx = 9;   /* !!! Not used but reserved !!! 
                                  * Do not use for anything else */

/************************* Global variables ***************************/



/************************* Object instanciation ***********************/
LinkyHistTIC Linky(pin_LkyRx, pin_LkyTx);

/****************************  Routines  ******************************/




/******************************  Setup  *******************************/
void setup()
  {

  /* Initialise serial link */
  Serial.begin(9600);

  /* Initialise the Linky receiver */
  Linky.Init();

  Serial << F("Bonjour") << endl;
  }


/******************************* Loop *********************************/
void loop()
  {
  uint8_t i;

  Linky.Update();

  if (Linky.pappIsNew())
    {
    Serial << F("Puis. app. = ") << Linky.papp() << F(" VA") << endl;
    }

  #ifdef LKY_Base
  if (Linky.baseIsNew())
    {
    Serial << F("Index base = ") << Linky.base() << F(" Wh") << endl;
    }
  #endif

  #ifdef LKY_HPHC
  if (Linky.hchpIsNew())
    {
    Serial << F("Index HP = ") << Linky.hchp() << F(" Wh") << endl;
    }
  if (Linky.hchcIsNew())
    {
    Serial << F("Index HC = ") << Linky.hchc() << F(" Wh") << endl;
    }
  if (Linky.ptecIsNew())
    {
    Serial << F("Tarif en cours : ");
    if (Linky.ptec() == Linky.C_HPleines)
      {
      Serial << F("heures pleines") << endl;
      }
      else
      {
      Serial << F("heures creuses") << endl;
      }
    }
  #endif

  #ifdef LKY_IMono
  if (Linky.iinstIsNew())
    {
    Serial << F("I instant. = ") << Linky.iinst() << F(" A") << endl;
    }
  #endif

  #ifdef LKY_ITri
  for (i = Linky.C_Phase_1; i <= Linky.C_Phase_3; i++)
    {
    if (Linky.iinstIsNew(i))
      {
      Serial << F("I Phase ") << i+1 << F(" = ") \
             << Linky.iinst(i) << F(" A") << endl;
      }
    }
  #endif

  };
