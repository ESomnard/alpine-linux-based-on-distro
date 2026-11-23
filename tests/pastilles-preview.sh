#!/bin/sh
# Pastilles de fenêtre (rouge fermer · jaune réduire · verte plein écran) des applis Plume,
# compilées comme dans Plume Void (-DPLUME_PASTILLES), dans un sway virtuel.
#   ./tests/pastilles-preview.sh     -> out/pastilles/*.png
set -eu
cd "$(dirname "$0")/.."
mkdir -p out/pastilles
docker run --rm --cap-add SYS_NICE --platform linux/amd64 \
	-v "$PWD":/w:ro -v "$PWD/out/pastilles":/out \
	alpine:latest sh /w/tests/pastilles-preview-inside.sh
ls -l out/pastilles
