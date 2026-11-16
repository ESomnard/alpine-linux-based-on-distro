#!/bin/sh
# Aperçu du bureau Plume (thème, fond d'écran, terminal, lanceur) dans un sway virtuel.
# plume-term et son mode poste de pilotage : tests/term-preview.sh
#   ./tests/desktop-preview.sh     -> out/desktop/*.png
set -eu
cd "$(dirname "$0")/.."
mkdir -p out/desktop
docker run --rm --cap-add SYS_NICE --platform linux/amd64 \
	-v "$PWD/hud":/src:ro -v "$PWD/shell":/shell:ro -v "$PWD/rootfs":/rootfs:ro -v "$PWD/tests":/tests:ro -v "$PWD/out/desktop":/out \
	alpine:latest sh /tests/desktop-preview-inside.sh
ls -l out/desktop
