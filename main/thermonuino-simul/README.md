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
