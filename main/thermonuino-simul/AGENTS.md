# Notes de reprise Codex

## Objectif du projet

Ce depot est un prototype de simulateur web pour valider une logique de thermostat
ecrite en C++ et compilee en WebAssembly.

Le simulateur represente une semaine en creneaux de 15 minutes :

- 96 creneaux par jour ;
- 672 creneaux par semaine ;
- la lecture peut boucler en fin de semaine ;
- un creneau peut etre rejoue sans modifier l'etat d'apprentissage.

## Structure

```text
src-core/greetings.cpp      Logique C++ exportee en WASM
src-core/greetings.h        Interface C partagee
firmware/main.ino           Exemple d'appel cote firmware
simulator/index.html        UI
simulator/simulator.js      Player temporel, pont JS/WASM, graphe canvas
simulator/styles.css        Styles UI
simulator/wasm/             Sortie generee par Emscripten, ignoree par Git
docker-compose.yml          Compilation via emscripten/emsdk
```

## Commandes utiles

```powershell
npm run build:wasm
npm run dev
```

Le serveur local expose ensuite :

```text
http://127.0.0.1:5173/
```

Verification rapide :

```powershell
node --check simulator/simulator.js
npm run build:wasm
```

## API C++ actuelle

```cpp
void setupThermostat(double baseTemp);
void resetThermostat();
const char* evaluateThermostatSlot(
    int absoluteSlot,
    double measuredTemp,
    double userVariation,
    int presenceDetected,
    int replayOnly);
```

`setupThermostat` initialise la temperature de base et remet l'etat
d'apprentissage a zero.

`evaluateThermostatSlot` retourne une chaine JSON consommee par
`simulator/simulator.js`. Le pointeur retourne est base sur une `static
std::string`; il faut consommer/copier la chaine avant un prochain appel si un
autre environnement est ajoute.

## Semantique actuelle

- `absoluteSlot` est monotone et peut depasser 671.
- `slotOfWeek` est calcule par modulo 672.
- `replayOnly = 1` permet de rejouer un creneau sans incrementer l'apprentissage.
- `presenceDetected` est transmis a chaque calcul.
- `userVariation` est transmis a chaque calcul, en pas UI de 0.5.
- Le graphe stocke le dernier resultat connu par `slotOfWeek`.

## Points d'attention

- Toute nouvelle fonction C++ appelee depuis JS doit etre ajoutee dans
  `-sEXPORTED_FUNCTIONS` dans `docker-compose.yml`.
- Apres modification C++, toujours relancer `npm run build:wasm`.
- `simulator/wasm/greetings.js` et `simulator/wasm/greetings.wasm` sont generes
  et ignores par Git.
- Le nom historique `greetings.*` est conserve pour l'instant, meme si la logique
  est devenue thermostat. Un renommage propre peut etre fait plus tard.
