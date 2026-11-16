#!/bin/sh
# Plume — ne garde QUE les firmwares utilisés par ton matériel (gain : plusieurs centaines de Mo).
# À lancer sur la machine finale, quand tout fonctionne (wifi, écran, bluetooth branchés/actifs).
set -eu
if [ "$(id -u)" -ne 0 ]; then
	command -v sudo >/dev/null && exec sudo sh "$0" "$@"
	exec doas sh "$0" "$@"
fi

FW=/lib/firmware
needed=$(mktemp)
trap 'rm -f "$needed"' EXIT

echo "==> Analyse des modules chargés..."
for mod in $(awk '{print $1}' /proc/modules); do
	for fw in $(modinfo -F firmware "$mod" 2>/dev/null); do
		# fw peut contenir des jokers (ex: iwlwifi-cc-a0-*.ucode) : on laisse le shell les développer
		for f in $FW/$fw $FW/$fw.*; do
			[ -e "$f" ] || continue
			echo "$f"
			[ -L "$f" ] && readlink -f "$f"
		done
	done
done | sort -u > "$needed.files"

if [ -s "$needed.files" ]; then
	xargs apk info --who-owns < "$needed.files" 2>/dev/null \
		| sed -n 's/.* is owned by //p' \
		| sed -E 's/-[0-9][^-]*-r[0-9]+$//' \
		| grep '^linux-firmware-' | sort -u > "$needed"
fi
rm -f "$needed.files"

echo "==> Firmwares nécessaires :"
if [ -s "$needed" ]; then sed 's/^/  - /' "$needed"; else echo "  (aucun)"; echo linux-firmware-none > "$needed"; fi

remove=""
for p in $(apk info 2>/dev/null | grep '^linux-firmware'); do
	grep -qx "$p" "$needed" || remove="$remove $p"
done

# Microcode : on garde seulement celui du CPU
case "$(awk -F': ' '/vendor_id/ {print $2; exit}' /proc/cpuinfo)" in
	GenuineIntel) apk info -e amd-ucode >/dev/null && remove="$remove amd-ucode" ;;
	AuthenticAMD) apk info -e intel-ucode >/dev/null && remove="$remove intel-ucode" ;;
esac

[ -n "$remove" ] || { echo "Rien à supprimer."; exit 0; }
echo "==> À supprimer :$remove"
printf 'Continuer ? [o/N] '; read -r r
case "$r" in o|O|y|Y) ;; *) echo "Annulé."; exit 0 ;; esac

# shellcheck disable=SC2046
apk add $(cat "$needed")
# shellcheck disable=SC2086
apk del $remove
echo "==> Fait. Espace disque :"; df -h /
