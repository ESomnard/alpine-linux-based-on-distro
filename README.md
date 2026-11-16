# Plume Linux 🪶

> Une distribution Linux **la plus légère possible**, basée sur **Alpine Linux**,
> mais **complète pour un usage quotidien**.

---

## 👥 Auteurs

Ce projet a été **conçu et dirigé par ESomnard** et
**développé avec [Claude](https://claude.com/claude-code)** (assistant IA d'Anthropic, via Claude Code).

| ESomnard | Claude |
|---|---|
| Idée et objectif du projet | Écriture du code (scripts, configs, build, tests) |
| Design et fonctionnalités de l'écran HUD (projet d'origine *HUD Lockscreen*) | Réécriture native du HUD en C (`plume-hud`) |
| Direction artistique du bureau (dev / hacker / Mac, basé sur le HUD) | Barre + dock natifs (`plume-shell`), thème, fond d'écran |
| Exigences : légèreté, usage quotidien, claviers QWERTY / AZERTY / pinyin, écran de verrouillage, test en VM | Choix techniques (sway, iwd, IBus, zram…) et mesures de poids |
| Pilotage, priorités, relecture et validation | Construction de l'ISO, tests en VM, recherche et correction des bugs |
| Tests manuels | Rédaction de la documentation |

---

## 🎯 Objectif

Avoir un ordinateur **rapide, sobre et simple**, même sur une vieille machine :

- **le moins de choses possible** installées : chaque composant est le plus petit qui fait correctement le travail ;
- **utilisable tous les jours** : internet, vidéos, musique, PDF, photos, fichiers, wifi, son, veille, luminosité, verrouillage ;
- **multilingue** : AZERTY, QWERTY et chinois pinyin ;
- **compréhensible** : quelques scripts shell commentés, pas de magie ;
- **reproductible** : l'ISO se construit avec une seule commande, et un test automatique vérifie l'installation complète.

### Poids mesuré sur le disque (Alpine 3.24, x86_64)

| Couche | Paquets | Taille cumulée |
|---|---|---|
| Base Alpine (`alpine-base`, le socle de minirootfs) | 27 | ~14 Mo |
| + noyau `linux-lts` (sans firmwares) | 47 | ~220 Mo |
| + socle système (sudo, dbus, iwd, zram…) | 75 | ~230 Mo |
| + bureau, son, claviers et pinyin | 264 | ~700 Mo |
| + applications (Firefox, mpv, PDF…) | 377 | **~1,15 Go** |
| + firmwares de **ta** machine (détectés à l'installation) | | + quelques dizaines de Mo |

Mesuré dans le test automatique, une fois tout installé : **1,1 Go sur le disque, 387 paquets** ;
**88 Mo de RAM** sans bureau, **129 Mo** avec sway, un terminal, le son et IBus lancés.

À comparer : une Ubuntu ou Fedora de bureau occupe **5 à 10 Go**, et les firmwares Linux complets pèsent à eux seuls ~770 Mo.

---

## 📦 Ce que contient la distro

### Système

| Composant | Choix Plume | À la place de | Pourquoi |
|---|---|---|---|
| Base | **Alpine Linux 3.24** (musl + BusyBox) | Debian, Ubuntu… | La base la plus petite qui reste une vraie distro |
| Démarrage des services | **OpenRC** | systemd | Simple, rapide, peu de RAM |
| Noyau | **linux-lts** 6.18 | linux-virt | `virt` n'a pas les pilotes wifi, GPU, son ni touchpad |
| Admin | **sudo** (+ doas, gardé pour Alpine) | — | Les commandes d'Ubuntu marchent : `sudo …`, `sudo apt install …` (traduit vers apk) |
| Sessions graphiques | **seatd** | elogind / logind | Minuscule, suffisant pour Wayland |
| Wifi | **iwd** | NetworkManager | Un seul démon, qui gère aussi le DHCP |
| Swap | **zram** (compressé en RAM) | partition swap | Aucun espace disque perdu |
| Heure | **ntpd de BusyBox** | chrony | Déjà présent, 0 octet de plus |
| Firmwares | **seulement ceux de ton matériel** | tous (~770 Mo) | Détectés à l'installation + `slim-firmware.sh` |
| Connexion et verrouillage | GDM / SDDM (100+ Mo), Electron (~250 Mo) | **plume-hud** (~95 Ko) | Écrit en C, outils de compilation supprimés après |

### Bureau (Wayland) — style HUD « dev / hacker / Mac »

Tout le bureau reprend l'identité visuelle de l'écran de connexion : bleu nuit `#02060b`,
traits cyan lumineux `#4fb8e0`, équerres dans les coins, police **JetBrains Mono**.

```
┌◆ PLUME  [1] 2 3 │      FOOT  ~/projets      │ CPU ▂▃▅ 12% │ RAM 18% │ ●WIFI │ lun 16 nov 23:41 ⏻┐
│  ┌──────────────────────────┐ ┌──────────────────────────┐   (bordure cyan = fenêtre active)  │
│  │ ┌─[esomnard@plume]─[~]   │ │                          │                                    │
│  │ └─➜ █                    │ │                          │                                    │
│  └──────────────────────────┘ └──────────────────────────┘                                    │
│                  ╭──── >_  ◎  ▤  ✎  ▶  ◧  ◈ │ ⏻ ────╮                                      │
└──────────────────╰─────•──────────────────────────────╯──────────────────────────────────────┘
```


| Rôle | Logiciel |
|---|---|
| Gestionnaire de fenêtres | **sway** (mosaïque, marges, bordures cyan lumineuses) |
| Barre du haut + dock | **plume-shell** (maison, ~80 Ko) : bureaux, fenêtre active, CPU en graphe, RAM, réseau, son, batterie, heure ; dock avec applis ouvertes et info-bulles |
| Fond d'écran | **plume-wall** (maison) : grille, anneaux HUD, logo PLUME |
| Poste de pilotage | **plume-term --hud** : le terminal en plein écran, entouré des performances (CPU, cœurs, RAM, swap, réseau, disques, processus), d'un explorateur de fichiers avec recherche et d'un clavier à l'écran — `Super + x` ou `◈` dans le dock |
| Logithèque | **plume-logitheque** (maison, ~120 Ko) : installer et désinstaller des applications comme GNOME Logiciels — catalogue en français par catégories, recherche dans tous les paquets Alpine et Flathub, mises à jour ; `▦` dans le dock ou « Logithèque » dans le lanceur |
| Terminal | **plume-term** (maison, ~150 Ko + libvterm ~100 Ko) : terminal HUD avec onglets, historique, copier/coller, couleurs complètes, et mode poste de pilotage — **foot** reste installé en secours (`Super + Maj + Entrée`) |
| Lanceur d'applications | **fuzzel** |
| Notifications | **mako** |
| Écran de connexion | **plume-hud** (HUD maison) lancé par **greetd** dans **cage** |
| Écran de verrouillage | **plume-hud** (même HUD, protocole sécurisé) + **swayidle** : verrouillage auto à 5 min, avant la veille, en fermant le capot |
| Captures d'écran | **grim** + **slurp** |
| Presse-papiers | **wl-clipboard** |
| Son | **PipeWire** + WirePlumber (compatible PulseAudio et ALSA) |
| Graphisme | **Mesa** (+ accélération vidéo Intel si détectée) |
| Claviers | **AZERTY + QWERTY** (`Alt + Maj`) et **chinois pinyin** via IBus (`Super + Espace`) |
| Polices | **JetBrains Mono** (dev) + DejaVu + WenQuanYi Zen Hei (chinois) |
| Terminal « hacker » | invite `┌─[moi@plume]─[~]` / `└─➜`, fiche système **fastfetch** au premier terminal, alias (`ll`, `maj`, `info`) |

### Applications

| Usage | Logiciel |
|---|---|
| Internet | **Firefox** |
| Vidéo et musique | **mpv** |
| Images | **imv** |
| PDF | **zathura** |
| Fichiers | **pcmanfm** |
| Éditeur de texte | **micro** |

### Volontairement absents

systemd, NetworkManager, PulseAudio, sudo, bash, GNOME, KDE, Qt, serveur SSH, documentation (`*-doc`).
Activables en une ligne dans `desktop.list` : Xwayland, Bluetooth, partage d'écran. L'impression s'ajoute avec une commande (voir « Utilisation au quotidien »).

---

## 📊 État du projet

| Partie | État |
|---|---|
| Construction de l'ISO (`make iso`) | ✅ Testé |
| Démarrage de l'ISO live (BIOS) | ✅ Testé en VM |
| Installation sur disque (`install.sh`) | ✅ Testé en VM (un bug de mot de passe utilisateur a été trouvé et corrigé) |
| Démarrage du système installé, réseau par câble | ✅ Testé en VM |
| Bureau (`desktop.sh`) : paquets, services, groupes, zram | ✅ Testé en VM |
| sway + terminal + son (PipeWire) + pinyin (IBus) au démarrage du bureau | ✅ Testé en VM |
| Allègement des firmwares (`slim-firmware.sh`) | ✅ Testé en VM (rien à supprimer : l'installateur ne met déjà que les firmwares nécessaires) |
| Captures d'écran dans le test automatique (connexion, bureau, verrouillage) | ✅ Testé en VM |
| `plume-hud` : verrou, greeter, mauvais / bon mot de passe | ✅ Testé dans un sway virtuel (`tests/hud-preview.sh`) |
| `plume-hud` intégré : compilé puis outils supprimés, greetd au démarrage, verrouillage depuis sway | ✅ Testé en VM |
| Thème du bureau (sway, foot, fuzzel, mako, GTK, fond d'écran) | ✅ Testé dans un sway virtuel (`tests/desktop-preview.sh`) |
| `plume-shell` (barre + dock) | ✅ Testé en VM (barre, dock, son, applis ouvertes) — bureau complet : ~150 Mo de RAM |
| `sudo` et `apt` (comme sur Ubuntu) | ✅ Testé en VM (sudo présent dès l'installation, `sudo apt update / install / remove`, `apt show / search`, refus sans `sudo`) et dans un Alpine 3.24 vierge (`apt-get`, `apt-cache`, `list`, `policy`, traduction des noms Debian, refus des `.deb`) |
| Logithèque Plume | 🟡 Testé dans un sway virtuel (`tests/logitheque-preview.sh`) : catalogue, recherche Alpine + Flathub, mauvais puis bon mot de passe, installation et suppression d'un paquet Alpine, protection des paquets vitaux, mises à jour, activation de Flathub — installation d'une appli Flathub pas encore testée (plusieurs centaines de Mo) ; test VM en cours |
| `plume-term` (terminal) | ✅ Testé en VM (compilé sur la machine, lancé depuis sway) et dans un sway virtuel (`tests/term-preview.sh`) : couleurs 16/256/24 bits, styles, accents et touches mortes, chinois, cadres, htop, historique, onglets, copier/coller, sélection à la souris, zoom |
| `plume-term --hud` (poste de pilotage) | ✅ Testé en VM (mesures réelles de la machine) et dans un sway virtuel avec souris et clavier virtuels : raccourcis, recherche, ouverture de fichier, « cd ici », fil d'Ariane, fichiers cachés, clavier à l'écran ; touches qui s'allument vérifiées à la main avec un vrai clavier (Mac, via VNC) ; ~35 Mo de RAM, ~1 à 2 % d'un cœur au repos |
| Wifi, veille, luminosité, touches multimédia | ⚪ Codé, à tester sur un vrai PC |
| Démarrage UEFI | ⚪ À tester |
| Chiffrement du disque | ⚪ À faire |

---

## 🚀 Démarrage rapide

```sh
make iso      # construit l'ISO dans out/ (Docker)
make test     # installation complète automatique dans une VM (~1 h)
make vm       # ouvre la distro installée dans une VM graphique (VNC)
```

Puis sur un vrai PC : copie l'ISO sur une clé USB → démarre dessus → `sh /etc/plume/install.sh`
→ redémarre → `sudo sh /etc/plume/desktop.sh` → redémarre. C'est tout.

---

## Sommaire

1. [Organisation du projet](#1-organisation-du-projet)
2. [Quelle base Alpine et pourquoi ?](#2-quelle-base-alpine-et-pourquoi-)
3. [Prérequis](#3-prérequis)
4. [Construire l'ISO](#4-construire-liso)
5. [Tester dans une machine virtuelle](#5-tester-dans-une-machine-virtuelle)
6. [Créer la clé USB](#6-créer-la-clé-usb)
7. [Installer sur le disque](#7-installer-sur-le-disque)
8. [Installer le bureau et les applications](#8-installer-le-bureau-et-les-applications)
9. [Alléger au maximum](#9-alléger-au-maximum)
10. [Utilisation au quotidien](#10-utilisation-au-quotidien)
11. [Personnaliser la distro](#11-personnaliser-la-distro)
12. [Mesurer la légèreté](#12-mesurer-la-légèreté)
13. [Dépannage](#13-dépannage)
14. [Feuille de route](#14-feuille-de-route)

---

## 1. Organisation du projet

```
.
├── Makefile                      # make iso / test / vm / vm-live / run / clean
├── build/
│   ├── build-iso.sh              # lance la construction dans Docker (macOS / Linux)
│   ├── inside-container.sh       # dans le conteneur : outils + mkimage.sh officiel d'Alpine
│   ├── mkimg.plume.sh            # PROFIL de l'ISO (base, noyau, paquets embarqués)
│   ├── genapkovl-plume.sh        # overlay : fichiers Plume injectés dans le système
│   └── run-qemu.sh               # VM locale si QEMU est installé sur la machine
├── hud/                          # plume-hud : écran de connexion + verrouillage HUD (C)
│   ├── main.c draw.c sys.c auth.c hud.h Makefile
│   ├── wall.c                    # plume-wall : générateur du fond d'écran
│   └── README.md                 # détails du HUD
├── shell/                        # plume-shell : barre du haut + dock (C, layer-shell)
│   └── main.c draw.c ipc.c json.c sys.c shell.h Makefile
├── term/                         # plume-term : terminal HUD + poste de pilotage (Wayland + libvterm)
│   └── main.c tab.c render.c hud.c sysinfo.c term.h hud.h Makefile
├── logitheque/                   # Logithèque Plume : installer des applications (Alpine + Flathub)
│   ├── main.c ui.c backend.c proc.c logitheque.h Makefile
│   └── catalogue.txt             # applications mises en avant (catégorie, sources, résumé en français)
├── tests/
│   ├── vm-test.sh                # test automatique complet (QEMU dans Docker)
│   ├── vm-test.py                # pilote la VM par la console série (pexpect)
│   ├── vm-gui.sh                 # VM graphique accessible en VNC
│   ├── hud-preview.sh            # teste plume-hud dans un sway virtuel (captures)
│   ├── term-preview.sh           # teste plume-term dans un sway virtuel (captures, mémoire, clics)
│   ├── vpointer.c                # souris virtuelle pour les tests de clics
│   ├── logitheque-preview.sh     # teste la Logithèque (mot de passe, installation, Flathub)
│   ├── desktop-preview.sh        # aperçu du bureau (thème, barre, dock, lanceur)
│   └── fake-greetd.py            # faux greetd pour tester l'écran de connexion
└── rootfs/                       # fichiers copiés tels quels dans le système
    └── etc/
        ├── motd                  # message d'accueil
        ├── sudoers.d/plume       # sudo pour les admins ; veille / extinction sans mot de passe
        ├── doas.d/plume.conf     # idem pour doas
        ├── profile.d/plume.sh    # ajoute /etc/plume/bin (apt) au PATH
        ├── plume/
        │   ├── install.sh        # installation sur disque (surcouche de setup-alpine)
        │   ├── desktop.sh        # installe le bureau et les applications
        │   ├── slim-firmware.sh  # supprime les firmwares inutiles
        │   ├── conf/             # configs système (iwd)
        │   └── packages/         # LISTES DE PAQUETS : core, desktop, apps
        └── skel/                 # config par défaut de chaque nouvel utilisateur
            ├── .profile          # lance sway sur tty1, variables pour IBus
            └── .config/
                ├── sway/         # config, barre d'état, bascule pinyin
                └── foot/         # terminal
```

Tout ce qui est dans `rootfs/` se retrouve dans l'ISO live **et** sur le disque après
l'installation (setup-alpine recopie `/etc`).

---

## 2. Quelle base Alpine et pourquoi ?

| Saveur d'Alpine | Taille | Démarre sur un vrai PC ? |
|---|---|---|
| **minirootfs** | ~3 Mo | ❌ Pas de noyau : un simple système de fichiers (conteneurs) |
| **virt** | ~60 Mo | ⚠️ Pas de pilotes wifi, GPU ni son : **VM uniquement** |
| **standard** | ~200 Mo | ✅ Noyau `linux-lts` + firmwares |
| extended | ~1 Go | ✅ Beaucoup de paquets en plus |

**Le choix de Plume :**
- **le système installé** part du même socle que *minirootfs* (`alpine-base`, 14 Mo), plus le noyau `linux-lts`, le minimum pour un vrai ordinateur ;
- **l'ISO d'installation** (~355 Mo) est construite à partir du profil officiel `standard`, pour démarrer sur n'importe quel PC et avoir le wifi pendant l'installation ;
- **le bureau n'est pas dans l'ISO** : `desktop.sh` le télécharge, ce qui donne une ISO plus petite et des logiciels à jour.

> Pour une distro réservée aux **machines virtuelles**, remplace `profile_standard` par
> `profile_virt` dans `build/mkimg.plume.sh` : l'ISO fera ~60 Mo.

La version d'Alpine n'est pas figée : le build prend **la dernière version stable**
(image Docker `alpine:latest`). Pour figer une version : `ALPINE_IMAGE=alpine:3.24 make iso`.

---

## 3. Prérequis

- **Docker** (Docker Desktop, Colima ou OrbStack). Il sert à tout : construire l'ISO et faire tourner les VM de test. Rien d'autre n'est à installer sur la machine.
- Une **clé USB** d'au moins 1 Go (elle sera effacée).
- Un PC x86_64 (BIOS ou UEFI) avec **Secure Boot désactivé** : Alpine n'est pas signé pour Secure Boot.
- Une connexion internet pendant l'installation (câble ou wifi).

---

## 4. Construire l'ISO

```sh
make iso
```

1. démarre un conteneur `alpine:latest` ;
2. installe les outils de build (`alpine-sdk`, `xorriso`, `grub`, `syslinux`…) ;
3. récupère les scripts officiels `mkimage.sh` d'Alpine pour la même version ;
4. ajoute le profil `plume` et l'overlay ;
5. produit `out/alpine-plume-<version>-x86_64.iso`.

Compte 10 à 15 minutes. Option : `ARCH=aarch64 make iso` pour une ISO ARM64.

---

## 5. Tester dans une machine virtuelle

### Test automatique complet

```sh
make test
```

QEMU tourne **dans Docker** et un script tape les réponses à ta place, par la console série :

1. démarrage de l'ISO live, puis `install.sh` sur un disque virtuel de 8 Go ;
2. redémarrage sur le disque, puis `desktop.sh` ;
3. vérification des services, des paquets, de sway (avec une **capture d'écran**), d'IBus et du swap zram ;
4. `slim-firmware.sh`.

Résultats : `out/test/vm.log` (console complète) et `out/test/screenshot.png`.
Compte environ 1 h : la VM est **émulée**, faute de virtualisation matérielle dans Docker sur macOS.

> Uniquement pour ce test, le disque virtuel reçoit `console=ttyS0` (pour la console série)
> et des règles `sudo` / `doas` sans mot de passe pour l'utilisateur `test`. L'ISO n'est pas modifiée.

### Essayer la distro toi-même (VM graphique)

```sh
make vm         # démarre le système installé par `make test`
make vm-live    # ou : démarre l'ISO live pour faire l'installation à la main
```

Puis connecte-toi à l'écran de la VM :

```sh
open vnc://localhost:5901       # macOS (ou Finder > Aller > Se connecter au serveur…)
```

- Mot de passe VNC : **`plume`**
- Après `make test` : l'**écran de connexion HUD** s'affiche ; utilisateur **`test`**, mot de passe **`plume-test-123`**
- La VM démarre sur une **copie** du disque (`out/vm/plume.qcow2`). Pour repartir de zéro : `rm out/vm/plume.qcow2`.
- Arrêt : `Ctrl + C` dans le terminal, ou `Super + Maj + e` → Éteindre.

> La VM est lente, en émulation logicielle. La vraie vitesse se voit sur un vrai PC.

---

## 6. Créer la clé USB

Sur **macOS** :

```sh
diskutil list                         # repère ta clé, ex. /dev/disk4  (VÉRIFIE BIEN !)
diskutil unmountDisk /dev/disk4
sudo dd if=out/alpine-plume-*.iso of=/dev/rdisk4 bs=4m status=progress
diskutil eject /dev/disk4
```

Sur **Linux** : `sudo dd if=out/alpine-plume-*.iso of=/dev/sdX bs=4M status=progress oflag=sync`

Ou plus simplement avec **balenaEtcher** ou **Ventoy**.

---

## 7. Installer sur le disque

1. Démarre le PC sur la clé (touche F12, F8, Échap ou Option selon la marque).
2. Connecte-toi en **`root`** (pas de mot de passe sur le live).
3. Lance :
   ```sh
   sh /etc/plume/install.sh
   ```
4. Réponds aux questions de Plume :
   - **clavier** : `fr fr` (France), `ch fr` (Suisse romande), `be be` (Belgique), `us us`… ;
   - **fuseau horaire** : `Europe/Paris` ;
   - **nom de la machine** : `plume` par défaut.
5. `setup-alpine` démarre avec les réglages légers déjà remplis : pas de SSH, ntpd de BusyBox, pas de partition swap, dépôt `community` activé. Il te demande ensuite :
   - **le réseau** : `eth0` (câble) ou `wlan0` (wifi : il scanne et demande le mot de passe), puis `dhcp` ;
   - **le mot de passe root** ;
   - **ton utilisateur** (`Setup a user?`) : ton login en minuscules, puis son mot de passe. Il sera admin via `sudo` ;
   - **le disque** : par ex. `sda` ou `nvme0n1`. ⚠️ **Il sera entièrement effacé.**
6. À la fin : retire la clé et tape `reboot`.

---

## 8. Installer le bureau et les applications

Après le redémarrage, connecte-toi avec **ton utilisateur**, puis :

```sh
sudo sh /etc/plume/desktop.sh            # tout
sudo sh /etc/plume/desktop.sh --no-apps  # sans Firefox, mpv… (plus léger)
sudo reboot
```

Le script :
1. active le dépôt `community` et met le système à jour ;
2. remplace `mdev` par `udev`, indispensable pour le clavier, la souris et le touchpad sous Wayland ;
3. installe `packages/core.list`, `desktop.list` et `apps.list`, plus l'accélération vidéo Intel si un GPU Intel est détecté ;
4. active les services `dbus`, `seatd`, `zram-init` et `iwd` ;
5. règle le swap **zram** à la moitié de la RAM ;
6. ajoute ton utilisateur aux groupes nécessaires (`seat`, `video`, `audio`, `input`…) et copie la config par défaut sans écraser l'existant ;
7. **compile `plume-hud`** (connexion + verrouillage), **`plume-shell`** (barre + dock), **`plume-term`** (terminal + poste de pilotage), la **Logithèque** et **`plume-wall`** (fond d'écran), puis **supprime les outils de compilation** ;
   crée aussi les dossiers Bureau, Documents, Téléchargements, Images, Musique, Vidéos ;
8. active **greetd**, qui affiche le HUD de connexion au démarrage ;
9. bascule le wifi vers **iwd**, en dernier pour ne pas couper l'installation.

Au redémarrage, **l'écran de connexion HUD** s'affiche : tape ton mot de passe, sway démarre.
Connexion texte de secours : `Ctrl + Alt + F1`.

Si tu étais en wifi, reconnecte-toi une fois avec iwd, il s'en souviendra :
```sh
iwctl station wlan0 scan
iwctl station wlan0 get-networks
iwctl station wlan0 connect "NomDuReseau"
```

---

## 9. Alléger au maximum

Les **firmwares** sont de petits logiciels que Linux envoie à certains composants (wifi,
carte graphique, bluetooth). Linux en livre pour des milliers de composants (~770 Mo),
alors qu'un PC en utilise quelques-uns.

**Bonne nouvelle : c'est déjà fait à l'installation.** L'installateur d'Alpine regarde le
matériel détecté et n'installe que les firmwares nécessaires.

`slim-firmware.sh` sert de **filet de sécurité**, par exemple si tu as installé tous les
firmwares (`sudo apt install linux-firmware`) pour faire marcher un composant. Lance-le quand
**tout fonctionne** (wifi connecté, écran OK, bluetooth activé si tu t'en sers) :

```sh
sudo sh /etc/plume/slim-firmware.sh
```

Le script :

1. liste les pilotes réellement chargés (`/proc/modules`) ;
2. demande au noyau quels firmwares chacun utilise (`modinfo -F firmware`) ;
3. retrouve les paquets correspondants (`apk info --who-owns`) ;
4. **propose** de supprimer tous les autres paquets `linux-firmware-*`, ainsi que le microcode du mauvais fabricant de CPU. Il demande confirmation.

> ⚠️ Un composant éteint pendant l'installation (wifi désactivé, dock débranché…) n'aura pas
> son firmware. Dans ce cas, ou si tu changes de matériel : `sudo apt install linux-firmware`,
> puis relance le script.

### Encore plus léger

- Retirer des applications : `sudo apt remove pcmanfm micro …`
- Pas besoin du chinois : retirer les lignes `ibus …` et `font-wqy-zenhei` de `desktop.list` (−115 Mo).
- Ne pas installer `xwayland` tant qu'aucune appli ne l'exige.
- Supprimer la documentation : `sudo apt remove $(apk info | grep -- '-doc$')`.
- Vider le cache : `sudo rm -rf /var/cache/apk/*`
- Démarrage plus rapide : `rc_parallel="YES"` dans `/etc/rc.conf`.
- Terminal plus léger : `set $term foot` dans `~/.config/sway/config` (foot : ~11 Mo de RAM, plume-term : ~24 Mo avec 2 onglets).

---

## 10. Utilisation au quotidien

### Raccourcis

La touche `Super` est la touche Windows (⌘ sur Mac).

| Raccourci | Action |
|---|---|
| Clic sur `◆ PLUME` | Lanceur d'applications |
| Clic sur un bureau / molette sur la barre | Changer de bureau |
| Clic sur `SON` / molette dessus | Couper le son / régler le volume |
| Clic sur le réseau | Ouvre `iwctl` (wifi) |
| Clic sur `AZERTY` | Change de disposition |
| Dock : clic / clic milieu | Affiche l'appli (ou la lance) / nouvelle fenêtre |
| `Super + x` | Poste de pilotage : terminal plein écran + système, réseau, fichiers, clavier |
| `Super + Entrée` | Terminal (plume-term) |
| `Super + Maj + Entrée` | Terminal de secours (foot) |
| `Super + d` | Lanceur d'applications |
| `Super + b` | Firefox |
| `Super + e` | Gestionnaire de fichiers |
| `Super + Maj + q` | Fermer la fenêtre |
| `Super + 1…0` | Changer de bureau |
| `Super + Maj + 1…0` | Envoyer la fenêtre sur un bureau |
| `Super + flèches` | Changer de fenêtre |
| `Super + f` | Plein écran |
| `Alt + Maj` | Basculer **AZERTY ⇄ QWERTY** |
| `Super + Espace` | Saisie chinoise **pinyin** activée / désactivée |
| `Super + Maj + Espace` | Fenêtre flottante |
| `Super + w` | Fenêtres en onglets |
| `Super + r` | Redimensionner (flèches, puis Échap) |
| `Super + Échap` | **Verrouiller l'écran** |
| `Super + Maj + e` | Éteindre, redémarrer, veille ou déconnexion |
| `Impr` | Capture d'une zone dans le presse-papiers |
| `Maj + Impr` | Capture d'écran dans un fichier de ton dossier |
| Touches volume et luminosité | Fonctionnent directement |

Fermer le capot = mise en veille. L'écran se verrouille après 5 min et s'éteint après 10.

### Écran de connexion et de verrouillage (plume-hud)

Le même HUD sert au démarrage (connexion) et au verrouillage :

- **5 panneaux** : opérateur et système (CPU, RAM, uptime), horloge et mot de passe, énergie et réseau, calendrier, journal ;
- **mot de passe du compte** ; après **5 erreurs**, blocage de **30 s** ;
- **sécurisé** : même si le programme plante, la session reste verrouillée ;
- `Tab` passe de l'utilisateur au mot de passe (connexion), `Ctrl + U` efface le champ.

Voir `hud/README.md` pour les détails.

### Terminal (plume-term)

Terminal maison au style HUD, sans Electron : la fenêtre, les onglets, l'historique, la sélection
et le dessin sont écrits pour Plume ; le décodage des séquences du terminal (couleurs, curseur,
écran plein comme `htop` ou `micro`) est confié à **libvterm**, la petite bibliothèque utilisée par Neovim.

| Raccourci | Action |
|---|---|
| `Ctrl + Maj + T` / `+` dans la barre | Nouvel onglet (dans le même dossier) |
| `Ctrl + Maj + W` / `✕` / clic milieu sur l'onglet | Fermer l'onglet |
| `Ctrl + Tab`, `Ctrl + Page↑/↓`, `Alt + 1…9` | Changer d'onglet |
| `Ctrl + Maj + C` / `Ctrl + Maj + V` | Copier / coller (aussi `Maj + Inser`) |
| Glisser · double-clic · triple-clic | Sélectionner du texte · un mot · une ligne |
| Clic droit | Copier la sélection, sinon coller |
| Clic milieu | Coller la dernière sélection |
| Molette, `Maj + Page↑/↓`, `Maj + Début/Fin` | Remonter dans l'historique (5 000 lignes par onglet) |
| `Ctrl + plus` / `Ctrl + moins` / `Ctrl + 0` | Taille du texte |
| `Maj` + souris | Sélectionner même dans `htop`, `micro`… (qui captent la souris) |

Options : `plume-term -e commande`, `-D dossier`, `-f taille`, `-a app_id`, `-T titre`, `--hud` (voir `plume-term --help`).

### Poste de pilotage (plume-term --hud)

`Super + x`, `◈` dans le dock, ou `Ctrl + Maj + H` dans n'importe quel plume-term : le terminal en
plein écran à la façon d'eDEX-UI, mais en C natif.

| Panneau | Contenu |
|---|---|
| **Centre** | le terminal, avec ses onglets |
| **SYSTÈME** (gauche) | OS, noyau, temps de marche, batterie ; CPU (graphe + chaque cœur, température, charge) ; mémoire et swap ; processus les plus gourmands |
| **RÉSEAU** (droite) | interface, IPv4, passerelle, signal wifi, connexions TCP, débit ↓↑ en graphe, totaux ; **STOCKAGE** (`/`, `/home`, clés USB) ; infos du terminal |
| **FICHIERS** (bas) | explorateur qui **suit le dossier du terminal** : raccourcis (Perso, Bureau, Documents, Téléchargements, Images, Musique, Vidéos, Racine), fil d'Ariane cliquable, tailles (et dates sur grand écran), fichiers cachés ; **recherche** : clic dans le champ (ou `Ctrl + Maj + F`), tape pour filtrer, `Entrée` ouvre, ou va à un chemin tapé (`~/Images`, `/etc`) ; double-clic ouvre (texte → nouvel onglet avec micro, PDF → zathura, image → imv, vidéo/son → mpv) ; **cd ici** envoie le terminal dans le dossier affiché |
| **CLAVIER** (bas) | ta disposition réelle (AZERTY/QWERTY), les touches s'allument quand tu tapes, dernière touche et compteur de frappes ; cliquer une touche écrit dans le terminal (`⇧` et `CTRL` restent enfoncées pour la touche suivante, ex. `CTRL` puis `C`) |

Quand l'explorateur a le clavier : `↑ ↓` choisir · `Entrée` ouvrir · `⌫` (recherche vide) ou `←`
dossier parent · `Ctrl + H` fichiers cachés · `Échap` vider la recherche, puis rendre le clavier au
terminal (un clic dans le terminal le rend aussi).

Volontairement absent par rapport à eDEX-UI : le globe des connexions (il faudrait envoyer les
adresses IP à un service en ligne ou embarquer une base de plusieurs Mo), les sons et les thèmes.
Les touches mortes de l'AZERTY marchent (`^` puis `e` = `ê`).

### Écrire en chinois (pinyin)

1. `Super + Espace` : la barre affiche `拼音`.
2. Tape le pinyin, par exemple `nihao`.
3. Choisis les caractères avec `Espace` ou les chiffres : 你好.
4. `Super + Espace` pour revenir à la saisie normale.

### Logithèque Plume (installer des applications)

`▦` dans le dock, ou « Logithèque » dans le lanceur (`Super + d`). Comme GNOME Logiciels, au style HUD :

| Onglet | Ce qu'on y fait |
|---|---|
| **Explorer** | ~70 applications choisies, rangées par catégorie (Internet, Bureautique, Multimédia, Graphisme, Jeux, Développement, Utilitaires, Système), décrites en français ; **tape n'importe où pour chercher** : dans le catalogue, dans les ~28 000 paquets Alpine et dans Flathub |
| **Installées** | applications installées (Alpine et Flathub), avec **Ouvrir** et **Supprimer** ; les paquets dont Plume a besoin (socle Alpine, bureau) sont protégés |
| **Mises à jour** | télécharge la liste à jour, montre ce qui peut être mis à jour (système et Flathub), **Tout mettre à jour** |

- **Deux sources** : les paquets **Alpine** (légers, mot de passe administrateur demandé dans la fenêtre, gardé 5 min en mémoire, jamais enregistré) et **Flathub** (Steam, Discord, Spotify…, installés pour toi seul, sans mot de passe). Quand une appli existe des deux côtés, la fiche propose de choisir.
- **Flathub** s'active d'un clic (`Activer Flathub`) : installe Flatpak (~70 Mo) et ajoute le dépôt. Les applis Flathub apparaissent ensuite dans le lanceur.
- Le catalogue est un simple fichier texte : `logitheque/catalogue.txt` (installé dans `/usr/local/share/plume/logitheque.txt`).

### sudo et apt (comme sur Ubuntu)

Plume est basée sur Alpine, dont le gestionnaire de paquets s'appelle **apk**. Pour ne pas changer
d'habitudes (et suivre les tutos d'Internet), Plume fournit **`sudo`** (le vrai) et une commande
**`apt`** (aussi `apt-get` et `apt-cache`) qui traduit vers apk :

| Tu tapes | Plume fait |
|---|---|
| `sudo apt update` | `apk update` |
| `sudo apt upgrade` (aussi `full-upgrade`, `dist-upgrade`) | `apk upgrade -U` |
| `sudo apt install nom…` | `apk add nom…`, en traduisant les noms Debian : `build-essential` → `build-base`, `python3-xxx` → `py3-xxx`, `libssl-dev` → `openssl-dev`, `openssh-server` → `openssh`, `default-jdk` → `openjdk21`… |
| `sudo apt remove nom…` (aussi `purge`) | `apk del nom…` |
| `apt search mot` | cherche dans les noms puis les descriptions (les meilleurs résultats en premier) |
| `apt show nom` | fiche du paquet : version, installé ou non, taille, site, licence, dépendances |
| `apt list --installed` / `--upgradable` | paquets installés / à mettre à jour |
| `sudo apt autoremove` | rien à faire : apk retire tout seul les dépendances inutiles |

Les options `-y`, `-q`, `--no-install-recommends` sont acceptées (apk ne pose pas de question).
Un fichier `.deb` ne s'installe pas sur Plume : `apt` le dit et propose de chercher l'équivalent
(`apt search`) ou une version Flatpak. `apk` et `doas` restent disponibles pour qui les préfère.

### Commandes utiles

| Je veux… | Commande |
|---|---|
| Mettre à jour le système | `sudo apt update && sudo apt upgrade` (ou l'alias `maj`) |
| Chercher un logiciel | `apt search nom` |
| Installer / supprimer | `sudo apt install nom` / `sudo apt remove nom` |
| Wifi | `iwctl` (interactif) ou `iwctl station wlan0 connect "SSID"` |
| Volume | `wpctl set-volume @DEFAULT_AUDIO_SINK@ 50%` |
| Choisir la sortie audio | `wpctl status`, puis `wpctl set-default <id>` |
| Bluetooth | `sudo apt install bluez bluez-openrc && sudo rc-update add bluetooth && sudo rc-service bluetooth start`, puis `bluetoothctl` |
| Clé USB | `sudo mount /dev/sdb1 /mnt` (ou pcmanfm + `sudo apt install udisks2 gvfs`) |
| Imprimer | `sudo apt install cups cups-openrc && sudo rc-update add cupsd` |
| Applis absentes d'Alpine | `sudo apt install flatpak` (attention : beaucoup plus lourd) |
| Services | `rc-status`, `sudo rc-service <nom> restart` |
| Journaux | `sudo tail -f /var/log/messages` |

Nouvelle version d'Alpine : remplace `v3.XX` dans `/etc/apk/repositories`, puis
`sudo apk upgrade -U --available` et redémarre.

---

## 11. Personnaliser la distro

- **Paquets** : `rootfs/etc/plume/packages/*.list` (un nom par ligne, `#` pour un commentaire).
- **Bureau** : `rootfs/etc/skel/.config/sway/config` (raccourcis, couleurs, claviers). Une fois installé, chaque utilisateur a sa copie dans `~/.config/sway/config`.
- **Dock** : `/etc/plume/dock.conf` → copie-le dans `~/.config/plume/dock.conf` (`glyphe | nom | commande | app_id`), puis `swaymsg reload`.
- **Fond d'écran** : `plume-wall ~/fond.png 1920 1080`, puis `output * bg ~/fond.png fill` dans la config sway.
- **Couleurs** : la palette du HUD est commune à `hud/draw.c`, `shell/draw.c`, `hud/wall.c` et aux fichiers de `rootfs/etc/skel/.config/`.
- **Autres claviers** : `xkb_layout fr,us` devient par ex. `fr,us,de`. Japonais : `ibus-anthy`, coréen : `ibus-hangul`.
- **Nom de la distro** : `title`, `profile_abbrev` et `hostname` dans `build/mkimg.plume.sh`, et `rootfs/etc/motd`.
- **Contenu de l'ISO** : variable `apks` dans `build/mkimg.plume.sh`.
- Après chaque modification : `make iso`, puis `make test`.

---

## 12. Mesurer la légèreté

```sh
df -h /                                   # espace disque utilisé
free -m                                   # RAM utilisée
apk info | wc -l                          # nombre de paquets installés
cat /etc/apk/world                        # paquets demandés (sans les dépendances)
rc-status                                 # services lancés
ps -o pid,rss,comm | sort -k2 -n | tail   # processus les plus gourmands
```

---

## 13. Dépannage

| Problème | Solution |
|---|---|
| La clé ne démarre pas | Désactive Secure Boot, essaie l'autre mode (UEFI / Legacy) |
| Pas de réseau pendant l'installation | `setup-interfaces`, puis `rc-service networking restart` ; en wifi, vérifie que la carte est vue : `ip link` |
| `Login incorrect` pour ton utilisateur | Connecte-toi en `root`, puis `passwd tonlogin` |
| sway ne démarre pas | Lance `sway` à la main depuis tty1 pour voir l'erreur ; vérifie `groups` (`seat`, `video`, `input`) et `rc-service seatd status` |
| Écran noir en VM | `WLR_RENDERER=pixman sway` |
| Clavier ou souris inactifs sous sway | `sudo setup-devd udev`, puis redémarre |
| Pas de son | `ps \| grep pipewire` ; vérifie `echo $XDG_RUNTIME_DIR` et le groupe `audio` |
| Écran de connexion noir ou absent | `Ctrl + Alt + F1` pour la connexion texte, puis `sudo rc-service greetd restart` ; journal : `sudo cat /var/log/messages \| grep -i greet` |
| Le terminal ne s'ouvre pas | `Super + Maj + Entrée` ouvre foot ; puis `plume-term` dedans pour voir l'erreur |
| Le verrouillage ne s'affiche pas | `plume-hud --dev` pour tester ; à défaut, sway utilise swaylock automatiquement |
| Le pinyin ne s'active pas | `ps \| grep ibus-daemon` ; relance avec `ibus-daemon -drx` |
| Wifi absent | `sudo rc-service iwd restart`, puis `iwctl` ; si la carte n'apparaît pas : `sudo apt install linux-firmware` |
| Plus de wifi après `slim-firmware.sh` | Branche un câble, `sudo apt install linux-firmware`, redémarre, relance le script avec le wifi **actif** |
| Luminosité bloquée | Vérifie le groupe `video`, puis reconnecte-toi |
| Une appli X11 ne s'ouvre pas | `sudo apt install xwayland` |
| Programme compilé pour glibc | `sudo apt install gcompat`, sinon Flatpak |
| Plus de son après une mise à jour d'Alpine | Alpine prévoit de retirer `/usr/libexec/pipewire-launcher` : adapte la ligne correspondante de la config sway selon le wiki PipeWire d'Alpine |

---

## 14. Feuille de route

🔴 urgent · 🟠 important · 🟢 plus tard

### Corrections à faire

| Priorité | Problème | Détails |
|---|---|---|
| 🟠 | **plume-hud : le clavier ne répond plus s'il est débranché puis rebranché pendant l'écran de connexion ou de verrouillage** | **Quand ça arrive :** sur un PC avec un clavier USB (ou sans fil avec dongle), si le clavier est débranché puis rebranché, ou se met en veille puis se reconnecte, alors que l'écran de connexion ou de verrouillage est affiché. Un portable n'est pas concerné tant qu'on utilise son clavier intégré, qui ne disparaît jamais.<br>**Ce qu'on voit :** l'écran reste affiché mais les frappes ne font plus rien : impossible de taper son mot de passe, donc de se connecter ou de déverrouiller. La souris peut avoir le même problème.<br>**Pourquoi :** quand le dernier clavier disparaît, le compositeur (sway ou cage) annonce qu'il n'y a « plus de clavier », puis « de nouveau un clavier » au rebranchement. Une appli Wayland doit alors abandonner l'ancien objet clavier, devenu inerte, et en demander un nouveau. plume-hud garde l'ancien et n'en redemande jamais (`seat_caps` dans `hud/main.c`).<br>**Contournement en attendant (non testé) :** relancer plume-hud, car une nouvelle copie récupère le clavier rebranché. `Ctrl + Alt + F2`, se connecter en mode texte, puis : écran de connexion → `sudo rc-service greetd restart` ; écran de verrouillage → `pkill plume-hud && WAYLAND_DISPLAY=wayland-1 plume-hud --lock &`, puis revenir sur l'écran de sway (`Ctrl + Alt + F7` avec greetd, `F1` sinon). Attention : tuer plume-hud sans le relancer laisse la session verrouillée sans rien pour taper le mot de passe (c'est voulu, pour la sécurité). En dernier recours : redémarrer (le travail non enregistré est perdu).<br>**Correction :** une dizaine de lignes, comme celle déjà faite dans plume-term : libérer l'ancien clavier ou l'ancienne souris quand ils disparaissent, en redemander un quand ils reviennent. Rien ne change à l'apparence. |

### Stabilité et matériel

| Priorité | Tâche |
|---|---|
| 🔴 | Tester sur un vrai PC : wifi, veille, son, luminosité, touches multimédia |
| 🔴 | Tester le démarrage UEFI |
| 🟢 | Installation 100 % automatique (fichier de réponses complet) |
| 🟢 | Gestion du wifi en mode graphique (`iwgtk`) |

### Sécurité et mises à jour

| Priorité | Tâche |
|---|---|
| 🟠 | **Sécuriser la distro** : pare-feu activé par défaut (nftables : tout bloquer en entrée sauf ce qui est demandé), chiffrement du disque en option dans `install.sh` (`setup-disk -e`), services réduits au strict nécessaire, réglages noyau durcis (`sysctl`) |
| 🟠 | **Mises à jour simples** : une commande et un bouton dans la barre pour tout mettre à jour (système, applis Flatpak, outils Plume), avec une notification quand des mises à jour de sécurité sont disponibles |
| 🟢 | Dépôt `.apk` Plume pour mettre à jour les configs et les outils maison avec `apk upgrade` |

### Applications

| Priorité | Tâche |
|---|---|

### Jeux vidéo

| Priorité | Tâche |
|---|---|
| 🟠 | **Jeux PC** : Steam (via Flatpak, car Steam a besoin de glibc et Alpine utilise musl), Epic Games et GOG via **Heroic Games Launcher** ; jeux GOG installables aussi en local, hors ligne ; pilotes graphiques et Vulkan, manettes |
| 🟢 | **Émulateurs de consoles** : RetroArch et des émulateurs dédiés (PlayStation, Nintendo…), avec les manettes configurées automatiquement |

### Bureau et apparence

| Priorité | Tâche |
|---|---|
| 🟠 | **Changer de fond d'écran** facilement : plusieurs fonds Plume au choix, ou une image perso, depuis une fenêtre de réglages (le fond est aussi utilisé par l'écran de connexion) |
| 🟢 | **Mode « Windows » avec `Ctrl + 9`** : bascule le bureau vers une disposition familière pour qui vient de Windows (barre des tâches en bas, menu Démarrer, fenêtres flottantes avec boutons fermer/agrandir, raccourcis Windows), puis retour au mode Plume avec le même raccourci. À vérifier : `Ctrl + 9` est déjà utilisé par les navigateurs (aller au dernier onglet) |
| 🟢 | Identité visuelle : thèmes de couleurs au choix |

### Organisation du projet

| Priorité | Tâche |
|---|---|
| 🟢 | **Un dépôt par composant** : séparer `plume-hud` (connexion, verrouillage), `plume-shell` (barre, dock), `plume-term` (terminal, poste de pilotage) et les futurs outils dans des dépôts Git distincts, chacun avec son README et ses versions ; ce dépôt-ci ne garderait que la construction de la distro, qui les assemble |

### Ressources

- Wiki Alpine : https://wiki.alpinelinux.org
- Sway sur Alpine : https://wiki.alpinelinux.org/wiki/Sway
- PipeWire : https://wiki.alpinelinux.org/wiki/PipeWire
- iwd : https://wiki.alpinelinux.org/wiki/Iwd
- ISO personnalisée : https://wiki.alpinelinux.org/wiki/How_to_make_a_custom_ISO_image_with_mkimage
- Recherche de paquets : https://pkgs.alpinelinux.org
