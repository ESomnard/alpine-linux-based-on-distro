#!/bin/sh
# Construit l'ISO Plume Void (glibc) dans out/ (via Docker) : dépôt .xbps puis ISO.
set -eu
cd "$(dirname "$0")/.."
command -v docker >/dev/null || { echo "Docker est requis." >&2; exit 1; }
./void/depot.sh
exec docker run --rm --privileged --platform linux/amd64 --name plume-void-iso \
	-v "$PWD":/work -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" -e PLUME_CMDLINE="${PLUME_CMDLINE:-}" \
	"${VOID_IMAGE:-ghcr.io/void-linux/void-glibc-full:latest}" sh /work/void/inside-container.sh
