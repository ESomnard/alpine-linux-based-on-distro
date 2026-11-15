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
| + socle système (doas, dbus, iwd, zram…) | 75 | ~230 Mo |
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
| Admin | **doas** | sudo | Quelques centaines de lignes de code |
| Sessions graphiques | **seatd** | elogind / logind | Minuscule, suffisant pour Wayland |
| Wifi | **iwd** | NetworkManager | Un seul démon, qui gère aussi le DHCP |
| Swap | **zram** (compressé en RAM) | partition swap | Aucun espace disque perdu |
| Heure | **ntpd de BusyBox** | chrony | Déjà présent, 0 octet de plus |
| Firmwares | **seulement ceux de ton matériel** | tous (~770 Mo) | Détectés à l'installation + `slim-firmware.sh` |

### Bureau (Wayland)

| Rôle | Logiciel |
|---|---|
| Gestionnaire de fenêtres | **sway** (fenêtres en mosaïque, ~100 Mo de RAM) |
| Barre d'état | **swaybar** + script shell (clavier, wifi, volume, batterie, date) |
| Terminal | **foot** |
| Lanceur d'applications | **fuzzel** |
| Notifications | **mako** |
| Écran de verrouillage | **swaylock** + **swayidle** (verrouillage auto à 5 min, avant la veille, en fermant le capot) |
| Captures d'écran | **grim** + **slurp** |
| Presse-papiers | **wl-clipboard** |
| Son | **PipeWire** + WirePlumber (compatible PulseAudio et ALSA) |
| Graphisme | **Mesa** (+ accélération vidéo Intel si détectée) |
| Claviers | **AZERTY + QWERTY** (`Alt + Maj`) et **chinois pinyin** via IBus (`Super + Espace`) |
| Polices | DejaVu + WenQuanYi Zen Hei (chinois) |

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
| Capture d'écran dans le test automatique | 🟡 Corrigée, à revalider |
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
→ redémarre → `doas sh /etc/plume/desktop.sh` → redémarre. C'est tout.

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
├── tests/
│   ├── vm-test.sh                # test automatique complet (QEMU dans Docker)
│   ├── vm-test.py                # pilote la VM par la console série (pexpect)
│   └── vm-gui.sh                 # VM graphique accessible en VNC
└── rootfs/                       # fichiers copiés tels quels dans le système
    └── etc/
        ├── motd                  # message d'accueil
        ├── doas.d/plume.conf     # veille / extinction / redémarrage sans mot de passe
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
> et une règle `doas` sans mot de passe pour l'utilisateur `test`. L'ISO n'est pas modifiée.

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
- Après `make test` : utilisateur **`test`**, mot de passe **`plume-test-123`**
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
   - **ton utilisateur** (`Setup a user?`) : ton login en minuscules, puis son mot de passe. Il sera admin via `doas` ;
   - **le disque** : par ex. `sda` ou `nvme0n1`. ⚠️ **Il sera entièrement effacé.**
6. À la fin : retire la clé et tape `reboot`.

---

## 8. Installer le bureau et les applications

Après le redémarrage, connecte-toi avec **ton utilisateur**, puis :

```sh
doas sh /etc/plume/desktop.sh            # tout
doas sh /etc/plume/desktop.sh --no-apps  # sans Firefox, mpv… (plus léger)
doas reboot
```

Le script :
1. active le dépôt `community` et met le système à jour ;
2. remplace `mdev` par `udev`, indispensable pour le clavier, la souris et le touchpad sous Wayland ;
3. installe `packages/core.list`, `desktop.list` et `apps.list`, plus l'accélération vidéo Intel si un GPU Intel est détecté ;
4. active les services `dbus`, `seatd`, `zram-init` et `iwd` ;
5. règle le swap **zram** à la moitié de la RAM ;
6. ajoute ton utilisateur aux groupes nécessaires (`seat`, `video`, `audio`, `input`…) et copie la config par défaut sans écraser l'existant ;
7. bascule le wifi vers **iwd**, en dernier pour ne pas couper l'installation.

Ensuite, connecte-toi sur le premier terminal (tty1) : **sway démarre automatiquement**.

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
firmwares (`apk add linux-firmware`) pour faire marcher un composant. Lance-le quand
**tout fonctionne** (wifi connecté, écran OK, bluetooth activé si tu t'en sers) :

```sh
doas sh /etc/plume/slim-firmware.sh
```

Le script :

1. liste les pilotes réellement chargés (`/proc/modules`) ;
2. demande au noyau quels firmwares chacun utilise (`modinfo -F firmware`) ;
3. retrouve les paquets correspondants (`apk info --who-owns`) ;
4. **propose** de supprimer tous les autres paquets `linux-firmware-*`, ainsi que le microcode du mauvais fabricant de CPU. Il demande confirmation.

> ⚠️ Un composant éteint pendant l'installation (wifi désactivé, dock débranché…) n'aura pas
> son firmware. Dans ce cas, ou si tu changes de matériel : `doas apk add linux-firmware`,
> puis relance le script.

### Encore plus léger

- Retirer des applications : `doas apk del pcmanfm micro …`
- Pas besoin du chinois : retirer les lignes `ibus …` et `font-wqy-zenhei` de `desktop.list` (−115 Mo).
- Ne pas installer `xwayland` tant qu'aucune appli ne l'exige.
- Supprimer la documentation : `doas apk del $(apk info | grep -- '-doc$')`.
- Vider le cache : `doas rm -rf /var/cache/apk/*`
- Démarrage plus rapide : `rc_parallel="YES"` dans `/etc/rc.conf`.
- Terminaux plus légers : `exec foot --server` dans sway, puis `set $term footclient`.

---

## 10. Utilisation au quotidien

### Raccourcis

La touche `Super` est la touche Windows (⌘ sur Mac).

| Raccourci | Action |
|---|---|
| `Super + Entrée` | Terminal |
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

### Écrire en chinois (pinyin)

1. `Super + Espace` : la barre affiche `拼音`.
2. Tape le pinyin, par exemple `nihao`.
3. Choisis les caractères avec `Espace` ou les chiffres : 你好.
4. `Super + Espace` pour revenir à la saisie normale.

### Commandes utiles

| Je veux… | Commande |
|---|---|
| Mettre à jour le système | `doas apk upgrade -U` |
| Chercher un logiciel | `apk search nom` |
| Installer / supprimer | `doas apk add nom` / `doas apk del nom` |
| Wifi | `iwctl` (interactif) ou `iwctl station wlan0 connect "SSID"` |
| Volume | `wpctl set-volume @DEFAULT_AUDIO_SINK@ 50%` |
| Choisir la sortie audio | `wpctl status`, puis `wpctl set-default <id>` |
| Bluetooth | `doas apk add bluez bluez-openrc && doas rc-update add bluetooth && doas rc-service bluetooth start`, puis `bluetoothctl` |
| Clé USB | `doas mount /dev/sdb1 /mnt` (ou pcmanfm + `doas apk add udisks2 gvfs`) |
| Imprimer | `doas apk add cups cups-openrc && doas rc-update add cupsd` |
| Applis absentes d'Alpine | `doas apk add flatpak` (attention : beaucoup plus lourd) |
| Services | `rc-status`, `doas rc-service <nom> restart` |
| Journaux | `doas tail -f /var/log/messages` |

Nouvelle version d'Alpine : remplace `v3.XX` dans `/etc/apk/repositories`, puis
`doas apk upgrade -U --available` et redémarre.

---

## 11. Personnaliser la distro

- **Paquets** : `rootfs/etc/plume/packages/*.list` (un nom par ligne, `#` pour un commentaire).
- **Bureau** : `rootfs/etc/skel/.config/sway/config` (raccourcis, couleurs, claviers). Une fois installé, chaque utilisateur a sa copie dans `~/.config/sway/config`.
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
| Clavier ou souris inactifs sous sway | `doas setup-devd udev`, puis redémarre |
| Pas de son | `ps \| grep pipewire` ; vérifie `echo $XDG_RUNTIME_DIR` et le groupe `audio` |
| Le pinyin ne s'active pas | `ps \| grep ibus-daemon` ; relance avec `ibus-daemon -drx` |
| Wifi absent | `doas rc-service iwd restart`, puis `iwctl` ; si la carte n'apparaît pas : `doas apk add linux-firmware` |
| Plus de wifi après `slim-firmware.sh` | Branche un câble, `doas apk add linux-firmware`, redémarre, relance le script avec le wifi **actif** |
| Luminosité bloquée | Vérifie le groupe `video`, puis reconnecte-toi |
| Une appli X11 ne s'ouvre pas | `doas apk add xwayland` |
| Programme compilé pour glibc | `doas apk add gcompat`, sinon Flatpak |
| Plus de son après une mise à jour d'Alpine | Alpine prévoit de retirer `/usr/libexec/pipewire-launcher` : adapte la ligne correspondante de la config sway selon le wiki PipeWire d'Alpine |

---

## 14. Feuille de route

| Priorité | Tâche |
|---|---|
| 🔴 | Tester sur un vrai PC : wifi, veille, son, luminosité, touches multimédia |
| 🔴 | Tester le démarrage UEFI |
| 🟠 | Option de chiffrement du disque dans `install.sh` (`setup-disk -e`) |
| 🟢 | Installation 100 % automatique (fichier de réponses complet) |
| 🟢 | Dépôt `.apk` Plume pour mettre à jour les configs avec `apk upgrade` |
| 🟢 | Gestion du wifi en mode graphique (`iwgtk`) |
| 🟢 | Identité visuelle : fond d'écran, thème |

### Ressources

- Wiki Alpine : https://wiki.alpinelinux.org
- Sway sur Alpine : https://wiki.alpinelinux.org/wiki/Sway
- PipeWire : https://wiki.alpinelinux.org/wiki/PipeWire
- iwd : https://wiki.alpinelinux.org/wiki/Iwd
- ISO personnalisée : https://wiki.alpinelinux.org/wiki/How_to_make_a_custom_ISO_image_with_mkimage
- Recherche de paquets : https://pkgs.alpinelinux.org
