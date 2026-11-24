#!/bin/sh
# Pack développeur (plume-dev) testé dans un conteneur Void : paquets, Java, Docker, SDK Android,
# Flutter, Claude Code, variables d'environnement, clonage des projets publics. (~20 min, ~5 Go)
#   ./tests/plume-dev.sh
set -eu
cd "$(dirname "$0")/.."
exec docker run --rm --platform linux/amd64 -v "$PWD":/w:ro \
	ghcr.io/void-linux/void-glibc-full:latest sh /w/tests/plume-dev-inside.sh
