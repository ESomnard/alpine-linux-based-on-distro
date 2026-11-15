# plume-hud

Écran de **connexion** (greeter) et de **verrouillage** de Plume Linux, au style HUD futuriste.

C'est la réécriture native de **HUD Lockscreen**, le projet Electron d'ESomnard, en gardant toutes ses
fonctionnalités, mais en **~95 Ko** au lieu de ~250 Mo, et avec un vrai verrouillage sécurisé.

| | HUD Lockscreen (Electron) | plume-hud |
|---|---|---|
| Langage | JavaScript + HTML/CSS | C (Wayland + cairo + pango) |
| Taille | ~250 Mo (Chromium embarqué) | ~95 Ko, sans aucune dépendance en plus de Plume |
| Plateformes | macOS, Windows | Linux / Wayland (sway, cage…) |
| Verrouillage | fenêtre plein écran (contournable) | protocole **ext-session-lock-v1** : si le programme plante, la session **reste** verrouillée |
| Mot de passe | code séparé (haché en SHA-256) | **mot de passe du compte** (PAM / greetd) |
| Rôle | écran de verrouillage | écran de verrouillage **et** écran de connexion |

## Fonctionnalités conservées

- **5 panneaux** : opérateur et système, horloge et déverrouillage, énergie et réseau, calendrier, journal d'événements
- **Opérateur** : avatar avec initiales, nom, machine
- **Système** : OS, architecture, cœurs, uptime, jauges **CPU** et **RAM** en direct (lissées)
- **Énergie** : batterie (%), en charge ou sur batterie ; **réseau** : en ligne ou hors ligne
- **Calendrier** du mois, avec le jour courant en surbrillance
- **Journal** : initialisation, tentatives refusées, blocage, secteur branché ou débranché, réseau perdu ou rétabli
- **Sécurité** : blocage de **30 s après 5 essais** ratés, avec compte à rebours
- **Animations** : apparition des panneaux, anneaux qui tournent, voyant clignotant, champ qui tremble en cas d'erreur, fondu de sortie au déverrouillage
- **Tous les écrans** en mode verrouillage
- **Mode test** dans une fenêtre, `Échap` pour quitter
- Pied de page « VERROUILLÉ DEPUIS N MIN » et « ACCÈS RESTREINT »

## Utilisation

```sh
plume-hud --lock       # verrouiller la session (par défaut)
plume-hud -f           # idem, mais rend la main une fois verrouillé (pour swayidle)
plume-hud --greeter    # écran de connexion (lancé par greetd dans cage)
plume-hud --dev        # fenêtre de test, Échap pour quitter
```

Au clavier : `Entrée` valide, `Tab` passe du champ utilisateur au mot de passe (greeter),
`Ctrl+U` efface le champ, `Échap` vide le mot de passe.

## Dans Plume

`desktop.sh` s'occupe de tout :
1. installe temporairement les outils de compilation, compile, installe `/usr/local/bin/plume-hud`, puis **supprime les outils** ;
2. configure greetd (`/etc/greetd/config.toml`) pour lancer `plume-greeter`, c'est-à-dire cage + `plume-hud --greeter`, sur le terminal virtuel 7 ;
3. sway verrouille avec `plume-hud -f`, avec swaylock en secours.

## Compiler à la main

```sh
apk add gcc musl-dev make pkgconf wayland-dev wayland-protocols cairo-dev pango-dev libxkbcommon-dev linux-pam-dev
make && doas make install
```

## Tester sans VM

```sh
./tests/hud-preview.sh     # compile, lance les 3 modes dans un sway virtuel, captures dans out/hud/
```

## Fichiers

| Fichier | Rôle |
|---|---|
| `main.c` | Connexion Wayland, verrouillage, fenêtre, clavier, souris, boucle principale |
| `draw.c` | Dessin des 5 panneaux, reprise fidèle du CSS d'origine |
| `sys.c` | CPU, RAM, uptime, batterie, réseau |
| `auth.c` | Authentification dans un processus séparé : PAM (verrou) ou IPC greetd (connexion) |
| `pam.d/plume-hud` | Règles PAM (comme swaylock) |
