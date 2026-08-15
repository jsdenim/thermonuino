#include "Arduino.h"

//Le temps de chauffe est réparti par créneaux sur une heure. 
//Une zone connait le nombre de watt qu'elle peut produire en une heure.
//La consolle donne une consigne de watt par heure à produire. 
//On décide qu'une heure est découpée en créneaux de DURREE_CRENEAU_EN_SECONDES (10 secondes)
//C'est le temps de travail minimum. 

//Une fois que l'on connais la durée d'un créneau de travail, on sais combien on a de créneaux dans une heure : 
#define NOMBRE_CRENEAUX_EN_UNE_HEURE 360
        //(60*60)*DURREE_CRENEAU_EN_SECONDES

#define DURREE_CRENEAU_EN_SECONDES 10

/**
  Informations en rapport à avec une zone comportant un ou plusieurs radiateurs à fil pilote. 
*/
class Zone {
  private :

    //Pin de l'optocoupleur qui gère cette zone.
    byte optoPin;
    
    //Pin de la LED témoin sur le tableau électrique correspondant à cette zone.
    byte ledPin;

    //Cummul de la puissance en watt de tous les radiateurs branchés sur la zone.
    int capaciteZoneWattHeure = 1200;
    
    //Watt par heure qu'il faut produire sur la zone. (info donnée en ordre par la console)
    int consigneDepenseZoneWattHeure = 1;
    double nombreCreneauxOuIlFautChauffer = 0;
    
    //En gros, sur les créneaux de temps qui passent, tous les combien de créneaux il me faut en compter pour me mettre à chauffer. 
    double intervalesPourChauffer = 1;

  public :
  void Setup(byte optoPin, byte ledPin, int capaciteZoneWh){
    this->optoPin = optoPin;
    this->ledPin = ledPin;
    pinMode(ledPin, OUTPUT);
    pinMode(optoPin, OUTPUT);

    this->consigne(capaciteZoneWh, 0);
    this->forcerEtat(false);
  }

  /**
    En fonction du créneau actuel dans lequel on est (passé en paramètre), va déterminer s'il faut chauffer ou pas, 
    et enclancher les optocoupleurs et led en correspondance. 
  */
  bool activerZone(double indexCreneau){
      if(fmod(indexCreneau, this->intervalesPourChauffer) < 1){
        forcerEtat(true);
        return false;
      } else {
        forcerEtat(false);
        return true;      
      }
  }

  void forcerEtat(bool allumer){
    digitalWrite(this->ledPin, allumer ? LOW : HIGH);
    digitalWrite(this->optoPin, allumer ? LOW : HIGH);
  }

  /**
    Met à jour la consine de dépense exprimée en Watt par heure, et met à jour les calculs internes de la zone pour pouvoir 
    activer la zone aux bons créneaux.
  */
  void consigne(double capacite, double wh){
    if(wh <= 0){
      wh = 0.001;
    }

    this->capaciteZoneWattHeure = capacite;

    if(wh > this->capaciteZoneWattHeure){
      wh = this->capaciteZoneWattHeure;
    }

    this->consigneDepenseZoneWattHeure = wh;
    this->nombreCreneauxOuIlFautChauffer = ((double)NOMBRE_CRENEAUX_EN_UNE_HEURE * this->consigneDepenseZoneWattHeure) / this->capaciteZoneWattHeure;
    this->intervalesPourChauffer = ((double)NOMBRE_CRENEAUX_EN_UNE_HEURE) / this->nombreCreneauxOuIlFautChauffer;

  /*
    Serial.print("capaciteZoneWattHeure=");
    Serial.println(this->capaciteZoneWattHeure);

    Serial.print("consigneDepenseZoneWattHeure=");
    Serial.println(this->consigneDepenseZoneWattHeure);

    Serial.print("nombreCreneauxOuIlFautChauffer=");
    Serial.println(this->nombreCreneauxOuIlFautChauffer);

    Serial.print("intervalesPourChauffer=");
    Serial.println(this->intervalesPourChauffer);
*/
    

  }

};


