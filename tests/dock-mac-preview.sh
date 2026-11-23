#!/bin/sh
# Dock façon Mac (plume-shell, Plume Void) : loupe, rebond, réordonner / retirer en glissant,
# fenêtres réduites, corbeille, fichiers lâchés sur une appli ou sur la corbeille.
#   ./tests/dock-mac-preview.sh     -> out/dock-mac/*.png
set -eu
cd "$(dirname "$0")/.."
mkdir -p out/dock-mac
docker run --rm --cap-add SYS_NICE --platform linux/amd64 \
	-v "$PWD":/w:ro -v "$PWD/out/dock-mac":/out \
	alpine:latest sh /w/tests/dock-mac-preview-inside.sh
ls -l out/dock-mac
