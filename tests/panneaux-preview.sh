#!/bin/sh
# Essai de plume-son et plume-bluetooth dans un sway virtuel, avec un faux wpctl
# et un faux bluetoothctl (tests/fake-*).   ./tests/panneaux-preview.sh -> out/panneaux/*.png
set -eu
cd "$(dirname "$0")/.."
mkdir -p out/panneaux
docker run --rm --cap-add SYS_NICE --platform linux/amd64 \
	-v "$PWD/son":/son:ro -v "$PWD/bluetooth":/bt:ro -v "$PWD/tests":/tests:ro -v "$PWD/out/panneaux":/out \
	alpine:latest sh /tests/panneaux-preview-inside.sh
