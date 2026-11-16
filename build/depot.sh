#!/bin/sh
# Construit le dépôt .apk Plume (signé) dans out/depot/plume/<arch>/.
#   ./build/depot.sh          (appelé par « make depot » et « make iso »)
# Version : paquets/VERSION ; révision (pkgrel) : nombre de commits git, pour que
# chaque nouveau commit donne une mise à jour visible par apk.
# La clé de signature est créée une fois dans cles/ (À NE PAS PUBLIER : .gitignore) ;
# sa partie publique part dans le dépôt et dans l'ISO.
set -eu
cd "$(dirname "$0")/.."
VERSION=$(cat paquets/VERSION)
REL=${REL:-$(git rev-list --count HEAD 2>/dev/null || echo 0)}
mkdir -p cles
rm -rf out/depot && mkdir -p out/depot   # seulement la dernière version
docker run --rm --platform linux/amd64 -e VERSION="$VERSION" -e REL="$REL" \
	-v "$PWD":/src:ro -v "$PWD/cles":/cles -v "$PWD/out/depot":/depot \
	alpine:3.24 sh /src/build/depot-inside.sh
echo "Dépôt : out/depot/plume/ ($(ls out/depot/plume/*/*.apk | wc -l) paquets, version $VERSION-r$REL)"
