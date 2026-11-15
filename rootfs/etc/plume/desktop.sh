#!/bin/sh
# Plume — installe l'environnement du quotidien sur un système déjà installé.
# Usage : doas sh /etc/plume/desktop.sh [--no-apps]
set -eu

PLUME=/etc/plume
[ "$(id -u)" -eq 0 ] || exec doas sh "$0" "$@"

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
rc-update add seatd default
rc-update add zram-init default

log "Swap compressé en RAM (zram, moitié de la RAM)"
MEM_MB=$(( $(awk '/MemTotal/ {print $2}' /proc/meminfo) / 1024 ))
sed -i -E "s/^num_devices=.*/num_devices=1/; s/^size0=[0-9]*/size0=$(( MEM_MB / 2 ))/; s/^algo0=[a-z0-9]*/algo0=zstd/" /etc/conf.d/zram-init

log "Utilisateurs : groupes + fichiers de config par défaut"
awk -F: '$3 >= 1000 && $3 < 60000 {print $1":"$6}' /etc/passwd | while IFS=: read -r u home; do
	for g in seat video audio input netdev wheel; do addgroup "$u" "$g" 2>/dev/null || true; done
	mkdir -p "$home"
	# copie /etc/skel sans écraser ce qui existe déjà
	(cd /etc/skel && find . -type f) | while read -r f; do
		[ -e "$home/$f" ] && continue
		mkdir -p "$home/$(dirname "$f")"
		cp -p "/etc/skel/$f" "$home/$f"
	done
	chown -R "$u:$u" "$home"
	echo "  - $u"
done

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

Terminé ! Redémarre avec : doas reboot

Après le redémarrage, connecte-toi sur tty1 : sway démarre tout seul.
Wifi (une seule fois) :  iwctl station wlan0 connect "NomDuReseau"

Pour alléger encore : doas sh /etc/plume/slim-firmware.sh

END
