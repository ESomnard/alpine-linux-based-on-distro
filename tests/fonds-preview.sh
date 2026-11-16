#!/bin/sh
# Essai de plume-fonds dans un sway virtuel : fonds Plume, mode d'affichage, photos JPEG,
# mémorisation, réapplication au rechargement de sway, image supprimée sans erreur.
#   ./tests/fonds-preview.sh     -> out/fonds/*.png
set -eu
cd "$(dirname "$0")/.."
mkdir -p out/fonds
docker run --rm --cap-add SYS_NICE --platform linux/amd64 \
	-v "$PWD/fonds":/fonds:ro -v "$PWD/rootfs":/rootfs:ro -v "$PWD/tests":/tests:ro -v "$PWD/out/fonds":/out \
	alpine:latest sh /tests/fonds-preview-inside.sh
ls -l out/fonds
