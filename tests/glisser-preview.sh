#!/bin/sh
# Glisser-déposer du bureau Plume (plume-icones-bureau) avec le vrai gestionnaire de fichiers (PCManFM),
# aperçu rapide (Espace) et clé USB affichée sur le bureau.
#   ./tests/glisser-preview.sh     -> out/glisser/*.png
set -eu
cd "$(dirname "$0")/.."
mkdir -p out/glisser
docker run --rm --privileged --platform linux/amd64 \
	-v "$PWD":/w:ro -v "$PWD/out/glisser":/out \
	alpine:latest sh /w/tests/glisser-preview-inside.sh
ls -l out/glisser
