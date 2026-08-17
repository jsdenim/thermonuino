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
- transmettre la presence detectee et une variation utilisateur par creneau ;
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

Il gere 4 zones, 672 creneaux hebdomadaires et des consignes entieres en
demi-degres. Un creneau stocke un changement de consigne, pas l'etat complet.
La presence est stockee separement sous forme d'un booleen par zone et par
creneau hebdomadaire. A chaque execution non-replay d'un creneau,
`presence[zone][slotOfWeek]` est remplace par la detection courante ; cela
ecrase naturellement la valeur de la semaine precedente quand la simulation
repasse sur le meme creneau.

Principes implementes :

- premiere action utilisateur immédiatement utilisable ;
- fallback provisoire depuis les jours precedents tant que le jour courant n'a
  pas encore de consigne active ;
- confiance plafonnee pour eviter une inertie infinie ;
- renforcement fort sur confirmation explicite, faible en absence de correction ;
- contradiction avec baisse de confiance de l'ancienne habitude ;
- remplacement rapide apres deux observations recentes coherentes ;
- exclusion prevue des overrides temporaires via le parametre `temporaryOverride`
  du coeur C++.

Dans l'UI actuelle, les boutons `+` et `-` envoient une impulsion de variation
sur le creneau courant uniquement. Plusieurs clics sur le meme creneau
s'additionnent (`+0,5`, `+1,0`, etc.) et relancent aussitot le calcul du meme
cycle ; des que la simulation avance ou rejoue sans demande utilisateur, la
variation envoyee retombe a `0`. Le clic compte aussi comme presence ponctuelle.
Le bouton `Maintenant` marque une presence uniquement sur le creneau courant. Le
journal affiche notamment `conf`, `learned`, `contradiction` et l'eventuel
`candidate`.
