# Plume Linux 🪶

> Une distribution Linux **la plus légère possible**, mais **complète pour un usage quotidien**.
> Deux versions, le même bureau : **Plume Alpine** (musl) et **Plume Void** (glibc).

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
| Fond d'écran | **plume-wall** (maison) : grille, anneaux HUD, logo PLUME ; **plume-fonds** (maison, ~80 Ko) pour en changer : 6 autres fonds Plume, couleurs unies ou tes photos |
| Poste de pilotage | **plume-term --hud** : le terminal en plein écran, entouré des performances (CPU, cœurs, RAM, swap, réseau, disques, processus), d'un explorateur de fichiers avec recherche et d'un clavier à l'écran — `Super + x` ou `◈` dans le dock |
| Mises à jour | **plume-maj** : la barre vérifie toute seule (2 min après l'ouverture de session, puis toutes les 6 h) et affiche **⬆ N MAJ** + une notification ; un clic ouvre la liste (ancienne → nouvelle version), puis met à jour les paquets Alpine **et** les applis Flathub ; prévient s'il faut redémarrer (nouveau noyau). En terminal : `plume-maj`, `plume-maj --liste`, `plume-maj -y` |
| Son | **plume-son** (maison) : clic sur SON dans la barre → sortie et micro par défaut, volume de chaque appareil et de chaque application, muet ; clic du milieu = muet, molette = volume |
| Bluetooth | **plume-bluetooth** (maison) : clic sur ᛒ BT dans la barre (si la machine a le Bluetooth) → appareils associés (connecter, déconnecter, oublier, batterie) et à proximité (associer en un clic : association, confiance, connexion) |
| Wifi | **plume-wifi** (maison, aucune dépendance en plus) : clic sur le réseau dans la barre → réseaux visibles avec leur signal, mot de passe, réseaux connus, déconnexion, wifi activé/désactivé ; parle à **iwd** par `iwctl` |
| Logithèque | **plume-logitheque** (maison, ~120 Ko) : installer et désinstaller des applications comme GNOME Logiciels — catalogue en français par catégories, recherche dans tous les paquets Alpine et Flathub, mises à jour ; `▦` dans le dock ou « Logithèque » dans le lanceur |
| Terminal | **plume-term** (maison, ~150 Ko + libvterm ~100 Ko) : terminal HUD avec onglets, historique, copier/coller, couleurs complètes, et mode poste de pilotage — **foot** reste installé en secours (`Super + Maj + Entrée`) |
| Toutes les applications | **plume-lanceur** (maison, ~75 Ko) : grille plein écran à la GNOME, vraies icônes, recherche, pages — `Super + d` / `Super + a`, `◆ PLUME` ou `⣿` dans le dock ; **fuzzel** en secours |
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
| `plume-lanceur` (grille des applications) | ✅ Testé en VM (ouverture, fermeture) et dans un sway virtuel (`tests/lanceur-preview.sh`) : vraies icônes PNG/SVG, noms en français, recherche, `Entrée` et clic lancent l'appli, `Échap` et 2ᵉ appel ferment — changement de page pas encore essayé (trop peu d'applis) |
| Couleur du thème (`plume-theme`) | 🟡 Testé dans un sway virtuel : changement en direct, choix enregistré ; les 5 applis Plume compilées avec le thème, installées et lancées en VM |
| Pack émulation (`plume-emulation`) | 🟡 Installation Alpine testée dans un Alpine vierge (tous les paquets existent, groupe `input`) ; partie Flathub et vrais jeux non testés |
| Installation 100 % automatique (`install.sh --auto`) | ✅ Testée en VM (câble, aucune question) ; wifi et installation du bureau au premier démarrage pas encore testés |
| Dépôt `.apk` Plume (`make depot`) | 🟡 11 paquets signés (plume-hud, plume-shell, plume-term, plume-lanceur, plume-fonds, plume-wifi, plume-son, plume-bluetooth, plume-logitheque, plume-outils, méta-paquet plume-bureau). Testé dans un conteneur : `apk add plume-bureau` installe tout avec les dépendances, et une version plus récente du dépôt arrive avec `apk upgrade` (r7 → r99). Embarqué dans l'ISO, utilisé par desktop.sh (compilation sur place en secours) |
| `plume-son`, `plume-bluetooth` | 🟡 Testés dans un sway virtuel avec de faux wpctl / bluetoothctl (`tests/panneaux-preview.sh`, 11 vérifications) ; pas encore sur du vrai matériel |
| AppArmor | 🟡 Activé par desktop.sh (ligne de démarrage + service) ; vérification en VM ajoutée au test |
| Secure Boot (`plume-secureboot`) | 🟡 Préparation testée dans un conteneur (clés sbctl, image UEFI signée et vérifiée par sbverify) ; inscription des clés et démarrage réel non testés (il faut un PC UEFI) |
| `plume-maj` (mises à jour) | 🟡 Testé dans un conteneur Alpine 3.24.0 : 7 mises à jour trouvées et listées, `-y` les installe, compte remis à 0, vérification sans mot de passe autorisée et toute autre commande refusée ; indicateur de la barre compilé mais pas encore vu en VM |
| `plume-wifi` (panneau wifi) | 🟡 Testé dans un sway virtuel avec un faux iwd (`tests/wifi-preview.sh`) : liste, mauvais puis bon mot de passe, réseau connu, déconnexion, wifi désactivé ; pas encore sur une vraie carte wifi |
| `plume-fonds` (fond d'écran) | 🟡 Testé dans un sway virtuel (`tests/fonds-preview.sh`) : fonds Plume, mode Ajuster, photo JPEG, choix gardé après rechargement de sway, image supprimée sans erreur ; testé en VM (fonds générés, fenêtre, réapplication au démarrage) |
| Logithèque Plume | 🟡 Testé dans un sway virtuel (`tests/logitheque-preview.sh`) : catalogue, recherche Alpine + Flathub, mauvais puis bon mot de passe, installation et suppression d'un paquet Alpine, protection des paquets vitaux, mises à jour, activation de Flathub — installation d'une appli Flathub pas encore testée (plusieurs centaines de Mo) ; test VM en cours |
| `plume-term` (terminal) | ✅ Testé en VM (compilé sur la machine, lancé depuis sway) et dans un sway virtuel (`tests/term-preview.sh`) : couleurs 16/256/24 bits, styles, accents et touches mortes, chinois, cadres, htop, historique, onglets, copier/coller, sélection à la souris, zoom |
| `plume-term --hud` (poste de pilotage) | ✅ Testé en VM (mesures réelles de la machine) et dans un sway virtuel avec souris et clavier virtuels : raccourcis, recherche, ouverture de fichier, « cd ici », fil d'Ariane, fichiers cachés, clavier à l'écran ; touches qui s'allument vérifiées à la main avec un vrai clavier (Mac, via VNC) ; ~35 Mo de RAM, ~1 à 2 % d'un cœur au repos |
| Wifi, veille, luminosité, touches multimédia | ⚪ Codé, à tester sur un vrai PC |
| Démarrage UEFI | ⚪ À tester |
| Chiffrement du disque | ⚪ À faire |

### Plume Void (glibc)

| Partie | État |
|---|---|
| Dépôt `.xbps` Plume signé (`make depot-void`) | ✅ 12 paquets compilés sous glibc (les 11 d'Alpine + la police JetBrains Mono, absente de Void) |
| ISO live (`make iso-void`) | ✅ 1,6 Go, bureau inclus |
| Démarrage du live (BIOS), utilisateur `plume` avec les groupes du bureau | ✅ Testé en VM |
| Installation automatique (`install.sh --auto`) : partitions BIOS + UEFI, copie du live, dracut, GRUB | ✅ Testé en VM (bugs trouvés et corrigés : fuseau horaire, partition BIOS non reconnue par GRUB → `sgdisk`) |
| Démarrage du système installé (BIOS), connexion, glibc | ✅ Testé en VM |
| Services runit : dbus, seatd, greetd, nftables, iwd, bluetoothd, zramen | ✅ Testé en VM |
| Écran de connexion `plume-hud` (greetd + cage) | ✅ Testé en VM (capture d'écran) |
| Pare-feu, AppArmor, sysctl | ✅ Testé en VM |
| `plume-maj --verifier` sans mot de passe, `apt` → xbps, `plume-parefeu`, Logithèque, police, Internet | ✅ Testé en VM |
| Disque chiffré LUKS2 | ✅ Testé en VM (phrase demandée au démarrage, racine chiffrée, toutes les vérifications OK) |
| Session sway ouverte : barre, dock, son (PipeWire), terminal, lanceur | ✅ Vue en VM (captures d'écran) : sway 1.12, ~420 Mo de RAM avec le terminal ouvert ; texte du fond d'écran (« VOID LINUX ») et raccourcis Foot en double corrigés |
| Profils AppArmor Plume : Firefox, mpv, imv, Zathura | ✅ Testé en VM : 4 profils en mode enforce ; Firefox démarre confiné ; une appli confinée lit tes documents mais pas `~/.ssh`, ne peut ni modifier `.bashrc` ni exécuter un fichier téléchargé ; Zathura (PDF) n'a pas Internet |
| `plume-secureboot` (UKI dracut signée), pack émulation | 🟡 Codés, pas testés |
| Pack jeux (`plume-jeux`) : Steam natif + 32 bits, Heroic (Epic, GOG), Vulkan ; manettes (`plume-manette`) ; émulateurs Switch et PS4 | 🟡 Testé dans un Void vierge : tous les paquets existent (plus de 200 à installer avec le 32 bits), applis Flathub vérifiées (Heroic, ProtonUp-Qt, Ryujinx, shadPS4), règles manettes et groupe `input` posés ; vrais jeux et vraies manettes non testés |
| UEFI, wifi, son, Bluetooth, veille sur un vrai PC | ⚪ À tester |
| `slim-firmware.sh` (alléger les firmwares) | ✅ Testé en VM : 667 Mo → 1 Mo ; seuls les fichiers utiles à ton matériel restent, et des règles `noextract` de xbps empêchent les mises à jour de remettre les autres (`--restaurer` remet tout). Bug trouvé et corrigé : noms de firmwares avec espaces |

---

## 💿 Deux versions : Alpine ou Void

| | **Plume Alpine** | **Plume Void** |
|---|---|---|
| Base | Alpine Linux 3.24 | Void Linux (en continu) |
| Bibliothèque C | musl | **glibc** |
| Démarrage des services | OpenRC | runit |
| Paquets | apk + dépôt `.apk` Plume | xbps + dépôt `.xbps` Plume |
| ISO | ~380 Mo, bureau installé au 1er démarrage (Internet) | ~1,5 Go, **bureau déjà dedans** : essai en live, installation sans Internet |
| Pour qui | le plus léger possible | compatibilité : logiciels binaires, jeux (Steam), pilotes NVIDIA |

Même bureau HUD, mêmes outils (`plume-maj`, `plume-parefeu`, `plume-secureboot`, `apt`,
Logithèque…), même installation automatique (`install.sh --auto`), même disque chiffré
LUKS2 en option. Les sources C sont communes ; ce qui est propre à Void est dans `void/`.

### Télécharger

Les deux ISO sont sur la page **Releases** du dépôt GitHub :
https://github.com/ESomnard/alpine-linux-based-on-distro/releases/latest

- `plume-alpine-…-x86_64.iso` : connexion `root` (sans mot de passe), puis `sh /etc/plume/install.sh` ;
- `plume-void-…-x86_64.iso` : session live `plume` / `voidlinux`, puis dans un terminal
  `sudo sh /etc/plume/install.sh` ;
- `SHA256SUMS` : `sha256sum -c SHA256SUMS` pour vérifier le téléchargement.

### Construire et publier (mainteneur)

```sh
make isos            # les deux ISO dans out/ (Docker)
make test test-void  # installation complète automatique de chacune dans une VM
make release         # out/release/ : ISO renommées, SHA256SUMS, notes ; affiche la commande gh
./build/publier-depot.sh   # dépôts .apk et .xbps signés -> branche gh-pages
```

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
├── fonds/                        # plume-fonds : choisir le fond d'écran (+ 6 fonds Plume générés)
│   └── main.c ui.c images.c gen.c fonds.h Makefile
├── son/                          # plume-son : panneau son HUD (PipeWire par wpctl)
├── bluetooth/                    # plume-bluetooth : panneau Bluetooth HUD (bluez par bluetoothctl)
├── build/publier-depot.sh        # prépare la publication du dépôt .apk sur GitHub Pages
├── paquets/VERSION               # version des paquets .apk Plume (make depot)
├── wifi/                         # plume-wifi : panneau wifi HUD (iwd par iwctl)
│   └── main.c ui.c iwd.c wifi.h Makefile
├── lanceur/                      # plume-lanceur : toutes les applications en grille (à la GNOME)
│   └── main.c apps.c draw.c lanceur.h Makefile
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
│   ├── fonds-preview.sh          # teste le choix du fond d'écran (clics, photos, mémorisation)
│   ├── lanceur-preview.sh        # teste la grille des applications (recherche, lancement, fermeture)
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

> **Installation 100 % automatique** : copie `/etc/plume/install-auto.conf` (sur la clé, ou dans le live),
> remplis-le (disque, identifiant, mot de passe, wifi éventuel), puis `sh /etc/plume/install.sh --auto ton-fichier.conf`.
> Aucune question : le disque est effacé, le système installé, et au premier démarrage le bureau s'installe
> tout seul (journal : `/var/log/plume-installation.log`), puis la machine redémarre.
> Ce fichier contient ton mot de passe en clair : supprime-le après usage.


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
7. **compile `plume-hud`** (connexion + verrouillage), **`plume-shell`** (barre + dock), **`plume-term`** (terminal + poste de pilotage), **`plume-lanceur`** (grille des applications), **`plume-fonds`** (fond d'écran, avec ses 6 fonds), **`plume-wifi`** (panneau wifi), la **Logithèque** et **`plume-wall`** (fond d'écran), puis **supprime les outils de compilation** ;
   crée aussi les dossiers Bureau, Documents, Téléchargements, Images, Musique, Vidéos ;
8. active **greetd**, qui affiche le HUD de connexion au démarrage ;
9. bascule le wifi vers **iwd**, en dernier pour ne pas couper l'installation.

Au redémarrage, **l'écran de connexion HUD** s'affiche : tape ton mot de passe, sway démarre.
Connexion texte de secours : `Ctrl + Alt + F1`.

Si tu étais en wifi, reconnecte-toi une fois : **clic sur le réseau dans la barre**, choisis
ton réseau, tape le mot de passe, Entrée. iwd s'en souviendra. En texte, avec iwctl :
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
| Clic sur `◆ PLUME` ou `⣿` dans le dock | Toutes les applications (grille) |
| Clic sur un bureau / molette sur la barre | Changer de bureau |
| Clic sur `SON` / molette dessus | Couper le son / régler le volume |
| Clic sur le réseau | Ouvre le panneau wifi (`plume-wifi`) |
| Clic sur `AZERTY` | Change de disposition |
| Dock : clic / clic milieu | Affiche l'appli (ou la lance) / nouvelle fenêtre |
| `Super + x` | Poste de pilotage : terminal plein écran + système, réseau, fichiers, clavier |
| `Super + Entrée` | Terminal (plume-term) |
| `Super + Maj + Entrée` | Terminal de secours (foot) |
| `Super + d` ou `Super + a` | Toutes les applications (grille) ; encore une fois pour fermer |
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

### Sécurité

| Protection | Ce qu'elle fait | Comment s'en servir |
|---|---|---|
| **Pare-feu** (nftables, actif par défaut) | Rien n'entre depuis le réseau, sauf les réponses à ce que tu as demandé. Tout ce qui sort est permis : navigation, jeux en ligne et mises à jour marchent normalement. | `plume-parefeu` (état) · `sudo plume-parefeu ouvrir 22` · `sudo plume-parefeu ouvrir 1714-1764/udp` · `sudo plume-parefeu fermer 22` · `desactiver` / `activer` |
| **Noyau durci** (`/etc/sysctl.d/60-plume.conf`) | Cache les adresses et le journal du noyau. Interdit d'espionner un autre programme (ptrace), réserve eBPF à root, bloque kexec. Refuse les redirections réseau et les adresses usurpées, et résiste aux inondations SYN. Flatpak, Steam et Docker marchent toujours. | Rien à faire |
| **Disque chiffré** (LUKS2, en option) | Sans ta phrase secrète, personne ne lit tes fichiers, même avec le PC volé ou le disque retiré. Elle est demandée à chaque démarrage, avant l'écran de connexion. **Oubliée = données perdues.** | À l'installation : réponds `o` à « Chiffrer le disque », ou mets `CHIFFREMENT="oui"` + `PHRASE_CHIFFREMENT` dans le fichier de `--auto` |
| **Mises à jour** | La barre affiche **⬆ N MAJ** dès que des correctifs sont publiés, et **★ 3.xx** quand une nouvelle version d'Alpine sort. | Clic sur ⬆ ou ★, ou `plume-maj` / `plume-maj --nouvelle-version` |
| **AppArmor** | Confinement des programmes par profils, activé au démarrage (`lsm=…,apparmor`). Les profils fournis par Alpine visent surtout des serveurs ; Firefox, Chromium, Discord… y sont pour l'instant « non confinés ». **Plume Void** a ses propres profils pour Firefox, mpv, imv et Zathura (`/etc/apparmor.d/plume-*`) : l'appli garde accès à tes documents, mais pas à tes secrets (`~/.ssh`, `~/.gnupg`, mots de passe), ne peut pas s'installer au démarrage (`.bashrc`, config sway) ni exécuter un fichier téléchargé ; Zathura et imv n'ont pas Internet. Si une appli bloque : `/etc/apparmor.d/local/plume-<appli>`. Le profil de `unix_chkpwd` est désactivé par précaution : il vérifie le mot de passe de l'écran de verrouillage et n'a pas été éprouvé avec musl. Coût : Python 3 (~45 Mo), exigé par le paquet `apparmor` d'Alpine. | État : `cat /sys/module/apparmor/parameters/enabled` (Y) · retirer : `sudo apk del apparmor apparmor-profiles` |
| **Démarrage sécurisé** (Secure Boot, UEFI, en option) | Tes propres clés. Le noyau, l'initramfs et la ligne de démarrage forment une seule image signée, refaite et re-signée à chaque mise à jour du noyau. Les clés Microsoft sont gardées (cartes graphiques, Windows en double démarrage). | `plume-secureboot` (état) · `sudo plume-secureboot activer` (firmware en « mode configuration ») · `sudo plume-secureboot preparer` |
| **Pas de root direct** | En installation auto, le compte root est verrouillé : tout passe par `sudo` et ton mot de passe. Aucun serveur (SSH…) n'écoute. | — |

### Apparence : fond d'écran et couleur du thème (plume-fonds)

« Apparence » dans la grille des applications (`Super + a`) :

- **Couleur du thème** : Cyan, Émeraude, Violet, Ambre, Rouge ou Rose. Change d'un clic la barre, le dock,
  le terminal, la grille des applications, la Logithèque, les bordures des fenêtres, foot, fuzzel et les
  notifications (en ligne de commande : `plume-theme rouge`). L'écran de connexion reste cyan.

- **Fonds Plume** : le fond HUD d'origine et 6 autres (Horizon, Aurore, Constellation, Circuit,
  Topographie, Nébuleuse), générés à l'installation (~4 Mo) ;
- **Couleurs unies** ;
- **Mes images** : tes photos (JPEG, PNG, WebP…) trouvées dans Images, Bureau et Téléchargements ;
- affichage : **Remplir**, **Ajuster**, **Centrer** ou **Mosaïque**.

Un clic applique tout de suite. Le choix est gardé dans `~/.config/plume/fond.conf` et réappliqué
au démarrage ; si l'image a été supprimée, Plume reprend son fond d'origine (sans erreur).
L'écran de connexion et de verrouillage garde son décor HUD.

### Jeux vidéo (pack « plume-jeux », Plume Void)

Sur **Plume Void**, une seule commande installe tout ce qu'il faut pour jouer :

```sh
plume-jeux --liste             # ce qui sera installé
sudo plume-jeux                # jeux PC + manettes + émulateurs (plusieurs Go)
sudo plume-jeux --pc           # seulement Steam, Epic, GOG, pilotes et manettes
sudo plume-jeux --emulateurs   # seulement les émulateurs
```

| Pour | Ce qui est installé |
|---|---|
| **Steam** | Paquet natif Void (dépôts nonfree + multilib 32 bits activés). Jeux Windows : Proton (Steam > Paramètres > Compatibilité) |
| **Epic Games, GOG, Amazon** | **Heroic Games Launcher** (Flathub). Les installateurs GOG téléchargés (hors ligne) s'importent dans Heroic |
| **Jeux Windows en plus** | ProtonUp-Qt : versions Proton-GE / Wine-GE pour Steam et Heroic |
| **Graphismes** | Vulkan + Mesa 64 et 32 bits (Intel, AMD), XWayland, GameMode, MangoHud, gamescope. NVIDIA : `plume-jeux` donne la commande du pilote officiel |
| **Manettes** | `plume-manette` (ci-dessous) |

#### Manettes (plume-manette)

Xbox, PlayStation (DualShock 4, DualSense), Switch (Pro, Joy-Con), Steam, 8BitDo, PowerA, Hori…
en USB et en Bluetooth, avec vibrations, gyroscope et pavé tactile.

```sh
plume-manette                  # manettes branchées (USB / Bluetooth) et batterie
plume-manette --appairer       # appairer une manette Bluetooth (explique le bouton à tenir)
plume-manette --tester 0       # tester boutons et sticks
plume-manette --verifier       # diagnostic : pilotes, droits, règles
sudo plume-manette --installer # règles d'accès, uinput, outils (fait par plume-jeux)
```

Les pilotes sont ceux du noyau (`xpad`, `hid-playstation`, `hid-nintendo`, `hid-steam`…).
`plume-manette --installer` donne au groupe `input` l'accès aux manettes (`hidraw`, pour les
vibrations et le gyroscope) et à `uinput` (manettes virtuelles de Steam Input), et ajoute un outil
pour configurer les boutons : AntiMicroX.

#### Émulateurs (pack « meilleur émulateur par console »)

```sh
plume-emulation --liste        # quel émulateur pour quelle console
sudo plume-emulation           # paquets de la distribution : NES, Game Boy/GBA, DS, PS1, N64, GameCube/Wii, PSP (~1 Go)
sudo plume-emulation --tout    # + Flathub : SNES, PS2, PS3, Wii U, 3DS, Xbox, Dreamcast (+ PS4 et Switch sur Plume Void)
```

| Console | Émulateur |
|---|---|
| Game Boy / Color / Advance | mGBA |
| Wii / GameCube | Dolphin |
| Nintendo 3DS | Azahar |
| Nintendo Switch | Ryujinx (Ryubing) |
| PlayStation 3 | RPCS3 |
| PlayStation 4 | shadPS4 (encore jeune : une partie des jeux seulement) |
| PlayStation 5 | **aucun** : aucun émulateur ne lance de jeux PS5 aujourd'hui (les « émulateurs PS5 » trouvés en ligne sont des arnaques) |

Chaque émulateur est aussi dans la Logithèque (catégorie Jeux). Les jeux, BIOS, clés et firmwares
ne sont pas fournis : utilise uniquement tes propres copies (Switch, PS3, PS4 : fichiers système de
ta console).

### Toutes les applications (plume-lanceur)

`Super + a` (comme GNOME), `Super + d`, `◆ PLUME` dans la barre ou `⣿` au bout du dock : toutes les
applications installées (Alpine, Flathub, les tiennes) en **grille plein écran**, avec leurs icônes.

- **Tape pour chercher** (nom, description, mots-clés, en français si l'appli est traduite) ; les applis
  que tu lances souvent passent en premier dans les résultats ;
- `Entrée` ou clic : lancer · flèches : choisir · `Page↑ / Page↓`, molette ou points en bas : changer de page ;
- `Échap`, clic dans le vide, clic droit, ou le même raccourci : fermer.

### Logithèque Plume (installer des applications)

`▦` dans le dock, ou « Logithèque » dans la grille des applications (`Super + a`). Comme GNOME Logiciels, au style HUD :

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
| ✅ | **plume-hud : clavier débranché puis rebranché** (corrigé) | L'écran de connexion et de verrouillage abandonne l'ancien clavier ou l'ancienne souris quand ils disparaissent et en reprend un neuf au retour. Testé (`tests/hud-cpu-inside.sh`) : un 1er clavier tape un mauvais mot de passe puis disparaît, un 2e tape le bon et déverrouille ; l'ancienne version restait bloquée. |
| ✅ | **plume-hud : processeur** (corrigé) | Il redessinait tout l'écran 20 fois par seconde, même écran éteint. Il attend maintenant que l'image soit affichée, et ralentit à 5 images/s après 8 s sans frappe. Mesuré en 1920×1080 (rendu logiciel) : **42 % → 11 %** écran allumé, **42 % → 0 %** écran éteint, apparence identique. |

### Ce qu'Ubuntu a en plus (à ajouter à Plume Void)

Comparaison avec Ubuntu : ce qui manque vraiment au quotidien.

| Priorité | Manque | Ce qui coince sans | Ce qu'il faut |
|---|---|---|---|
| 🔴 | **Clés USB et disques externes** | Une clé branchée ne s'ouvre pas dans le gestionnaire de fichiers | udisks2, polkit, gvfs, montage automatique |
| 🔴 | **Portails Wayland** | Pas de partage d'écran (Discord, Meet, Teams dans Firefox) ; fenêtres « Ouvrir un fichier » cassées dans les applis Flatpak (Heroic, Steam) | xdg-desktop-portal-wlr et -gtk |
| 🔴 | **Secure Boot sans réglage** | Sur un PC récent, Plume ne démarre pas tant que Secure Boot n'est pas désactivé ou que tes clés ne sont pas inscrites ; Ubuntu démarre directement grâce à un chargeur signé par Microsoft (shim) | Marche à suivre claire dans l'installateur, ou shim |
| 🔴 | **Réglage des écrans** | Aucun moyen simple de régler la résolution, le zoom (écran 4K), un 2e écran ou un vidéoprojecteur | Panneau HUD « écrans » (wlr-randr / kanshi) |
| 🟠 | **Imprimante et scanner** | Impossible d'imprimer ou de numériser | CUPS + pilotes, sane, panneau simple |
| 🟠 | **Batterie** | Pas de mode économie / performance | power-profiles-daemon ou TLP, affichage dans la barre |
| 🟠 | **Mise à jour du BIOS et des firmwares** | Le BIOS et les firmwares du PC ne sont jamais mis à jour | fwupd |
| 🟠 | **Installateur graphique** | `install.sh` se lance dans un terminal : difficile pour un débutant | Installateur HUD à la souris |
| 🟠 | **Wifi d'hôtel ou de gare** | La page de connexion du wifi public ne s'ouvre pas toute seule | Détection du portail captif et ouverture de Firefox |
| 🟠 | **Lumière du soir** | Pas de filtre de lumière bleue | wlsunset + bouton dans la barre |
| 🟢 | **Retour en arrière** après une mauvaise mise à jour | Void évolue en continu : une mise à jour ratée peut casser le système | Instantanés btrfs, ou ancien noyau au choix dans GRUB |
| 🟢 | **Sauvegarde** des fichiers | Pas d'outil de sauvegarde simple | Équivalent de Déjà Dup |
| 🟢 | **VPN** en un clic | Il faut tout configurer à la main | WireGuard / OpenVPN dans un panneau |
| 🟢 | **Accessibilité** | Ni loupe ni lecteur d'écran | Loupe, lecteur d'écran, clavier à l'écran hors du terminal |

#### Pour le jeu vidéo et le développement pro

| Priorité | Usage | Ce qui coince | Ce qu'il faut |
|---|---|---|---|
| 🔴 | Dev pro, jeux | **Partage d'écran** en réunion (Teams, Meet, Zoom, Slack) ou en stream (Discord, OBS) impossible | Portails Wayland (ligne 🔴 ci-dessus) |
| 🔴 | Dev pro | **VS Code** : seule la version Flatpak est dans la Logithèque ; son terminal intégré ne voit ni tes compilateurs ni Docker | Version native (`vscode` dans Void) dans la Logithèque |
| 🟠 | Jeux | **Carte NVIDIA** : pilote officiel à installer à la main (`plume-jeux` donne seulement la commande) | Détection et installation automatiques du bon pilote (`nvidia` ou `nvidia470` + 32 bits) |
| 🟠 | Dev pro | **VPN d'entreprise** (OpenVPN, Cisco AnyConnect, GlobalProtect, WireGuard) souvent obligatoire au travail | Paquets VPN + panneau (ligne VPN ci-dessus, à remonter en 🟠) |
| 🟢 | Jeux | **HDR** : sway ne le gère pas encore bien | Attendre sway, ou gamescope en HDR |
| 🟢 | Dev pro | **Pas de systemd** : quelques outils et tutoriels supposent `systemctl` | Distrobox (Ubuntu dans un conteneur) ; Docker marche déjà avec runit |
| — | Jeux | **Anti-triche noyau** (Valorant, Fortnite, Call of Duty…) | Rien à faire : ces jeux ne marchent sur aucune distro Linux |
| — | Dev pro | **PC géré par l'entreprise** (antivirus imposé, gestion à distance) | Hors de portée : ces outils ne supportent qu'Ubuntu ou RHEL |

Déjà aussi bien ou mieux qu'Ubuntu : mises à jour en un clic, pare-feu actif par défaut, disque chiffré,
AppArmor avec de vrais profils, magasin d'applis (Logithèque), wifi, son, Bluetooth, jeux et manettes,
et environ 150 Mo de RAM pour le bureau.

### Stabilité et matériel

| Priorité | Tâche |
|---|---|
| 🔴 | Tester sur un vrai PC : wifi, veille, son, luminosité, touches multimédia |
| 🔴 | Tester le démarrage UEFI |

### Sécurité et mises à jour

| Priorité | Tâche |
|---|---|
| ✅ | **Sécuriser la distro** : pare-feu nftables (`plume-parefeu`), noyau durci (`sysctl`), disque chiffré LUKS2 en option, AppArmor, Secure Boot avec tes clés (`plume-secureboot`). Plume Void : vrais profils AppArmor pour Firefox, mpv, imv et Zathura (`/etc/apparmor.d/plume-*`) |
| ✅ | **Mises à jour simples** : `plume-maj` + ⬆ MAJ dans la barre (paquets Alpine + Flatpak), passage à la version suivante d'Alpine (`plume-maj --nouvelle-version`, ★ dans la barre). Reste : les outils Plume eux-mêmes (dépôt .apk ci-dessous) |
| ✅ | Dépôt `.apk` Plume (`make depot`, signé, embarqué dans l'ISO), publication sur GitHub Pages préparée par `./build/publier-depot.sh` |
| 🟢 | **Plus tard (optionnel) : publication automatique du dépôt** — une GitHub Action qui reconstruit et publie le dépôt à chaque push. Il faudrait confier la clé privée de signature aux *secrets* GitHub : pratique, mais la clé ne serait plus seulement chez toi. En attendant : `./build/publier-depot.sh` à la main |

### Applications

| Priorité | Tâche |
|---|---|

### Jeux vidéo

| Priorité | Tâche |
|---|---|
| ✅ | **Jeux PC (Plume Void)** : `sudo plume-jeux` — Steam natif + 32 bits, Epic et GOG via Heroic (installateurs GOG hors ligne importables), Vulkan, GameMode, MangoHud, manettes (`plume-manette`), émulateurs Switch / PS4 ajoutés. Reste : essayer de vrais jeux sur un vrai PC |

### Bureau et apparence

| Priorité | Tâche |
|---|---|
| 🟢 | **Mode « Windows » avec `Ctrl + 9`** : bascule le bureau vers une disposition familière pour qui vient de Windows (barre des tâches en bas, menu Démarrer, fenêtres flottantes avec boutons fermer/agrandir, raccourcis Windows), puis retour au mode Plume avec le même raccourci. À vérifier : `Ctrl + 9` est déjà utilisé par les navigateurs (aller au dernier onglet) |

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

## Publier les dépôts Plume (GitHub Pages)

Les outils Plume (barre, terminal, panneaux, écran de connexion…) sont des paquets
.apk signés. Une copie est dans l'ISO. Pour que les PC déjà installés reçoivent les
nouvelles versions avec `plume-maj`, le dépôt est publié sur GitHub Pages :
`https://esomnard.github.io/alpine-linux-based-on-distro/plume` (déjà indiqué dans
`/etc/plume/depot.conf`, ajouté à `/etc/apk/repositories` par desktop.sh).

**Une seule fois** : sur GitHub, *Settings → Pages → Source : Deploy from a branch*,
branche **gh-pages**, dossier **/ (root)**. Le dépôt GitHub doit être public (compte gratuit).

Le dépôt `.xbps` de Plume Void est publié au même endroit, dans `plume-void/`
(`/etc/xbps.d/10-plume.conf` sur Plume Void). Sa clé privée est `cles/plume-void.pem`.

**À chaque nouvelle version** :

```sh
./build/publier-depot.sh        # construit le dépôt signé dans out/gh-pages (branche gh-pages)
git -C out/gh-pages commit -m "Dépôt Plume 1.0.0-rN"     # commandes affichées par le script
git -C out/gh-pages push -u origin gh-pages
```

La version des paquets vient de `paquets/VERSION` et du nombre de commits (`-rN`) :
chaque nouveau commit publié devient une mise à jour pour apk.

> ⚠️ **Sauvegarde le dossier `cles/`** (clé privée de signature, jamais dans git) hors de
> ce PC. Si elle est perdue, les PC installés refuseront les paquets signés avec une
> nouvelle clé. La confiance repose sur cette signature, pas sur l'hébergeur : même un
> GitHub piraté ne pourrait pas faire installer un faux paquet.

## Licence

Plume Linux (les outils maison : plume-hud, plume-shell, plume-term, plume-lanceur,
plume-fonds, plume-wifi, plume-son, plume-bluetooth, la Logithèque, les scripts et
la configuration) est un logiciel libre sous **GNU GPL version 3 ou ultérieure**
(`GPL-3.0-or-later`) — voir [LICENSE](LICENSE).

Tu peux l'utiliser, l'étudier, le modifier et le redistribuer. Si tu distribues une
version modifiée, tu dois la publier sous la même licence, avec ses sources.
Alpine Linux et les logiciels qu'elle fournit gardent leurs propres licences.
