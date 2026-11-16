#!/bin/sh
# Plume — installe l'environnement du quotidien sur un système déjà installé.
# Usage : sudo sh /etc/plume/desktop.sh [--no-apps]
set -eu

PLUME=/etc/plume
if [ "$(id -u)" -ne 0 ]; then
	command -v sudo >/dev/null && exec sudo sh "$0" "$@"
	exec doas sh "$0" "$@"
fi

WITH_APPS=1
for a; do
	case "$a" in
		--no-apps) WITH_APPS=0 ;;
		*) echo "Option inconnue : $a" >&2; exit 1 ;;
	esac
done

log() { printf '\033[1;35m==>\033[0m %s\n' "$*"; }

# Lit une liste de paquets en ignorant commentaires et lignes vides
read_list() { sed -e 's/#.*//' "$PLUME/packages/$1.list" | tr -s ' \t' '\n' | grep -v '^$' || true; }

# Pilotes vidéo selon le GPU détecté
gpu_pkgs() {
	for d in /sys/bus/pci/devices/*; do
		[ -r "$d/class" ] || continue
		case "$(cat "$d/class")" in 0x03*) ;; *) continue ;; esac
		case "$(cat "$d/vendor")" in
			0x8086) echo intel-media-driver ;;     # accélération vidéo Intel récente
		esac
	done
}

log "Activation du dépôt community"
sed -i -E 's|^#[[:space:]]*(.*/community)$|\1|' /etc/apk/repositories
apk update
apk upgrade

log "Passage de mdev à udev (nécessaire pour Wayland/libinput)"
setup-devd udev

PKGS="$(read_list core) $(read_list desktop) $(gpu_pkgs)"
[ "$WITH_APPS" = 1 ] && PKGS="$PKGS $(read_list apps)"
log "Installation des paquets"
# shellcheck disable=SC2086
apk add $PKGS

log "Services"
rc-update add dbus default
rc-update add bluetooth default 2>/dev/null || true
rc-update add seatd default
rc-update add zram-init default

log "Swap compressé en RAM (zram, moitié de la RAM)"
MEM_MB=$(( $(awk '/MemTotal/ {print $2}' /proc/meminfo) / 1024 ))
sed -i -E "s/^num_devices=.*/num_devices=1/; s/^size0=[0-9]*/size0=$(( MEM_MB / 2 ))/; s/^algo0=[a-z0-9]*/algo0=zstd/" /etc/conf.d/zram-init

log "Utilisateurs : groupes + fichiers de config par défaut"
# dossiers personnels habituels (aussi pour les futurs utilisateurs via /etc/skel)
USER_DIRS="Bureau Documents Téléchargements Images Musique Vidéos"
for d in $USER_DIRS; do mkdir -p "/etc/skel/$d"; done
awk -F: '$3 >= 1000 && $3 < 60000 {print $1":"$6}' /etc/passwd | while IFS=: read -r u home; do
	for g in seat video audio input netdev wheel; do addgroup "$u" "$g" 2>/dev/null || true; done
	mkdir -p "$home"
	for d in $USER_DIRS; do mkdir -p "$home/$d"; done
	# copie /etc/skel sans écraser ce qui existe déjà
	(cd /etc/skel && find . -type f) | while read -r f; do
		[ -e "$home/$f" ] && continue
		mkdir -p "$home/$(dirname "$f")"
		cp -p "/etc/skel/$f" "$home/$f"
	done
	chown -R "$u:$u" "$home"
	echo "  - $u"
done

# --- Composants Plume : paquets .apk du dépôt Plume (embarqué dans l'ISO) ---
# Ils se mettent ensuite à jour avec le reste (plume-maj / apk upgrade).
# Sans dépôt : compilation sur place, comme avant.
DEPOT=/etc/plume/depot
HUD_OK=0
if [ -f "$DEPOT/$(apk --print-arch)/APKINDEX.tar.gz" ] && ls "$DEPOT"/*.rsa.pub >/dev/null 2>&1; then
	log "Composants Plume : paquets du dépôt Plume"
	cp "$DEPOT"/*.rsa.pub /etc/apk/keys/
	grep -qxF "$DEPOT" /etc/apk/repositories || echo "$DEPOT" >> /etc/apk/repositories
	# dépôt Plume en ligne (mises à jour des outils Plume), s'il est configuré
	DEPOT_URL=""
	# shellcheck disable=SC1091
	[ -f "$PLUME/depot.conf" ] && . "$PLUME/depot.conf"
	if [ -n "$DEPOT_URL" ]; then
		grep -qxF "$DEPOT_URL" /etc/apk/repositories || echo "$DEPOT_URL" >> /etc/apk/repositories
	fi
	# anciennes copies compilées à la main (/usr/local) : elles masqueraient les paquets
	rm -f /usr/local/bin/plume-*
	apk update -q || true
	if apk add plume-bureau; then
		HUD_OK=1
	else
		log "ATTENTION : installation des paquets Plume impossible"
	fi
else
	log "Compilation de plume-hud (connexion + verrouillage), plume-shell (barre + dock), plume-term (terminal + poste de pilotage), plume-lanceur (applications), plume-fonds (fond d'écran), plume-wifi, plume-son, plume-bluetooth et la Logithèque"
	# outils de compilation installés temporairement, puis supprimés juste après
	BUILD_DEPS="gcc musl-dev make pkgconf wayland-dev wayland-protocols wlr-protocols cairo-dev pango-dev libxkbcommon-dev linux-pam-dev libvterm-dev librsvg-dev gdk-pixbuf-dev"
	HUD_OK=0
	# shellcheck disable=SC2086
	if apk add --virtual .plume-build $BUILD_DEPS; then
		rm -rf /tmp/plume-hud && cp -r "$PLUME/hud" /tmp/plume-hud
		if make -C /tmp/plume-hud && make -C /tmp/plume-hud install PREFIX=/usr/local; then
			HUD_OK=1
			mkdir -p /usr/share/plume
			plume-wall /usr/share/plume/wallpaper.png 2560 1440 || true
		fi
		rm -rf /tmp/plume-hud
		rm -rf /tmp/plume-shell && cp -r "$PLUME/shell" /tmp/plume-shell
		make -C /tmp/plume-shell && make -C /tmp/plume-shell install PREFIX=/usr/local || true
		rm -rf /tmp/plume-shell
		rm -rf /tmp/plume-term && cp -r "$PLUME/term" /tmp/plume-term
		make -C /tmp/plume-term && make -C /tmp/plume-term install PREFIX=/usr/local || true
		rm -rf /tmp/plume-term
		rm -rf /tmp/plume-fonds && cp -r "$PLUME/fonds" /tmp/plume-fonds
		make -C /tmp/plume-fonds && make -C /tmp/plume-fonds install PREFIX=/usr/local || true
		rm -rf /tmp/plume-fonds
		rm -rf /tmp/plume-wifi && cp -r "$PLUME/wifi" /tmp/plume-wifi
		make -C /tmp/plume-wifi && make -C /tmp/plume-wifi install PREFIX=/usr/local || true
		rm -rf /tmp/plume-wifi
		for p in son bluetooth; do
			rm -rf "/tmp/plume-$p" && cp -r "$PLUME/$p" "/tmp/plume-$p"
			make -C "/tmp/plume-$p" && make -C "/tmp/plume-$p" install PREFIX=/usr/local || true
			rm -rf "/tmp/plume-$p"
		done
		rm -rf /tmp/plume-lanceur && cp -r "$PLUME/lanceur" /tmp/plume-lanceur
		make -C /tmp/plume-lanceur && make -C /tmp/plume-lanceur install PREFIX=/usr/local || true
		rm -rf /tmp/plume-lanceur
		rm -rf /tmp/plume-logitheque && cp -r "$PLUME/logitheque" /tmp/plume-logitheque
		make -C /tmp/plume-logitheque && make -C /tmp/plume-logitheque install PREFIX=/usr/local || true
		rm -rf /tmp/plume-logitheque
		log "Suppression des outils de compilation"
		apk del .plume-build
	fi

	# scripts Plume (fournis par le paquet plume-outils quand il y a le dépôt)
	# lanceur de barre : plume-shell (barre + dock HUD), sinon swaybar
	install -Dm755 "$PLUME/conf/plume-bar" /usr/bin/plume-bar
	# lanceur de terminal : plume-term, sinon foot
	install -Dm755 "$PLUME/conf/plume-terminal" /usr/bin/plume-terminal
	# toutes les applications : plume-lanceur (grille), fuzzel en secours
	install -Dm755 "$PLUME/conf/plume-menu" /usr/bin/plume-menu
	# couleur du thème (Apparence) et pack émulation
	install -Dm755 "$PLUME/conf/plume-theme" /usr/bin/plume-theme
	install -Dm755 "$PLUME/conf/plume-emulation" /usr/bin/plume-emulation
	install -Dm755 "$PLUME/conf/plume-maj" /usr/bin/plume-maj
	install -Dm755 "$PLUME/conf/plume-parefeu" /usr/bin/plume-parefeu
	install -Dm755 "$PLUME/conf/plume-greeter" /usr/bin/plume-greeter
fi

if [ "$HUD_OK" = 1 ]; then
	log "Écran de connexion : greetd + plume-hud"
	install -Dm644 "$PLUME/conf/greetd.toml" /etc/greetd/config.toml
	for g in seat video input; do addgroup greetd "$g" 2>/dev/null || true; done
	grep -q '^rc_need=' /etc/conf.d/greetd 2>/dev/null || echo 'rc_need="seatd"' >> /etc/conf.d/greetd
	rc-update add greetd default
else
	log "ATTENTION : plume-hud n'a pas pu être compilé — connexion texte + swaylock conservés"
fi

log "Sécurité : pare-feu (nftables) + durcissement du noyau (sysctl)"
install -Dm644 "$PLUME/conf/nftables.nft" /etc/nftables.nft
mkdir -p /etc/nftables.d
install -Dm644 "$PLUME/conf/sysctl-plume.conf" /etc/sysctl.d/60-plume.conf
rc-update add nftables default
rc-update add sysctl boot
# tout de suite (les connexions en cours, dont l'installation, restent permises)
rc-service nftables start >/dev/null 2>&1 || true
sysctl -q -p /etc/sysctl.d/60-plume.conf >/dev/null 2>&1 || true

log "Sécurité : AppArmor (actif au prochain démarrage)"
# compilé dans le noyau d'Alpine mais absent de la liste des modules de sécurité :
# on l'ajoute à la ligne de démarrage (extlinux en BIOS, GRUB en UEFI)
LSM="lsm=landlock,lockdown,yama,loadpin,safesetid,integrity,apparmor"
if [ -f /etc/update-extlinux.conf ] && ! grep -q 'lsm=' /etc/update-extlinux.conf; then
	sed -i "s/^default_kernel_opts=\"/&$LSM /" /etc/update-extlinux.conf
	update-extlinux >/dev/null 2>&1 || true
fi
if [ -f /etc/default/grub ] && ! grep -q 'lsm=' /etc/default/grub; then
	sed -i "s/^GRUB_CMDLINE_LINUX_DEFAULT=\"/&$LSM /" /etc/default/grub
	grub-mkconfig -o /boot/grub/grub.cfg >/dev/null 2>&1 || true
fi
# unix_chkpwd vérifie ton mot de passe à l'écran de verrouillage : son profil
# n'est pas éprouvé avec musl, et un refus empêcherait de déverrouiller
mkdir -p /etc/apparmor.d/disable
[ -f /etc/apparmor.d/unix-chkpwd ] && ln -sf /etc/apparmor.d/unix-chkpwd /etc/apparmor.d/disable/unix-chkpwd
rc-update add apparmor boot 2>/dev/null || true
[ -x /usr/bin/plume-secureboot ] || install -Dm755 "$PLUME/conf/plume-secureboot" /usr/bin/plume-secureboot
# apt, apt-get, apt-cache (traduits vers apk) partout, même hors connexion texte
for c in apt apt-get apt-cache; do ln -sf /etc/plume/bin/apt "/usr/local/bin/$c"; done

log "Nettoyage"
rm -rf /var/cache/apk/*

# --- Le réseau en dernier, pour ne pas couper la connexion pendant l'installation ---
log "Wifi : bascule vers iwd"
mkdir -p /etc/iwd
cp "$PLUME/conf/iwd-main.conf" /etc/iwd/main.conf
rc-update add iwd default
# retire le wifi géré par wpa_supplicant / ifupdown (iwd gère tout seul le DHCP)
rc-update del wpa_supplicant boot 2>/dev/null || true
rc-update del wpa_supplicant default 2>/dev/null || true
awk '
	/^auto[[:space:]]+wlan/ {next}
	/^iface[[:space:]]+wlan/ {skip=1; next}
	/^(auto|iface|allow-)/ {skip=0}
	/^[[:space:]]*$/ {skip=0}
	!skip
' /etc/network/interfaces > /tmp/interfaces && mv /tmp/interfaces /etc/network/interfaces

cat <<'END'

Terminé ! Redémarre avec : sudo reboot

Après le redémarrage, l'écran de connexion HUD s'affiche : ton mot de passe → sway.
(Connexion texte de secours : Ctrl+Alt+F1.)  Verrouiller : Super+Échap.
Wifi (une seule fois) :  iwctl station wlan0 connect "NomDuReseau"

Pour alléger encore : sudo sh /etc/plume/slim-firmware.sh

END
