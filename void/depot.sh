#!/bin/sh
# Construit le dépôt .xbps Plume (Void, glibc) signé dans out/depot-void/ (via Docker).
set -eu
cd "$(dirname "$0")/.."
command -v docker >/dev/null || { echo "Docker est requis." >&2; exit 1; }
mkdir -p out cles
REL=${REL:-$(git rev-list --count HEAD 2>/dev/null || echo 1)}
exec docker run --rm --platform linux/amd64 --name plume-void-depot \
	-v "$PWD":/work -e REL="$REL" -e VERSION="$(cat paquets/VERSION)" \
	"${VOID_IMAGE:-ghcr.io/void-linux/void-glibc-full:latest}" sh /work/void/depot-inside.sh
