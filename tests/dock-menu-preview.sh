#!/bin/sh
# Menu du clic droit du dock (comme sur Mac), plume-shell compilé comme dans Plume Void.
#   ./tests/dock-menu-preview.sh     -> out/dock-menu/*.png
set -eu
cd "$(dirname "$0")/.."
mkdir -p out/dock-menu
docker run --rm --cap-add SYS_NICE --platform linux/amd64 \
	-v "$PWD":/w:ro -v "$PWD/out/dock-menu":/out \
	alpine:latest sh /w/tests/dock-menu-preview-inside.sh
ls -l out/dock-menu
