#!/bin/sh
# Essai de la Logithèque Plume dans un sway virtuel : recherche, mauvais puis bon mot de passe,
# installation et suppression d'un paquet Alpine, protection des paquets vitaux, activation de Flathub.
#   ./tests/logitheque-preview.sh     -> out/logitheque/*.png
set -eu
cd "$(dirname "$0")/.."
mkdir -p out/logitheque
docker run --rm --cap-add SYS_NICE --platform linux/amd64 \
	-v "$PWD/logitheque":/lg:ro -v "$PWD/rootfs":/rootfs:ro -v "$PWD/tests":/tests:ro -v "$PWD/out/logitheque":/out \
	alpine:latest sh /tests/logitheque-preview-inside.sh
ls -l out/logitheque
