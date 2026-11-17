#!/bin/sh
# Tape du texte dans la VM graphique (vm-gui.sh), clavier AZERTY de Plume.
#   ./tests/vm-touches.sh "plume"        tape « plume »
#   ./tests/vm-touches.sh -k ret         envoie une touche (ret, tab, esc, meta_l-ret…)
set -eu
if [ "${1:-}" = -k ]; then
	CMDS="sendkey $2"
else
	# lettres seulement ; AZERTY : a<->q, z<->w, m est sur la touche « ; » du QWERTY
	CMDS=$(printf '%s\n' "$1" | fold -w1 | while read -r c; do
		case "$c" in
		(a) k=q ;; (q) k=a ;; (z) k=w ;; (w) k=z ;; (m) k=semicolon ;; (*) k=$c ;;
		esac
		echo "sendkey $k 10"
	done)
fi
printf '%s\n' "$CMDS" | docker exec -i plume-vm sh -c "
	command -v socat >/dev/null || apk add -q --no-cache socat >/dev/null
	while read -r l; do echo \"\$l\"; sleep 0.15; done | socat - UNIX-CONNECT:/tmp/moniteur.sock >/dev/null"
