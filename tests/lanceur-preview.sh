#!/bin/sh
# Essai de plume-lanceur (grille des applications) dans un sway virtuel.
#   ./tests/lanceur-preview.sh     -> out/lanceur/*.png
set -eu
cd "$(dirname "$0")/.."
mkdir -p out/lanceur
docker run --rm --cap-add SYS_NICE --platform linux/amd64 \
	-v "$PWD/lanceur":/lanceur:ro -v "$PWD/shell":/shell:ro -v "$PWD/rootfs":/rootfs:ro -v "$PWD/tests":/tests:ro \
	-v "$PWD/out/lanceur":/out alpine:latest sh /tests/lanceur-preview-inside.sh
ls -l out/lanceur
