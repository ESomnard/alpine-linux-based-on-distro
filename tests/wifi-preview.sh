#!/bin/sh
# Essai de plume-wifi dans un sway virtuel, avec un faux iwctl (tests/fake-iwctl) :
# liste des réseaux, mot de passe faux puis juste, déconnexion, wifi désactivé.
#   ./tests/wifi-preview.sh     -> out/wifi/*.png
set -eu
cd "$(dirname "$0")/.."
mkdir -p out/wifi
docker run --rm --cap-add SYS_NICE --platform linux/amd64 \
	-v "$PWD/wifi":/wifi:ro -v "$PWD/tests":/tests:ro -v "$PWD/out/wifi":/out \
	alpine:latest sh /tests/wifi-preview-inside.sh
ls -l out/wifi
