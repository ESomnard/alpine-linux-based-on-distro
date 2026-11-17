#!/bin/sh
# Capture d'écran de la VM graphique lancée par vm-gui.sh (make vm-live-void, make vm-void…)
#   ./tests/vm-capture.sh [nom]   ->  out/vm/<nom>.png (par défaut : ecran)
set -eu
cd "$(dirname "$0")/.."
N="${1:-ecran}"
docker exec plume-vm sh -c "
	command -v socat >/dev/null || apk add -q --no-cache socat >/dev/null
	echo 'screendump /tmp/ecran.ppm' | socat - UNIX-CONNECT:/tmp/moniteur.sock >/dev/null
	sleep 1
	command -v pnmtopng >/dev/null || apk add -q --no-cache netpbm >/dev/null
	pnmtopng /tmp/ecran.ppm > /o/vm/$N.png"
echo "out/vm/$N.png"
