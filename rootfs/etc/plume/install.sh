#!/bin/sh
# Plume — installation sur disque depuis l'ISO live.
# Pose quelques questions, puis lance setup-alpine avec des réponses "légères" par défaut.
set -eu

[ "$(id -u)" -eq 0 ] || { echo "À lancer en root." >&2; exit 1; }

ask() { # ask "question" "défaut"
	printf '%s [%s] : ' "$1" "$2" >&2
	read -r _r
	echo "${_r:-$2}"
}

echo "=== Installation de Plume Linux ==="
KEYMAP="$(ask 'Disposition clavier (ex: fr fr, us us, ch fr)' 'fr fr')"
TZ_NAME="$(ask 'Fuseau horaire' 'Europe/Paris')"
HOST="$(ask "Nom de la machine" 'plume')"

ANSWERS=/tmp/plume-answers
cat > "$ANSWERS" <<ANS
KEYMAPOPTS="$KEYMAP"
HOSTNAMEOPTS="$HOST"
DEVDOPTS=mdev
TIMEZONEOPTS="$TZ_NAME"
PROXYOPTS=none
APKREPOSOPTS="-1 -c"
# Pas de nom d'utilisateur ici : sinon setup-user ne demande pas de mot de passe
# et le compte reste verrouillé. Il demandera nom + mot de passe lui-même.
USEROPTS="-a -g audio,video,netdev,input"
SSHDOPTS=none
NTPOPTS=busybox
DISKOPTS="-m sys -s 0"
LBUOPTS=none
APKCACHEOPTS=none
ANS
# Volontairement NON définis -> setup-alpine les demande :
#   INTERFACESOPTS (ethernet ou wifi), DNS, mots de passe, disque cible.

cat <<'INFO'

setup-alpine va maintenant demander :
  - le réseau   : choisis eth0 (câble) ou wlan0 (wifi) puis "dhcp"
  - le mot de passe root
  - ton utilisateur : son nom (minuscules), puis son mot de passe
  - le disque   : ex. sda / nvme0n1  (ATTENTION : il sera EFFACÉ)

INFO
printf 'Entrée pour continuer, Ctrl+C pour annuler... ' ; read -r _

setup-alpine -f "$ANSWERS"

cat <<'END'

Installation terminée. Retire la clé USB puis tape : reboot
Ensuite, connecte-toi avec ton utilisateur et lance :

    sudo sh /etc/plume/desktop.sh

END
