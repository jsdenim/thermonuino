Situations nécessitant une icone plein écran 

## Batterie HS
## Porte Ouverte
## Mode arrêt (Stop, Vacance, Au dessus du max de fonctionnement)

Petites icones écran d'accueil : 

L'écran doit indiquer la température courante, la température consigne. Eventuellement, la température extérieure. 
Il peut indiquer que la batterie est basse. 
Il indique la date et l'heure. 
Le bas de l'écran représente une courbe de température de la journée. 
Il indique aussi s'il communique bien avec la centrale. 

Petites icones : 
- Batterie déchargée
- Horloge, calendrier
- Signe Wifi
- Thermomètre
- Maison

## Base eInk 0.97" GDEM0097T61

Le driver bas niveau de l'écran est pose dans `ThermioEink097.h`.

Points valides par les tests PCB et par la demo GoodDisplay locale :

- controleur SSD1680 / ecran GoodDisplay GDEM0097T61 ;
- geometrie RAM officielle GoodDisplay : `88 x 184` ;
- taille physique visible : `184 x 88` ;
- commande `0x11 = 0x01` ;
- fenetre RAM X : `0..10` octets ;
- fenetre RAM Y : `183..0` ;
- pointeur initial : `x = 0`, `y = 183` ;
- refresh plein ecran : `0x22 = 0xF7`, puis `0x20` ;
- deep sleep apres refresh : `0x10 = 0x01`.

Sur le PCB Thermonuino Sonde, le texte n'est lisible du bon cote que si l'axe
RAM X est miroir. Cette correction est volontairement centralisee dans
`ThermioEink097::mapRamToReadableFront()` pour eviter d'empiler des rotations
et miroirs dans le code applicatif.

La future interface graphique doit donc dessiner en coordonnees "front" du
driver (`88 x 184`) et laisser `ThermioEink097` convertir vers l'organisation
RAM attendue par l'ecran.
