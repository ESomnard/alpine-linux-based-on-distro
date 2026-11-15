#!/bin/sh
# Lance la construction de l'ISO dans un conteneur Alpine (fonctionne sur macOS et Linux).
set -eu
cd "$(dirname "$0")/.."

ARCH="${ARCH:-x86_64}"
ALPINE_IMAGE="${ALPINE_IMAGE:-alpine:latest}"   # ex: alpine:3.22 pour figer une version

case "$ARCH" in
	x86_64)  PLATFORM=linux/amd64 ;;
	aarch64) PLATFORM=linux/arm64 ;;
	*) echo "Architecture non supportée : $ARCH" >&2; exit 1 ;;
esac

command -v docker >/dev/null || { echo "Docker est requis." >&2; exit 1; }
mkdir -p out

exec docker run --rm \
	--platform "$PLATFORM" \
	-v "$PWD":/work \
	-e ARCH="$ARCH" \
	-e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" \
	"$ALPINE_IMAGE" sh /work/build/inside-container.sh
