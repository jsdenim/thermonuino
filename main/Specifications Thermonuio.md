Le projet Thermonuino est un ensemble de composants qui communiquent entre eux pour gérer le chauffage d’un appartement. 

Le mode de commande est par fil pilote : les radiateurs sont réglés au maximum, et on les commande soit en “CONFORT” \= rien dans le fil pilote, pour les faire chauffer, soit en “HORS-GEL” \= demi-alternance dans le fil pilote. 

Il y a 4 type de composants dans le système :   
La partie pilote, la partie console, la partie mesure, et la partie détection de fenêtre ouvertes. 

La partie pilote et reliée à la partie console par deux paires de cuivre : 5V, GND, et RX et TX pour communiquer.   
Les parties mesure et détection de fenêtre communiquent avec la console par RF 433 Mhz via des modules CC1101. Pour ces derniers, chaque module doit avoir une adresse unique et vaguement aléatoire sur 32000 possibilités, (pour éviter des conflits avec d’autres appartements).

Tous les composants fonctionnent avec des Atmega328p à 8Mhz, en 5v quand il y a une alimentation continue, ou en 3.3v quand ils sont sur pile. 

Le système est capable de gérer 4 zones, et dans chaque zone, il y a une sonde de mesure, et éventuellement un détecteur de portes ouvertes. 

Fonctionnement général : 

La température commandée se fait par zone, et par apprentissage progressif. Dans un premier temps, l’utilisateur va indiquer sur chaque zone la température qu’il souhaite, et le système va mémoriser qu’à telle heure, tel jour de la semaine, il doit faire telle température dans la zone. Chaque fois qu’il reprécise son souhait, cela enrichit la programmation. 

La partie console permet de dériver de la programmation habituelle pour toutes les zones, en demandant un peu plus chaud, un peu plus froid, rien du tout…. 

Les détecteurs de porte ouverte permettent de mettre en pause le chauffage dans la zone correspondante. 

# Partie Pilote

Sur la partie haute tension, elle gère les ordre dans le fil pilote. Ensuite, il y a une alimentation 5V qui alimente la logique de PCB, et alimente aussi la partie console.   
Les deux PCB sont reliés par deux paires torsadées, une pour le 5v, l’autre pour une communication série en 9600 bauds. 

Cette partie gère aussi une lecture des trames TIC d’un compteur Linky.   
La TIC est employée pour deux choses : lire la date et l’heure (et la transmettre a la console), et déterminer la variation de courant soutirée lorsqu’un radiateur est commandé (ce qui aide pour la logique de chauffage). 

La partie pilote envoie à la console l’heure courante, l’estimation déduite de la puissance des radiateurs sur chaque zone.   
Elle reçoit de la partie console des instructions de chauffage pour chaque zone.   
L’instruction est sous forme de duty cycle.   
La durée du cycle est indiquée par la console pour toutes les zones à la fois. Probablement que 30 minutes est une bonne base de travail.   
Ensuite, la console indique pour chaque zone le temps de travail sur la durée du cycle, par une valeur comprise entre 0 et 255\. 0 représentant aucune activation du chauffage, 255 une activation permanente pour les 30 prochaine minutes du cycle de travail.   
127 représente une activation durant 15 minutes, découpées en plusieurs morceaux pour être réparti équitablement dans le temps de travail des 30 minutes.

La réception de nouvelles instructions de travail provoque la rupture du duty cycle déjà en cours. 

Sur la partie pilote, il y a un bouton statut, et un bouton par zone, une chaîne de LED adressable, une pour le statut, une pour le Linky, et une par zone.   
Tant qu’on appuie sur rien, la partie pilote respecte ce que dit la console.   
La LED statut est alors verte. 

Si on appuie sur l’un des boutons d’une zone, la partie pilote n’écoute plus ce que dit la centrale. La zone correspondante est inversée, et on ne fait rien de plus. Plusieurs zones peuvent ainsi être forcées à être allumées.   
Le bouton statut permet de revenir au mode normal. 

Dans tous les cas, les LED des zones indiquent si le chauffage est commandé ou pas. 

Si la détection de la puissance d’une zone échoue, on peut afficher une LED violette quand elle n’est pas commandée. Le reste du temps, quand on fait chauffer une zone, on allume en orange, si non éteint. 

# Partie Console

La partie console dispose d’un CC1101 pour échanger avec les sondes de mesure et le détecteur de portes ouvertes. 

La partie console est responsable de mémoriser la programmation de la température dans chaque pièce, et d’ordonner à la partie pilote un rythme de chauffage pour chaque zone.   
La mémoire se fait sur l’EEPROM séparée, accessible en I2C, d’une capacité de 512Kbits.

La partie console dispose d’un ATH30 en I2C pour une mesure de température “de secours”, c’est à dire si les sondes de mesures ne donnent plus de signal. 

Le PCB dispose d’une chaine de LED adressables pour chaque zone, et une sous la mode sélectionné, et une au centre du boitier.

L’utilisateur peut choisir un mode à l’aide d’une molette, qui s’applique alors sur toutes les zones

* Normal : respecte la programmation habituelle apprise pour chaque zone.   
* Plus : augmente d’un degré tout l’appartement par rapport à la programmation normale.   
* Moins : diminue d’un degré tout l’appartement par rapport à la programmation normale.   
    
  Les modes plus et moins peuvent être utilisés de manière impulsionnelle : un retour à normal de moins de 3s suivi d’un retour à plus ou moins additionne l’effet.   
    
* Douche : le reste de l’appartement reste en mode normal, mais une zone spécifiée fait \+2 degrés durant 30 minutes, puis repasse en mode normal. Par défaut, la zone de douche correspond à la zone n°1.  
    
* Stop : tout les chauffages sont coupés.   
    
* Vacance : applique une règle à 17° pour tout l’appartement, mesuré uniquement sur la centrale. 

Le mode vacances peut aussi se déclencher de lui-même lorsque les détecteurs de mouvement ne rapportent plus de mouvement depuis plus de 2 jours. (Fonctionnalité non appliquée lorsqu’aucune zone n’a plus d’information des sondes de mesure.) 

La LED du mode :   
Vert \= Normal  
Plus \= Orangé  
Moins \= Bleu  
Douche \= Alternance Orangé / Vert  
Stop \= éteint  
Vacance \= Bleu clignotant doucement (1 part 10 secondes)

Fonctions des LED par zone : 

* Orangé : chauffe en cours.   
* Violet : porte ouverte détectée  
* Éteint : pas d’action en cours.   
* Rouge clignotant par alternance avec la couleur normale : pile de la sonde de mesure à remplacer.   
* Rouge fixe : plus de communication avec la sonde de mesure  
* Violet clignotant par alternance avec la couleur normale : pile du détecteur de porte ouverte à remplacer. 

La partie console indique aux sondes de mesure de basculer en OFF lorsque l’une des conditions suivantes est remplie :   
Le mode de fonctionnement est sur STOP ou VACANCE  
La température de la centrale est supérieure à 23° depuis plus d’un jour. 

# Partie Mesure

ATH30 en I2C, détecteur de mouvement, état de la pile. 

La communication avec la console se fait à l’aide d’un module CC1101, qui est derrière un transistor sur sa ligne 3v3 pour l’activer.   
Par défaut, le CC1101 est désactiver. Quand il communique avec la console, il écoute pendant quelques secondes la réponse de la console qui accuse réception de ce que la sonde communique, et indique aussi les messages qui étaient en attente pour la sonde de mesure. 

La partie sonde dispose aussi un écran et d’un switch bidirectionnel \+ bouton.   
La fréquence de communication avec la centrale dépend des conditions, et des interactions avec l’utilisateur : 

* Tant que la température est stable, et sans interaction de la part de l’utilisateur, ou que la centrale a indiqué qu’il fallait se mettre en OFF, la sonde ne parle que toutes les heures.   
* La sonde signale sous quelques secondes tout changement de température de plus de 0.45°.   
* De même, si l'utilisateur fait des changements sur la température transitoire dans la pièce, ou sur la programmation, ils sont communiqués à la centrale sous quelques secondes. 

La sonde dispose donc de plusieurs états de fonctionnement, avec une répercussion sur l’écran. 

* Normal : affichage classique  
* Réglages : sous forme de menu :   
  * Débuter la séquence pour s’associer à la console,   
  * Sélectionner la zone qui est associée à la sonde,   
  * Indiquer si cette zone correspond à celle du programme Douche.   
  * Faire relais pour la sélection de la zone associée au détecteur de porte ouverte.   
  * Afficher la date et l’heure de la console, et la modifier (utile si la communication Linky est KO)  
  * Spécifier la température de consigne par défaut, utilisée partout s’il n’y pas de consigne particulière (18° par défaut)  
  * Afficher et modifier la puissance de chauffage de la zone  
  * Remettre à zéro l’apprentissage pour la zone correspondante.   
* Batterie faible : la sonde n’émet plus, l’écran affiche une batterie vidée, et l’atmega se met en arrêt définitif.   
* OFF : la sonde n’émet plus que toutes les heures. 

En mode normal, l’écran affiche la température actuelle, et une flèche vers le haut s’il faut chauffer, ou une flèche vers le bas, s’il faut laisser refroidir. 

Si l'utilisateur manœuvre le switch vers le haut ou vers le bas, cela signale un souhait de monter ou baisser la température de façon transitoire, c'est-à-dire seulement juqu’au prochain point de programmation.   
Si l’utilisateur fait suivre ce gestion par un appui sur le bouton central, cela transforme l’instruction en changement pérenne sur la programmation. 

Un appui long sur le bouton central (10 secondes), cela fait sortir le menu de programmation.  

Un détecteur de mouvement PIR permettent de savoir s’il y a toujours quelqu’un dans la zone. Du point de vue global de l’appartement, on peut déduire après 2 jours sans mouvement dans tout l’appartement qu’il n’y a personne, et qu’on peut basculer automatiquement en mode vacance. Au niveau d’un pièce, cela peut aussi servir à enrichir la programmation : si on remarque que la personne passe souvent dans un même créneau horaire, et plus rarement dans un autre, on peux ajuster la consigne avec cela. 

# Partie détection fenêtre ouverte

Etat de la pile, interrupteur REED, CC1101.   
Signale un changement d’état si on ne revient pas à l’état précédent en moins de 10 secondes.   
Parle au moins toutes les heures à la centrale.   
Un bouton permet de mettre le module en mode association. Une fois fait, sur le boitier sonde de la même zone, il faut se mettre en programmation / association du détecteur de porte ouverte, ce qui provoquera l’association du module en tant que détecteur de porte ouverte de la même zone que la sonde. 

# Réglages techniques RF CC1101

Les essais de portée entre la console et le détecteur de porte ouverte ont montré que la communication CC1101 est très sensible aux périodes où le microcontrôleur est occupé à faire autre chose. Les motifs LED de diagnostic ne doivent donc pas bloquer l'exécution radio.

Réglages validés pour les tests de portée :

* La console/dial reste en réception presque permanente.
* Les modules sur pile ne doivent pas émettre des rafales aveugles. Ils utilisent un protocole à tentative unique, ACK, timeout et retry.
* Avant d'émettre, un module sur pile passe brièvement en réception et vérifie si le canal semble occupé. Si le canal est occupé, il attend un délai aléatoire avant de réessayer.
* La trame de test contient un préfixe `TNU`, l'identifiant source, un compteur, un type, puis le compteur accusé :
  * `B` pour une balise du détecteur de porte ouverte ;
  * `A` pour un accusé de réception de la console ;
  * le dernier octet vaut 0 dans une balise, et vaut le compteur reçu dans un ACK.
* Après une trame émise, le module sur pile passe immédiatement en réception et attend un ACK de la console pendant environ 300 ms.
* Si l'ACK n'arrive pas avant timeout, le module attend un délai aléatoire de backoff, puis réessaie. Le backoff augmente avec le nombre d'échecs.
* Valeurs de départ recommandées :
  * écoute canal avant émission : 30 ms ;
  * timeout ACK côté module sur pile : 300 ms ;
  * nombre maximal de tentatives : 6 ;
  * backoff initial : 100 à 600 ms, augmenté à chaque tentative.
* La console répond par une courte série d'ACK pendant environ 300 ms, pour donner plusieurs chances au module sur pile de les recevoir sans saturer le canal.
* Pendant l'émission des ACK, la console peut indiquer visuellement l'émission de retour, par exemple en passant la LED SDB en orange.
* Sur le détecteur de porte ouverte, le résultat de transaction peut être indiqué pendant 3 s :
  * LED fixe si l'ACK a été reçu ;
  * LED majoritairement allumée avec des extinctions de 250 ms si aucun ACK n'a été reçu.
* Les FIFO RX doivent être vidées seulement en cas d'overflow réel (`RXBYTES & 0x80`). Il ne faut pas vider la FIFO quand une trame partielle est en cours de réception.
* Les motifs LED doivent être non bloquants. Un `delay()` long dans un clignotement peut faire manquer les balises ou les ACK suivants.
* La reconfiguration complète du CC1101 ne doit pas être faite périodiquement pendant l'écoute normale, car elle peut tomber au moment où une trame arrive. Elle est utile au démarrage, ou après une erreur radio identifiée.
* Sur le PCB "porte ouverte" testé, le CC1101 est alimenté en 3,3 V permanent. La broche historique `RF_EN` ne doit pas être utilisée pour couper ou activer la RF dans ce test.

# Protocole entre la partie PILOTE et CONSOLE : 

Liaison série 9600 abauds.   
cable de 2m max. Est-ce utile de prévoir quelque chose pour s’assurer de recevoir des trammes d’info complette ? 

Sens PILOTE \-\> CONSOLE :   
TIMESTAMP : yyyy-mm-dd hh:mm:ss  
Z1\_PUISSANCE : 0 à 2500, en VA équivalent watt.   
Z2\_PUISSANCE   
Z3\_PUISSANCE   
Z4\_PUISSANCE 

Sens CONSOLE \-\> PILOTE

DC\_LENGTH \= 30, durée en minute  
Z1\_WORKLOAD \= 0, pas de travail sur les 30 prochaines minutes  
Z2\_WORKLOAD \= 255, travail permanence sur les 30 prochaines minutes  
Z3\_WORKLOAD \= 127, travail 50% du temps sur les 30 prochaines minutes, réparti harmonieusement.   
Z4\_WORKLOAD \= 64, travail 25% du temps sur les 30 prochaines minutes, réparti harmonieusement. 

Le workload indiqué ici est purement indicatif. Il s’agit d’un byte qui peut aller de 0 à 255\. 

# Programmation automatique par apprentissage

La console assure une programmation automatique des consignes de température par **apprentissage des habitudes de l'utilisateur**, sans nécessiter la création manuelle d'un planning.

L'objectif est que lorsqu'un utilisateur demande régulièrement une certaine température dans une zone à un certain moment de la journée, le système apprenne ce comportement et applique ensuite automatiquement cette consigne.

L'apprentissage doit privilégier deux qualités potentiellement contradictoires :

* **la stabilité**, afin qu'une action exceptionnelle ne modifie pas immédiatement une habitude bien établie ;  
* **la capacité d'adaptation**, afin qu'une habitude ancienne puisse néanmoins être modifiée rapidement lorsque le comportement de l'utilisateur change.

La confiance accordée à une programmation ne doit donc pas créer une inertie croissante avec son ancienneté. Une habitude utilisée depuis plusieurs mois doit pouvoir être remplacée rapidement lorsque plusieurs observations récentes cohérentes indiquent un changement.

### **Découpage temporel**

La semaine est découpée en créneaux de **15 minutes**, soit :

* 96 créneaux par jour ;  
* 672 créneaux par semaine ;  
* un ensemble indépendant de créneaux pour chaque zone.

Une action effectuée par l'utilisateur est affectée au créneau de 15 minutes en cours, en utilisant le début du créneau.

Par exemple :

* une action à 18h03 correspond au créneau de 18h00 ;  
* une action à 18h17 correspond au créneau de 18h15 ;  
* une action à 18h29 correspond également au créneau de 18h15.

### **Signification d'un créneau**

Les créneaux représentent des **changements de consigne** et non l'état complet du chauffage.

Un créneau non renseigné ne signifie donc pas qu'aucune température n'est demandée. Il signifie simplement qu'aucun changement de consigne ne doit intervenir.

Par exemple :

18h00 : 20 °C  
18h15 : \-  
18h30 : \-  
18h45 : \-  
19h00 : \-  
...  
22h45 : 17 °C

signifie que la consigne passe à 20 °C à 18h00, reste à 20 °C jusqu'à 22h45, puis passe à 17 °C.

### **Première phase d'apprentissage**

Lorsque la mémoire d'apprentissage est vide, le système doit apprendre rapidement.

Une première action de l'utilisateur constitue immédiatement une information utilisable. Il n'est pas nécessaire d'attendre plusieurs semaines avant de commencer à automatiser le chauffage.

Tant qu'un jour de la semaine ne dispose pas encore de son propre historique, les habitudes observées les jours précédents peuvent être utilisées comme programmation provisoire.

Par exemple, si le dimanche matin l'utilisateur demande 20 °C à 8h00, le système peut proposer automatiquement 20 °C le lundi à 8h00.

Si l'utilisateur demande alors 18 °C, le système apprend que le comportement du lundi est différent de celui du dimanche.

Progressivement, chaque jour de la semaine acquiert ainsi sa propre programmation et les données spécifiques à ce jour deviennent prioritaires sur les habitudes provenant des jours précédents.

### **Confiance dans une consigne**

Chaque consigne apprise possède un **niveau de confiance** indiquant à quel point le système considère cette habitude comme établie.

Une action explicite de l'utilisateur correspondant à la consigne apprise renforce fortement sa confiance.

Lorsqu'une consigne automatique est appliquée et qu'elle reste en vigueur sans être corrigée par l'utilisateur, cela peut également renforcer sa confiance, mais plus faiblement qu'une action explicite.

L'absence de correction constitue en effet une indication positive, mais moins fiable qu'une demande volontaire de l'utilisateur.

Les créneaux sans changement de consigne peuvent ainsi contribuer à renforcer la confiance de la **dernière consigne active**, sans créer de nouvelles consignes redondantes dans ces créneaux.

La confiance doit être plafonnée ou conçue de façon à ne jamais produire une inertie excessive.

### **Contradiction et changement d'habitude**

Lorsqu'une consigne automatique apprise est active et que l'utilisateur demande manuellement une température différente, cette action constitue une **contradiction** de la programmation existante.

Cette contradiction a deux effets :

1. elle diminue la confiance accordée à l'ancienne consigne ;  
2. elle constitue une nouvelle observation pour le créneau dans lequel l'utilisateur effectue sa modification.

Une seule contradiction ne doit normalement pas suffire à supprimer une habitude bien établie. Elle peut correspondre à une situation exceptionnelle.

En revanche, **deux observations récentes et cohérentes indiquant le même changement doivent suffire à remettre fortement en cause l'ancienne habitude**, même si celle-ci était utilisée depuis longtemps.

Par exemple :

Habitude existante :  
18h15 → 20 °C

Première correction :  
utilisateur → 19 °C  
\=\> 20 °C reste provisoirement programmé  
\=\> 19 °C devient une nouvelle tendance candidate

Deuxième observation cohérente :  
utilisateur → 19 °C  
\=\> l'ancienne habitude est fortement dévaluée  
\=\> la programmation converge rapidement vers 19 °C

Le niveau de confiance d'une ancienne habitude ne doit donc jamais conduire à devoir répéter de nombreuses fois une nouvelle consigne avant qu'elle soit prise en compte.

Le système doit avoir une **mémoire longue pour déterminer qu'une habitude est fiable, mais une mémoire courte pour détecter qu'une habitude est en train de changer**.

Des corrections proches et allant dans le même sens peuvent être considérées comme cohérentes sans nécessairement être strictement identiques. Par exemple, des corrections successives de 20 °C vers 19,5 °C puis vers 19 °C peuvent indiquer une même évolution de l'habitude.

### **Modifications temporaires**

La console générale de la maison permet à l'utilisateur d'indiquer temporairement qu'il souhaite avoir **plus chaud** ou **plus froid** que d'habitude.

Ces commandes constituent volontairement des corrections temporaires et **doivent être totalement exclues de la programmation par apprentissage**.

Elles ne doivent :

* ni créer une nouvelle consigne apprise ;  
* ni renforcer une consigne existante ;  
* ni diminuer la confiance d'une consigne ;  
* ni être considérées comme une contradiction.

L'apprentissage doit uniquement tenir compte des modifications de consigne effectuées depuis les commandes permettant de définir réellement la température souhaitée dans une zone.

### **Stockage**

La programmation est enregistrée dans l'EEPROM de **512 kbits (64 Kio)** présente dans la console.

Avec des créneaux de 15 minutes, une semaine représente 672 créneaux par zone. Pour quatre zones, cela représente 2 688 créneaux.

La capacité disponible étant largement suffisante, la priorité doit être donnée à une structure de données simple, robuste et facilement modifiable plutôt qu'à une compression agressive des informations.

Les températures ne doivent pas être enregistrées sous forme de nombres à virgule flottante. Elles peuvent être représentées par une valeur entière correspondant aux températures utilisables par le système, par exemple par pas de 0,5 °C entre les limites autorisées.

La structure exacte utilisée pour représenter un créneau, son niveau de confiance, les éventuelles observations contradictoires et les informations nécessaires à l'apprentissage sera définie lors de la conception logicielle.

Il faudra également tenir compte de **l'endurance en écriture de l'EEPROM** : les données ne doivent pas être réécrites inutilement à chaque cycle de fonctionnement. Les écritures devront être effectuées uniquement lorsqu'une information d'apprentissage doit réellement être conservée.

# PINOUT & PCB

## PCB Fil Pilote

Fonctionne en 5v avec un Atmega328 en 8mhz internal

LEDCHAINDATA sur PCINT21 : un réseau de 6 LED Adressables (XL-0807RGBC-2812B), dans cet ordre : LEDZONE4, LEDZONE3, LEDZONE2, LEDZONE1, LEDLINKY, LEDSTATUT.  
BTNSTAT sur PCINT19, un swtich qui va vers GND quand on appuie dessus.   
BTNMUX1 sur PCINT13, 10k vers GND quand SWZ1 est appuyé, 2,2k quand SWZ2 est appuyé.   
BTNMUX2 sur PCINT12, 10k vers GND quand SWZ3 est appuyé, 2,2k quand SWZ4 est appuyé. 

MOSI sur PCINT3  
MISO sur PCINT4  
HEARTBEAT sur PCINT18, lui envoyer du 5v pour que le watchdog n'active pas le reset.   
TX sur PCINT17 (Pour echanger avec la console déportée)  
RX sur PCINT16 (Pour echanger avec la console déportée)  
LINKY\_RX sur PCINT22  
CMDZ1 sur PCINT11, commande la base d'un MMBT3904. Envoyer du courant pour éteindre l'optocoupleur qui gère la zone.   
CMDZ2 sur PCINT10, idem  
CMDZ3 sur PCINT9, idem  
CMDZ4 sur PCINT8, idem  
SCK sur PCINT5

## PCB Sonde

Atmega328 qui fonctionne en 3v 8Mhz.

MOSI et RF\_MOSI sur PCINT3  
MISO et RF\_MISO sur PCINT4  
SCK et RF\_SCK sur PCINT5  
BAT\_SENS sur PCINT8, donne la tension de la batterie entre deux résistance de 1MHom  
BODYDETECT sur PCINT23, la sortie OUT d’un EG4005C.  
RF\_CSN sur PCINT2  
TX de débug sur PCINT17  
RF\_GDO0 sur PCINT1  
POT\_ALIM sur PCINT0, pour alimenter le potentiomètre de 10kHom que quand on veut en lire sa valeur.  
RF\_EN sur PCINT0, commande un AO3401A pour alimenter la parite RF en 3,3v.  
Une LED sur PCINT21

Un switch SLLB510100 avec CMD\_SENS1 sur PCINT16, CMD\_SENS2 sur PCINT9, CMD\_BTN sur PCINT11 , lorsqu’ils sont actionnés, ils conduisent vers GND

Dans PCINT18, y-a le réseau WAKE qui arrive, qui devrait changer quand on appuie sur le bouton, ou que BODYDETECT change

un AHT20-F sur la ligne I2C avec I2CSDA sur PCINT12 et I2CSLC sur PCINT13  
Un module à base de CC1101 au bout de RF\_\*

Un écran eInk GoodDisplay de 0.97 pouces avec CS\# sur PCINT22, RES\# sur PCINT20, D/C\# sur PCINT19, BUSY sur PCINT10, le SPI partagé avec le reste. 

## PCB Porte Ouverte 

Atmega328 qui fonctionne en 3v 8Mhz.

MOSI et RF\_MOSI sur PCINT3  
MISO et RF\_MISO sur PCINT4  
SCK et RF\_SCK sur PCINT5  
BAT\_SENS sur PCINT8, donne la tension de la batterie entre deux résistance de 1MHom  
BODYDETECT sur PCINT23, la sortie OUT d’un EG4005C.  
RF\_CSN sur PCINT2  
TX de débug sur PCINT17  
RF\_GDO0 sur PCINT1  
POT\_ALIM sur PCINT0, pour alimenter le potentiomètre de 10kHom que quand on veut en lire sa valeur.  
RF\_EN sur PCINT0, commande un AO3401A pour alimenter la parite RF en 3,3v.  
Une LED sur PCINT11, qui doit envoyer du courant pour l’allumer.

DOOR\_OPEN sur PCINT18, traverse en parallèle des interrupteurs REED, qui vont vers GND.   
Un bouton sur PCINT19 qui fait arriver 3.3v dans le pin quand on appuie dessus. 

## PCB Console : 

Atmega328 qui fonctionne en 5v 8Mhz.

eprom 24LC512T-I/SN en I2C avec I2CSDA sur PCINT12, et I2CSLC sur PCINT13

LEDCHAINDATA sur PCINT11, un ensemble de LEDS Adressables (XL-0807RGBC-2812B), avec dans l’odre : LEDSALON, LEDCHAMBRE, LEDBUREAU, LEDSDB, LEDCENTRE, LEDMODE.

Un module CC1101 avec CSN sur PCINT18, RF\_GDO0 sur PCINT10, MOSI sur PCINT4 et MISO sur PCINT3, SCK sur PCINT5

Puis une piste sur PCB avec plusieurs positions : chaque entree mode est reliee
au 5 V par une resistance de 4.7 kOhm, puis le contact du bouton/selecteur la
relie a GND quand cette position est active. Le firmware doit donc lire ces
entrees en actif bas : relache = HIGH, position active = LOW. Les retours se
font par ces pins :   
MODE\_DOUCHE sur PCINT8  
MODE\_STOP sur PCINT9  
MODE\_PLUS sur PCINT2  
MODE\_NORMAL sur PCINT1  
MODE\_MOINS sur PCINT0  
MODE\_VAC sur PCINT23. 
