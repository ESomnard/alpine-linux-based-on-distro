#!/bin/sh
# Exécuté DANS le conteneur Alpine : génère l'ISO avec l'outil officiel mkimage.sh (aports).
set -eu

ARCH="${ARCH:-x86_64}"
RELEASE="$(cat /etc/alpine-release)"          # ex: 3.24.4
BRANCH="$(echo "$RELEASE" | cut -d. -f1,2)"   # ex: 3.24
MIRROR="${MIRROR:-https://dl-cdn.alpinelinux.org/alpine}"
BUILD_USER=build
BUILD_HOME=/home/$BUILD_USER

echo "==> Alpine $RELEASE ($ARCH)"

apk add -q --no-cache alpine-sdk alpine-conf git fakeroot xorriso squashfs-tools \
	mtools dosfstools grub-efi
[ "$ARCH" = x86_64 ] && apk add -q --no-cache syslinux grub-bios

# mkimage refuse de tourner en root : on utilise un utilisateur dédié
adduser -D -G abuild "$BUILD_USER"

# Clé de signature pour l'index des paquets embarqués dans l'ISO
su "$BUILD_USER" -c "abuild-keygen -a -n" >/dev/null 2>&1
cp "$BUILD_HOME"/.abuild/*.rsa.pub /etc/apk/keys/

# Récupère uniquement le dossier scripts/ d'aports, pour la même version d'Alpine
echo "==> Récupération de mkimage.sh (aports $BRANCH-stable)"
git clone -q --depth 1 --filter=blob:none --sparse --branch "$BRANCH-stable" \
	https://gitlab.alpinelinux.org/alpine/aports.git "$BUILD_HOME/aports"
git -C "$BUILD_HOME/aports" sparse-checkout set scripts

cp /work/build/mkimg.plume.sh /work/build/genapkovl-plume.sh "$BUILD_HOME/aports/scripts/"
chmod +x "$BUILD_HOME/aports/scripts/genapkovl-plume.sh"
cp -a /work/rootfs "$BUILD_HOME/rootfs"
# sources de plume-hud (compilées sur la machine par desktop.sh)
mkdir -p "$BUILD_HOME/rootfs/etc/plume/hud"
cp /work/hud/*.c /work/hud/*.h /work/hud/Makefile "$BUILD_HOME/rootfs/etc/plume/hud/"
cp -r /work/hud/pam.d "$BUILD_HOME/rootfs/etc/plume/hud/"
mkdir -p "$BUILD_HOME/rootfs/etc/plume/shell"
cp /work/shell/*.c /work/shell/*.h /work/shell/Makefile "$BUILD_HOME/rootfs/etc/plume/shell/"
mkdir -p "$BUILD_HOME/rootfs/etc/plume/term"
cp /work/term/*.c /work/term/*.h /work/term/Makefile "$BUILD_HOME/rootfs/etc/plume/term/"
mkdir -p "$BUILD_HOME/rootfs/etc/plume/fonds"
cp /work/fonds/*.c /work/fonds/*.h /work/fonds/Makefile /work/fonds/plume-fonds.desktop "$BUILD_HOME/rootfs/etc/plume/fonds/"
mkdir -p "$BUILD_HOME/rootfs/etc/plume/wifi"
cp /work/wifi/*.c /work/wifi/*.h /work/wifi/Makefile /work/wifi/plume-wifi.desktop "$BUILD_HOME/rootfs/etc/plume/wifi/"
for p in son bluetooth; do
	mkdir -p "$BUILD_HOME/rootfs/etc/plume/$p"
	cp /work/$p/*.c /work/$p/*.h /work/$p/Makefile /work/$p/*.desktop "$BUILD_HOME/rootfs/etc/plume/$p/"
done
# dépôt .apk Plume (make depot) : desktop.sh installe les composants depuis lui
if [ -f /work/out/depot/plume/x86_64/APKINDEX.tar.gz ]; then
	mkdir -p "$BUILD_HOME/rootfs/etc/plume/depot"
	cp -r /work/out/depot/plume/. "$BUILD_HOME/rootfs/etc/plume/depot/"
	echo "==> Dépôt Plume embarqué : $(ls /work/out/depot/plume/x86_64/*.apk | wc -l) paquets"
fi
mkdir -p "$BUILD_HOME/rootfs/etc/plume/lanceur"
cp /work/lanceur/*.c /work/lanceur/*.h /work/lanceur/Makefile "$BUILD_HOME/rootfs/etc/plume/lanceur/"
mkdir -p "$BUILD_HOME/rootfs/etc/plume/logitheque"
cp /work/logitheque/*.c /work/logitheque/*.h /work/logitheque/Makefile /work/logitheque/catalogue.txt \
	/work/logitheque/plume-logitheque.desktop "$BUILD_HOME/rootfs/etc/plume/logitheque/"
mkdir -p "$BUILD_HOME/out"
chown -R "$BUILD_USER:abuild" "$BUILD_HOME"

echo "==> Construction de l'ISO"
su "$BUILD_USER" -c "
	cd $BUILD_HOME/aports/scripts
	export PLUME_ROOTFS=$BUILD_HOME/rootfs
	sh $BUILD_HOME/aports/scripts/mkimage.sh \
		--tag $RELEASE \
		--outdir $BUILD_HOME/out \
		--workdir $BUILD_HOME/work \
		--arch $ARCH \
		--repository $MIRROR/v$BRANCH/main \
		--repository $MIRROR/v$BRANCH/community \
		--profile plume
"

mkdir -p /work/out
cp "$BUILD_HOME"/out/*.iso /work/out/
chown -R "${HOST_UID:-0}:${HOST_GID:-0}" /work/out
echo "==> ISO prête :"
ls -lh /work/out/*.iso
