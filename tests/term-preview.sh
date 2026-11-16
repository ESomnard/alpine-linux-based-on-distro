#!/bin/sh
# Essai de plume-term dans un sway virtuel (couleurs, Unicode, htop, onglets, historique, coller).
#   ./tests/term-preview.sh     -> out/term/*.png
set -eu
cd "$(dirname "$0")/.."
mkdir -p out/term
docker run --rm --cap-add SYS_NICE --platform linux/amd64 \
	-v "$PWD/term":/term:ro -v "$PWD/rootfs":/rootfs:ro -v "$PWD/tests":/tests:ro -v "$PWD/out/term":/out \
	alpine:latest sh /tests/term-preview-inside.sh
ls -l out/term
