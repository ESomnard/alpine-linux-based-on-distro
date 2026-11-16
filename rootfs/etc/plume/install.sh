#!/bin/sh
# Plume — installation sur disque depuis l'ISO live.
#   sh /etc/plume/install.sh                    questions, puis setup-alpine
#   sh /etc/plume/install.sh --auto FICHIER     100 % automatique (modèle :
#                                               /etc/plume/install-auto.conf)
set -eu

[ "$(id -u)" -eq 0 ] || { echo "À lancer en root." >&2; exit 1; }

ask() { # ask "question" "défaut"
	printf '%s [%s] : ' "$1" "$2" >&2
	read -r _r
	echo "${_r:-$2}"
}

AUTO=""
if [ "${1:-}" = --auto ]; then
	AUTO="${2:-}"
	[ -r "$AUTO" ] || { echo "Fichier de réponses introuvable : $AUTO (modèle : /etc/plume/install-auto.conf)" >&2; exit 1; }
	CLAVIER="fr fr" FUSEAU="Europe/Paris" MACHINE="plume" DISQUE="" UTILISATEUR=""
	MOT_DE_PASSE="" MOT_DE_PASSE_ROOT="" RESEAU="eth0" WIFI_NOM="" WIFI_MOT_DE_PASSE=""
	BUREAU="oui" APPLIS="oui" REDEMARRER="oui" CHIFFREMENT="non" PHRASE_CHIFFREMENT=""
	# shellcheck disable=SC1090
	. "$AUTO"
	for v in DISQUE UTILISATEUR MOT_DE_PASSE; do
		eval "[ -n \"\${$v}\" ]" || { echo "Réponse manquante dans $AUTO : $v" >&2; exit 1; }
	done
	[ -b "$DISQUE" ] || { echo "Disque introuvable : $DISQUE" >&2; exit 1; }
	if [ "$CHIFFREMENT" = oui ] && [ ${#PHRASE_CHIFFREMENT} -lt 8 ]; then
		echo "CHIFFREMENT=oui : PHRASE_CHIFFREMENT doit faire au moins 8 caractères" >&2
		exit 1
	fi
	KEYMAP="$CLAVIER" TZ_NAME="$FUSEAU" HOST="$MACHINE"
else
	echo "=== Installation de Plume Linux ==="
	KEYMAP="$(ask 'Disposition clavier (ex: fr fr, us us, ch fr)' 'fr fr')"
	TZ_NAME="$(ask 'Fuseau horaire' 'Europe/Paris')"
	HOST="$(ask "Nom de la machine" 'plume')"
	echo
	echo "Chiffrer le disque ? Sans ta phrase secrète, personne ne peut lire tes fichiers"
	echo "(PC volé, disque retiré). Elle sera demandée à chaque démarrage. Oubliée = données perdues."
	CHIFFREMENT="$(ask 'Chiffrer le disque (o/n)' 'n')"
	case "$CHIFFREMENT" in [oOyY]*) CHIFFREMENT=oui ;; *) CHIFFREMENT=non ;; esac
fi
# -e : racine chiffrée (LUKS2) ; /boot reste en clair pour pouvoir démarrer
CRYPT=""
[ "$CHIFFREMENT" = oui ] && CRYPT="-e "

ANSWERS=/tmp/plume-answers
cat > "$ANSWERS" <<ANS
KEYMAPOPTS="$KEYMAP"
HOSTNAMEOPTS="$HOST"
DEVDOPTS=mdev
TIMEZONEOPTS="$TZ_NAME"
PROXYOPTS=none
APKREPOSOPTS="-1 -c"
SSHDOPTS=none
NTPOPTS=busybox
LBUOPTS=none
APKCACHEOPTS=none
ANS

if [ -z "$AUTO" ]; then
	cat >> "$ANSWERS" <<'ANS'
# Pas de nom d'utilisateur ici : sinon setup-user ne demande pas de mot de passe
# et le compte reste verrouillé. Il demandera nom + mot de passe lui-même.
USEROPTS="-a -g audio,video,netdev,input"
ANS
	echo "DISKOPTS=\"${CRYPT}-m sys -s 0\"" >> "$ANSWERS"
	# Volontairement NON définis -> setup-alpine les demande :
	#   INTERFACESOPTS (ethernet ou wifi), DNS, mots de passe, disque cible.
	cat <<'INFO'

setup-alpine va maintenant demander :
  - le réseau   : choisis eth0 (câble) ou wlan0 (wifi) puis "dhcp"
  - le mot de passe root
  - ton utilisateur : son nom (minuscules), puis son mot de passe
  - le disque   : ex. sda / nvme0n1  (ATTENTION : il sera EFFACÉ)
INFO
	[ "$CHIFFREMENT" = oui ] && echo "  - ta phrase secrète de chiffrement (création, confirmation, puis ouverture)"
	echo
	printf 'Entrée pour continuer, Ctrl+C pour annuler... ' ; read -r _
	setup-alpine -f "$ANSWERS"
	cat <<'END'

Installation terminée. Retire la clé USB puis tape : reboot
Ensuite, connecte-toi avec ton utilisateur et lance :

    sudo sh /etc/plume/desktop.sh

END
	exit 0
fi

# ============================ mode automatique ============================
echo "=== Installation automatique de Plume sur $DISQUE (il sera EFFACÉ) ==="
if [ -n "$WIFI_NOM" ]; then
	# wifi : iwd se connecte et gère lui-même l'adresse (DHCP)
	echo "==> Wifi : connexion à $WIFI_NOM"
	mkdir -p /etc/iwd
	printf '[General]\nEnableNetworkConfiguration=true\n[Network]\nNameResolvingService=resolvconf\n' > /etc/iwd/main.conf
	rc-service iwd restart >/dev/null 2>&1 || rc-service iwd start
	sleep 3
	DEV=$(iwctl device list 2>/dev/null | awk '/station/ {print $1; exit}')
	DEV=${DEV:-wlan0}
	iwctl --passphrase "$WIFI_MOT_DE_PASSE" station "$DEV" connect "$WIFI_NOM"
	sleep 8
	IFACES="auto lo
iface lo inet loopback
"
else
	IFACES="auto lo
iface lo inet loopback

auto $RESEAU
iface $RESEAU inet dhcp
"
fi
{
	printf 'INTERFACESOPTS="%s"\n' "$IFACES"
	# nom d'utilisateur fourni : setup-user ne pose pas de question (mot de passe posé plus bas)
	printf 'USEROPTS="-a -g audio,video,netdev,input -k none %s"\n' "$UTILISATEUR"
	printf 'DISKOPTS="%s-m sys -s 0 %s"\n' "$CRYPT" "$DISQUE"
} >> "$ANSWERS"

# chiffrement sans question : setup-disk lance cryptsetup (création puis
# ouverture) qui lit la phrase sur l'entrée standard, une ligne à chaque fois.
# Le fichier CLE (RAM de l'ISO live, effacé à la fin) sert à rouvrir le disque.
CLE=""
if [ "$CHIFFREMENT" = oui ]; then
	CLE=$(mktemp /tmp/plume-cle.XXXXXX)
	chmod 600 "$CLE"
	printf '%s' "$PHRASE_CHIFFREMENT" > "$CLE"
	trap 'rm -f "$CLE"' EXIT
fi

# -e : pas de mot de passe root demandé ; ERASE_DISKS : pas de confirmation d'effacement
export ERASE_DISKS="$DISQUE"
if [ -n "$CLE" ]; then
	printf '%s\n%s\n' "$PHRASE_CHIFFREMENT" "$PHRASE_CHIFFREMENT" | setup-alpine -e -f "$ANSWERS"
else
	setup-alpine -e -f "$ANSWERS" < /dev/null
fi

echo "==> Réglages du système installé"
MNT=/mnt/plume-cible
mkdir -p "$MNT"
ROOTDEV="" MAPPE=""
for p in "$DISQUE"*[0-9]; do
	[ -b "$p" ] || continue
	if [ -n "$CLE" ] && cryptsetup isLuks "$p" 2>/dev/null; then
		cryptsetup open "$p" plume-cible --key-file "$CLE" >/dev/null || continue
		MAPPE=plume-cible
		if ! mount /dev/mapper/plume-cible "$MNT" 2>/dev/null; then
			cryptsetup close plume-cible
			MAPPE=""
			continue
		fi
	else
		mount "$p" "$MNT" 2>/dev/null || continue
	fi
	if [ -f "$MNT/etc/alpine-release" ]; then
		ROOTDEV="$p"
		break
	fi
	umount "$MNT"
	if [ -n "$MAPPE" ]; then
		cryptsetup close "$MAPPE"
		MAPPE=""
	fi
done
[ -n "$ROOTDEV" ] || { echo "Système installé introuvable sur $DISQUE" >&2; exit 1; }

# mots de passe (jamais écrits ailleurs que dans /etc/shadow du système installé)
printf '%s:%s\n' "$UTILISATEUR" "$MOT_DE_PASSE" | chroot "$MNT" chpasswd
if [ -n "$MOT_DE_PASSE_ROOT" ]; then
	printf 'root:%s\n' "$MOT_DE_PASSE_ROOT" | chroot "$MNT" chpasswd
else
	chroot "$MNT" passwd -l root >/dev/null # pas de connexion root : on passe par sudo
fi

# wifi : la connexion est gardée pour le système installé
if [ -n "$WIFI_NOM" ] && ls /var/lib/iwd/* >/dev/null 2>&1; then
	mkdir -p "$MNT/var/lib/iwd" "$MNT/etc/iwd"
	cp -a /var/lib/iwd/. "$MNT/var/lib/iwd/"
	cp /etc/iwd/main.conf "$MNT/etc/iwd/main.conf"
	chroot "$MNT" apk add -q iwd openresolv >/dev/null 2>&1 || true
	chroot "$MNT" rc-update add iwd default >/dev/null 2>&1 || true
fi

# bureau installé tout seul au premier démarrage (journal : /var/log/plume-installation.log)
if [ "$BUREAU" = oui ]; then
	OPT=""
	[ "$APPLIS" = oui ] || OPT="--no-apps"
	mkdir -p "$MNT/etc/local.d"
	cat > "$MNT/etc/local.d/plume-premier-demarrage.start" <<EOF
#!/bin/sh
# Plume : installation du bureau au premier démarrage (une seule fois)
rm -f /etc/local.d/plume-premier-demarrage.start
echo "Plume : installation du bureau en cours (journal : /var/log/plume-installation.log)…" > /dev/console
if sh /etc/plume/desktop.sh $OPT > /var/log/plume-installation.log 2>&1; then
	echo "Plume : bureau installé, redémarrage." > /dev/console
	reboot
else
	echo "Plume : échec de l'installation du bureau, voir /var/log/plume-installation.log" > /dev/console
fi
EOF
	chmod 755 "$MNT/etc/local.d/plume-premier-demarrage.start"
	chroot "$MNT" rc-update add local default >/dev/null
fi
umount "$MNT"
[ -z "$MAPPE" ] || cryptsetup close "$MAPPE"

echo
echo "Installation terminée."
[ "$CHIFFREMENT" = oui ] && echo "Disque chiffré : ta phrase secrète sera demandée à chaque démarrage."
[ "$BUREAU" = oui ] && echo "Au premier démarrage, le bureau s'installe tout seul (10 à 30 min, connexion Internet nécessaire)."
if [ "$REDEMARRER" = oui ]; then
	echo "Redémarrage dans 10 s (retire la clé USB)…"
	sleep 10
	reboot
fi
