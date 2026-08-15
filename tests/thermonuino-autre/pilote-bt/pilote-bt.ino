#include <math.h>
#include "Zone.cpp"



#define LED_PIN_ZONE1 6
#define LED_PIN_ZONE2 7
#define LED_PIN_ZONE3 8
#define LED_PIN_ZONE4 9
#define LED_PIN_STATUT 10

#define OPTO_PIN_ZONE1 A0
#define OPTO_PIN_ZONE2 A4
#define OPTO_PIN_ZONE3 A1
#define OPTO_PIN_ZONE4 A5

#define TELEINFO A2

#define BTN_PIN 3

#define ZONES 4
Zone zones[ZONES];



double modResult = 0;
double indexCreneauActuel = 0;

void setup() {



  Serial.begin(9600);

  

  zones[0].Setup(OPTO_PIN_ZONE1, LED_PIN_ZONE1, 1200);
  zones[0].consigne(1200, (double)0);


  zones[1].Setup(OPTO_PIN_ZONE2, LED_PIN_ZONE2, 2700);
  zones[1].consigne(2700, (double)0);

  zones[2].Setup(OPTO_PIN_ZONE3, LED_PIN_ZONE3, 700);
  zones[2].consigne(700, (double)0);

  zones[3].Setup(OPTO_PIN_ZONE4, LED_PIN_ZONE4, 1200);
  zones[3].consigne(1200, (double)0);

  StatutSetup(LED_PIN_STATUT, BTN_PIN);
  



/*
  ça, c'est OK !
  for(double i = 1; i <= nombreCreneauxUneHeure; i++){
    modResult = fmod(i, nbCrenauxIntervale);
    Serial.print(i);
    Serial.print( "  " );
    Serial.print(modResult);
    if(modResult < 1){
      Serial.println("Chauffe");
    } else {
      Serial.println("     ");
    }

  }
*/
}



void loop() {


  if(StatutLoopAndBreake()){
    return;
  }

  indexCreneauActuel = fmod((millis()/1000/DURREE_CRENEAU_EN_SECONDES),NOMBRE_CRENEAUX_EN_UNE_HEURE) + 1;

  //Serial.println(indexCreneauActuel);

  for(byte i = 0; i < ZONES ; i++){
    zones[i].activerZone(indexCreneauActuel);
  }



  if(Serial.available()){
    lireInstructionsConsole();
  }

}

void lireInstructionsConsole(){
  String instruction = Serial.readString(); 
  instruction.trim();
  if (instruction.startsWith("Z")){
    int zone = instruction.substring(1, 2).toInt();
    double capacite = instruction.substring(2, 6).toDouble();
    double consigne = instruction.substring(6, 10).toDouble();
    zones[zone].consigne(capacite, consigne);
  } else if (instruction.startsWith("D")){
    Serial.println("Voici la date");
  } else if (instruction.startsWith("H")){
    Serial.println("Voici l'heure'");
  } else if (instruction.startsWith("S")){
    int zone = instruction.substring(1, 2).toInt();
    Serial.print("Voici l'état de la Zone ");
    Serial.println(zone);
  } else {
    Serial.println("--- ERREUR DE LECTURE ---");
    Serial.println(instruction);
  }  
}


