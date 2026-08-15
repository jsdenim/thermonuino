# Projet RF 433 MHz (SYN115 / SYN480R)

## Matériel
- Émetteur (TX) : Arduino Pro Mini + module SYN115 (433 MHz)
- Récepteur (RX) : Arduino Nano + module SYN480R (433 MHz)

## Câblage
- TX
  - SYN115 DATA → D4
  - VCC → 5V
  - GND → GND
  - Antenne ~17 cm recommandée
- RX
  - SYN480R DATA → D2
  - VCC → 5V
  - GND → GND
  - Antenne ~17 cm recommandée

## Librairie
- RadioHead (driver RH_ASK)
  - Utilisée pour l’encodage/décodage, préambule et CRC.
  - Débit actuel : 2000 bps (RH_ASK driver(2000, ...)).

## Troubleshooting (rapide)
- Vérifier antennes (~17 cm) et masse commune.
- Réduire le débit si instable (1000 bps ou 500 bps).
- Tester à courte distance (10–20 cm) pour valider la liaison.

## État
- Fonctionne sur les petits modules RF sur PCB (SYN115/SYN480R).
- L’émission ne fonctionne pas sur mes PCB perso.
