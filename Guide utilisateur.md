# Guide utilisateur Thermonuino

Ce guide explique l'usage courant du chauffage Thermonuino, sans détail technique.

## Utilisation quotidienne

La console choisit automatiquement quand chauffer chaque pièce. Elle utilise les thermomètres, les détecteurs d'ouverture et la programmation que vous indiquez depuis les thermomètres.

La température d'une pièce se règle depuis le thermomètre de cette pièce :

1. Depuis l'écran principal, appuyer sur `+` ou `-`.
2. Régler la température voulue.
3. Laisser le thermomètre revenir tout seul à l'écran principal pour faire une demande temporaire.
4. Appuyer sur le bouton central avant le retour à l'écran principal pour enregistrer cette température dans la programmation habituelle.

Une demande temporaire dure environ 2 heures, sauf si un autre changement programmé arrive avant.

## Modes de la console

La molette de la console choisit le mode général.

| Mode | Effet |
|---|---|
| Normal | Fonctionnement habituel. La console suit la programmation et les thermomètres. |
| Plus | Demande un peu plus chaud temporairement, sans modifier la programmation. |
| Moins | Demande un peu moins chaud temporairement, sans modifier la programmation. |
| Douche | La salle de bain vise plus chaud pendant environ 30 minutes. Le reste du logement baisse légèrement. |
| Stop | Coupe le chauffage. |
| Vacances | Maintient une température basse de sécurité, autour de 17 degrés. |

Les modes Plus, Moins, Douche, Stop et Vacances ne changent pas la programmation enregistrée.

## Thermomètres

L'écran principal affiche la température de la pièce.

Avec `+` ou `-`, le thermomètre passe sur l'écran de consigne. C'est la température souhaitée.

Un appui long sur le bouton central ouvre le menu. Le menu sert surtout à :

| Page | Utilité |
|---|---|
| Pièce ? | Choisir la pièce associée au thermomètre. |
| Apprentissage | Activer ou désactiver la programmation automatique de la pièce. |
| Thermomètre | Voir la mesure du capteur et régler une petite correction si besoin. |
| Batterie | Voir l'état de la pile. |
| Comm. console | Vérifier si le thermomètre parle bien avec la console. |
| Reset zone | Effacer la programmation de la pièce. |
| Reset global | Effacer toute la programmation. |

Si la console est en Stop ou Vacances, le thermomètre affiche l'état correspondant et ne change pas la programmation.

## Détecteurs d'ouverture

Un détecteur d'ouverture indique à la console qu'une fenêtre ou une porte est ouverte.

Quand une ouverture est détectée dans une pièce, la console évite de chauffer inutilement cette pièce.

## Appairage

L'appairage sert à dire à la console quel thermomètre ou quel détecteur appartient à quelle pièce.

La console accepte les appairages pendant les premières minutes après son démarrage. Pendant ce temps, la LED centrale est blanche.

### Appairer un thermomètre

1. Redémarrer la console.
2. Pendant que la LED centrale est blanche, aller dans le menu du thermomètre.
3. Ouvrir `Piece ?`.
4. Choisir la pièce.
5. Valider.
6. Attendre la confirmation.

### Appairer un détecteur d'ouverture

1. Redémarrer la console.
2. Pendant que la LED centrale est blanche, appuyer sur le bouton du détecteur.
3. Chaque appui fait passer à la pièce suivante.
4. Arrêter d'appuyer quand la bonne pièce est indiquée par la console.
5. Après quelques secondes sans appui, la console enregistre.

La console confirme la pièce enregistrée avec une LED de zone blanche pendant quelques secondes.

## Reset EEPROM

Un reset EEPROM efface les informations mémorisées localement dans l'appareil concerné. Il faut ensuite refaire l'appairage.

| Appareil | Procédure |
|---|---|
| Thermomètre | Maintenir le bouton central enfoncé pendant l'alimentation du thermomètre. |
| Détecteur d'ouverture | Maintenir le bouton du détecteur enfoncé pendant l'alimentation du détecteur. |
| Console | Utiliser la commande de debug prévue pour effacer les associations, ou reprogrammer avec effacement si nécessaire. |

Après un reset d'un thermomètre ou d'un détecteur, l'appareil peut attendre une première pression de bouton pour recréer son identité.

## Signification des LED

### LED centrale de la console

| Etat | Signification |
|---|---|
| Verte quelques instants au démarrage | La console communique bien avec la partie pilote. |
| Rouge fixe | Problème avec la communication vers la partie pilote. La console s'arrête. |
| Rouge clignotant | Problème avec la radio 433 MHz. La console s'arrête. |
| Blanche | La console accepte les appairages. |
| Eteinte | Fonctionnement normal, rien de particulier à signaler. |

### LED de mode de la console

| Etat | Signification |
|---|---|
| Verte | Mode Normal. |
| Orange | Mode Plus. |
| Bleue | Mode Moins. |
| Orange et verte en alternance | Mode Douche. |
| Eteinte | Mode Stop. |
| Bleue, clignotement lent | Mode Vacances. |

### LED des zones de la console

| Etat | Signification |
|---|---|
| Jaune ou orange fixe | La zone chauffe. |
| Bleue fixe | Porte ou fenêtre ouverte dans cette zone. |
| Eteinte | Rien de particulier. |
| Rouge fixe | Le thermomètre de cette zone ne donne plus de nouvelles. |
| Rouge clignotant lent | Pile du thermomètre faible. |
| Petit flash rouge | La console vient de recevoir une trame du thermomètre. |
| Violet fixe | Le détecteur d'ouverture de cette zone ne donne plus de nouvelles. |
| Violet clignotant lent | Pile du détecteur d'ouverture faible. |
| Petit flash violet | La console vient de recevoir une trame du détecteur d'ouverture. |
| Blanche clignotante | Zone candidate pendant un appairage. |
| Blanche fixe quelques secondes | Appairage enregistré sur cette zone. |

Si plusieurs choses arrivent en même temps, la console peut alterner les indications. Par exemple, une porte ouverte peut s'afficher en bleu, puis laisser apparaître brièvement une alerte de pile faible.

### LED du détecteur d'ouverture

| Etat | Signification |
|---|---|
| Eteinte avec l'aimant en place | Porte ou fenêtre fermée. |
| Allumée sans l'aimant | Porte ou fenêtre ouverte. |
| Flash court après un appui | L'appui a été pris en compte. |
| Allumée environ 1 seconde après une émission | La console a répondu. |
| Flash court répété après une émission | La console n'a pas répondu. |

## En cas de doute

1. Vérifier le mode de la console.
2. Vérifier si la LED centrale signale un défaut.
3. Vérifier la LED de la zone concernée.
4. Vérifier l'écran du thermomètre.
5. Si un thermomètre ou un détecteur semble perdu, refaire l'appairage.
