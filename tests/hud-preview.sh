#!/bin/sh
# Compile plume-hud et le photographie dans un sway virtuel (Docker), sans VM.
#   ./tests/hud-preview.sh           -> out/hud/*.png
set -eu
cd "$(dirname "$0")/.."
mkdir -p out/hud
docker run --rm --cap-add SYS_NICE --platform linux/amd64 -v "$PWD/hud":/src:ro -v "$PWD/tests":/tests:ro -v "$PWD/out/hud":/out \
	alpine:latest sh /tests/hud-preview-inside.sh
ls -l out/hud
