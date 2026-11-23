#!/bin/sh
# Icônes du bureau (plume-icones-bureau, Plume Void) dans un sway virtuel :
# sélection en glissant (gauche et droit), corbeille, nouveau dossier, ouvrir, déplacer, renommer.
#   ./tests/bureau-preview.sh     -> out/bureau/*.png
set -eu
cd "$(dirname "$0")/.."
mkdir -p out/bureau
docker run --rm --cap-add SYS_NICE --platform linux/amd64 \
	-v "$PWD":/w:ro -v "$PWD/out/bureau":/out \
	alpine:latest sh /w/tests/bureau-preview-inside.sh
ls -l out/bureau
