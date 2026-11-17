#!/bin/sh
# Plume Void — ne garde QUE les firmwares utilisés par ton matériel (gain : plusieurs centaines de Mo).
# À lancer sur la machine finale, quand tout fonctionne (wifi, écran, Bluetooth, dock branchés et actifs).
#
#   sudo sh /etc/plume/slim-firmware.sh              analyse, puis allège après confirmation
#   sudo sh /etc/plume/slim-firmware.sh --oui        sans question
#   sudo sh /etc/plume/slim-firmware.sh --restaurer  remet tous les firmwares (nouveau matériel)
#
# Sous Void, les paquets de firmwares restent installés (linux-base en dépend) : on garde seulement
# les FICHIERS utiles, et /etc/xbps.d/90-plume-firmware.conf (règles « noextract ») empêche les
# mises à jour de remettre les autres.
set -eu
if [ "$(id -u)" -ne 0 ]; then
	command -v sudo >/dev/null && exec sudo sh "$0" "$@"
	exec doas sh "$0" "$@"
fi

FW=/usr/lib/firmware
CONF=/etc/xbps.d/90-plume-firmware.conf

paquets_firmware() {
	xbps-query -l | awk '{print $2}' | sed 's/-[^-]*$//' | grep -- '-firmware' || true
}

if [ "${1:-}" = --restaurer ]; then
	rm -f "$CONF"
	echo "==> Réinstallation de tous les firmwares…"
	# shellcheck disable=SC2046
	xbps-install -fy $(paquets_firmware)
	echo "==> Fait. Espace disque :"; df -h /
	exit 0
fi

garder=$(mktemp)
trap 'rm -f "$garder"' EXIT

echo "==> Analyse des modules chargés…"
for mod in $(awk '{print $1}' /proc/modules); do
	modinfo -F firmware "$mod" 2>/dev/null || true
done | sort -u | while read -r fw; do
	# fw peut contenir des jokers (ex : iwlwifi-cc-a0-*.ucode) ; les fichiers peuvent être compressés (.zst)
	echo "$FW/$fw*"
	for f in $FW/$fw $FW/$fw.*; do
		if [ -L "$f" ]; then readlink -f "$f"; fi
	done
done > "$garder"
# toujours gardés : base de données wifi et microcode du processeur
echo "$FW/regulatory.db*" >> "$garder"
case "$(awk -F': ' '/vendor_id/ {print $2; exit}' /proc/cpuinfo)" in
	GenuineIntel) echo "$FW/intel-ucode/*" >> "$garder" ;;
	AuthenticAMD) echo "$FW/amd-ucode/*" >> "$garder" ;;
esac
sort -u -o "$garder" "$garder"

utile() {
	while read -r motif; do
		# shellcheck disable=SC2254
		case "$1" in $motif) return 0 ;; esac
	done < "$garder"
	return 1
}

# certains noms de firmwares contiennent des espaces : une ligne par fichier, jamais de $(find)
tous=$(mktemp); suppr=$(mktemp)
trap 'rm -f "$garder" "$tous" "$suppr"' EXIT
find "$FW" ! -type d > "$tous"
echo "==> Firmwares gardés :"
while IFS= read -r f; do
	if utile "$f"; then echo "  - ${f#"$FW"/}"; else printf '%s\n' "$f" >> "$suppr"; fi
done < "$tous"
n=$(wc -l < "$suppr")
[ "$n" -gt 0 ] || { echo "Rien à supprimer."; exit 0; }
taille=$(tr '\n' '\0' < "$suppr" | xargs -0 du -ck | tail -1 | cut -f1)
echo "==> À supprimer : $n fichiers, $((taille / 1024)) Mo"
echo "    (nouveau matériel plus tard : sudo sh /etc/plume/slim-firmware.sh --restaurer)"
if [ "${1:-}" != --oui ]; then
	printf 'Continuer ? [o/N] '; read -r r
	case "$r" in o|O|y|Y) ;; *) echo "Annulé."; exit 0 ;; esac
fi

# règles noextract : tout le dossier est exclu, sauf les fichiers gardés (la dernière règle gagne)
{
	echo "# Plume — firmwares allégés par slim-firmware.sh (supprime ce fichier pour tout remettre)"
	echo "noextract=$FW/*"
	sed 's/^/noextract=!/' "$garder"
} > "$CONF"

tr '\n' '\0' < "$suppr" | xargs -0 rm -f
find "$FW" -mindepth 1 -type d -empty -delete
echo "==> Fait. Espace disque :"; df -h /
