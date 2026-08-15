#include <SPI.h>
//EPD
#include "Display_EPD_W21_spi.h"
#include "Display_EPD_W21.h"
#include "Ap_29demo.h"  


void setup() {
#ifdef ESP8266
   pinMode(D0, INPUT);  //BUSY
   pinMode(D1, OUTPUT); //RES 
   pinMode(D2, OUTPUT); //DC   
   pinMode(D4, OUTPUT); //CS     
#endif 
#ifdef Arduino_UNO
   pinMode(7, INPUT);   // BUSY
   pinMode(8, OUTPUT);  // RES 
   pinMode(9, OUTPUT);  // DC   
   pinMode(10, OUTPUT); // CS   
   digitalWrite(10, HIGH);
#endif 
   Serial.begin(9600);
   Serial.println(F("GDEM0097T61 sample officiel adapte"));
   Serial.println(F("Pins: BUSY=D7 RES=D8 DC=D9 CS=D10 SCK=D13 SDI=D11"));
   //SPI
   SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0)); 
   SPI.begin ();  
}

//Tips//
/*
1.Flickering is normal when EPD is performing a full screen update to clear ghosting from the previous image so to ensure better clarity and legibility for the new image.
2.There will be no flicker when EPD performs a partial refresh.
3.Please make sue that EPD enters sleep mode when refresh is completed and always leave the sleep mode command. Otherwise, this may result in a reduced lifespan of EPD.
4.Please refrain from inserting EPD to the FPC socket or unplugging it when the MCU is being powered to prevent potential damage.)
5.Re-initialization is required for every full screen update.
6.When porting the program, set the BUSY pin to input mode and other pins to output mode.
*/
void loop() {
      Serial.println(F("Clear blanc"));
      EPD_HW_Init();
      EPD_WhiteScreen_White();
      EPD_DeepSleep();
      delay(2000);

      Serial.println(F("Image officielle gImage_1"));
      EPD_HW_Init();
      EPD_WhiteScreen_ALL(gImage_1);
      EPD_DeepSleep();
      Serial.println(F("Termine, ePaper en sommeil."));

      while (1);
}




//////////////////////////////////END//////////////////////////////////////////////////
