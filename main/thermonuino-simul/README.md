# Thermonuino WASM simulator demo

Mini-demonstrateur pour appeler une logique C++ depuis une interface web.

Le simulateur actuel execute une decision de thermostat sur une semaine
decoupee en creneaux de 15 minutes, soit 672 appels possibles par semaine.

## Prerequis

- Docker avec Compose
- Node.js

## Lancer

```powershell
npm run build:wasm
npm run dev
```

Puis ouvrir :

```text
http://127.0.0.1:5173
```

L'interface permet de :

- lancer ou mettre en pause la simulation ;
- executer le creneau suivant ;
- boucler en fin de semaine ;
- choisir un creneau precis avec la barre de lecture ;
- re-executer le creneau courant sans modifier l'etat d'apprentissage ;
- ralentir ou accelerer les appels ;
- modifier la temperature de base et les entrees de calcul ;
- transmettre la presence detectee, une ouverture de porte et une variation utilisateur par creneau ;
- activer ou desactiver l'apprentissage comme le menu sonde `APPRENT > ACTIF` ;
- visualiser la temperature decidee et les variations utilisateur sur un graphe.

## Structure

```text
src-core/greetings.cpp      Logique C++ exposee au simulateur
src-core/greetings.h        Interface partagee
firmware/main.ino           Exemple d'utilisation cote firmware
simulator/index.html        Interface web
simulator/simulator.js      Pont JS vers WebAssembly
simulator/wasm/             Sortie generee par Emscripten
docker-compose.yml          Compilation C++ vers WASM via emscripten/emsdk
```

Les fonctions exposees sont :

```cpp
const char* buildGreetings(const char* name);
void setupThermostat(double baseTemp);
void resetThermostat();
const char* evaluateThermostatSlot(
    int absoluteSlot,
    double measuredTemp,
    double userVariation,
    int presenceDetected,
    int replayOnly);
```

`evaluateThermostatSlot` retourne un JSON consomme par `simulator/simulator.js`.

## Algorithme d'apprentissage

Le coeur autonome est dans :

```text
src-core/thermostat_learning.h
src-core/thermostat_learning.cpp
```

La programmation hebdomadaire et la regulation thermique sont fournies par
`Thermonuino-commons`. Le simulateur emploie une EEPROM virtuelle adressee par
octet. Elle utilise exactement la meme API `read`/`update`, les memes adresses
et les memes formats que l'EEPROM I2C externe de la console ; seule la couche
physique differe.

Il gere 4 zones, 672 creneaux hebdomadaires et des consignes entieres en
demi-degres. Un creneau stocke un changement de consigne, pas l'etat complet.
Le stockage compact accepte jusqu'a 6 changements par jour et par zone, sans
recopier toute la semaine en RAM dans la console.
La presence est stockee separement sous forme d'un booleen par zone et par
creneau hebdomadaire. A chaque execution non-replay d'un creneau,
`presence[zone][slotOfWeek]` est remplace par la detection courante ; cela
ecrase naturellement la valeur de la semaine precedente quand la simulation
repasse sur le meme creneau.

Les ouvertures de porte/fenetre sont stockees comme un compteur d'habitude par
zone et par creneau. Si une ouverture est signalee sur le creneau, le compteur
augmente. Si la semaine suivante le meme creneau passe sans ouverture, le
compteur diminue d'une unite. Ce compteur sert a anticiper les phases
d'aeration frequentes en reduisant legerement la puissance demandee.

Le simulateur expose aussi une premiere approximation de regulation par
puissance lissee : puissance installee simulee, puissance demandee sur le
creneau et workload 0..255 envoye au pilote.

Principes implementes :

- les boutons `+` et `-` creent une consigne temporaire ;
- le bouton `OK` entre `-` et `+` simule l'appui central de la sonde et inscrit
  la consigne courante dans la programmation habituelle ;
- un changement temporaire ou programme force la consigne pendant 2 h, sauf si
  une autre consigne programmee intervient avant ;
- apres ces 2 h, le simulateur reprend la consigne programmee precedente si
  elle existe, sinon il conserve la consigne forcee jusqu'a rencontrer une
  prochaine consigne ;
- si aucune consigne n'existe sur le jour courant, le programme reprend le
  dernier jour precedent contenant une consigne ;
- sur la semaine suivante, les consignes programmees du meme jour sont
  rejouees ;
- une hausse programmee devient la cible de travail pendant le seul creneau de
  15 minutes qui la precede, si la temperature mesuree est encore inferieure a
  cette prochaine consigne ; une baisse n'est jamais anticipee ;
- l'apprentissage desactivable reste disponible : dans ce mode, la consigne
  appliquee vient directement de la sonde et la programmation n'est pas
  modifiee.

Dans l'UI actuelle, les boutons `+` et `-` envoient une impulsion de variation
relative a la consigne courante du creneau, pas relative a la temperature de
base. Plusieurs clics sur le meme creneau s'additionnent (`+0,5`, `+1,0`,
etc.) et relancent aussitot le calcul du meme cycle ; des que la simulation
avance ou rejoue sans demande utilisateur, la variation envoyee retombe a `0`.
Le clic compte aussi comme presence ponctuelle. Pour rendre la consigne
definitive, cliquer sur `OK` avant que la fenetre de correction ne se referme.
Le bouton `Maintenant` marque une presence uniquement sur le creneau courant. Le
journal affiche notamment `program-saved`, `temporary`, `preheat`, `airing`,
`requested`, `installed` et `workload`. Le graphe distingue la programmation
de la cible de travail transmise au regulateur afin de rendre visible le
creneau d'anticipation.
