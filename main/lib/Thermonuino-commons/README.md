# Thermonuino-commons

Librairie locale Arduino du projet Thermonuino.

Les fichiers et classes gardent le prefixe court `Thermio`.

Le but est de garder les sketches de production lisibles en sortant les responsabilités communes des `.ino`.

## Fichiers

* `ThermioRfFrame` : enveloppe RF commune, offsets payload, encodage/decodage report et reponse.
* `ThermioRfCc1101` : acces CC1101 minimal, configuration radio, TX/RX, `SPWD`.
* `ThermioRfIds` : identifiants RF en EEPROM interne et generation d'ID.
* `ThermioSlavePower` : watchdog 8 s, pin-change interrupt et sommeil profond ATmega.
* `ThermioSlaveLink` : etat de lien esclave, apprentissage console, echec ACK, console hors service.

## Niveaux de commun

Commun a tous les appareils :

* `ThermioRfFrame`
* `ThermioRfCc1101`
* `ThermioRfIds`

Commun aux esclaves sur pile :

* `ThermioSlavePower`
* `ThermioSlaveLink`

Les sketches gardent seulement le code propre au PCB : pinout, lecture des entrees, LED de diagnostic et payload metier.
