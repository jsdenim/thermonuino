# Session Notes

Projet de test PCB autour d'un ATmega avec :
- flotteur sur `D4`
- `CMDPOMP` sur `D5`
- `CMDINTERPH` sur `D6`
- `CMDARRO` sur `D7`
- boutons :
  - `A0` pompe
  - `A1` arrosage
  - `A2` RF
  - `A3` interphone
- CC1101 :
  - `D10` CS
  - `D11` MOSI
  - `D12` MISO
  - `D13` SCK
  - `D2` GDO0
  - `D3` GDO2

## Etat actuel

Le fichier principal est `test-bt300.ino`.

Le sketch a ete refait comme banc de test du PCB :
- chaque bouton physique declenche sa fonction
- commandes serie supportees :
  - `I=5`
  - `P=5`
  - `A=5`
  - `R`
  - `H`
- le bouton RF ne fait qu'un test de presence/init du CC1101 et log `OK` ou `KO`
- la lib CC1101 utilisee est `SmartRC-CC1101-Driver-Lib` version `3.0.1`
- compatibilite adaptee a cette version avec `ELECHOUSE_cc1101.setGDO(PIN_RF_GDO0, PIN_RF_GDO2);`

## Corrections deja faites

- conflit de noms Arduino avec les macros SPI corrige en renommant les constantes locales en `BT300_SPI_*`
- retrait de l'appel invalide `setGDO2()`
- ajout d'un filtrage logiciel sur le flotteur

## Particularite materielle importante

Le pin du flotteur est flottant a cause d'un probleme de conception hardware.

Rustine logicielle actuellement en place :
- le flotteur n'est considere a `HIGH` que si le signal reste stable plus de `2000 ms`
- aucun log n'est emis pendant la phase d'attente
- seuls les evenements valides sont traces :
  - `FLOTTEUR=HIGH confirme (eau detectee)`
  - `FLOTTEUR=LOW (pas d'eau)`

Quand le flotteur est valide a `HIGH`, la pompe est commandee pendant `1000 ms`.

## Probleme hors sketch rencontre

Un probleme de televersement Arduino a ete observe :
- `avrdude: stk500_recv(): programmer is not responding`
- `avrdude: stk500_getsync() ... resp=0x00`

Hypotheses deja evoquees :
- mauvais bootloader / old bootloader si Nano
- port serie occupe
- cable USB data defectueux
- reset auto non fonctionnel
- RX/TX perturbes par le PCB
- bootloader absent ou ATmega mal configure

## Reprise conseillée

Au prochain passage :
- repartir de `test-bt300.ino`
- verifier d'abord que le sketch compile et se televerse
- ensuite tester les boutons, le flotteur et la detection CC1101 sur moniteur serie
