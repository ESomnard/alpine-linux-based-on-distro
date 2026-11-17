#!/bin/sh
# Dans le conteneur Void (--privileged) : ISO live Plume avec void-mklive.
# Le système live contient déjà tout le bureau ; l'installateur le recopie sur le disque.
set -eu
W=/work
DEPOT=$W/out/depot-void/x86_64
[ -f "$DEPOT/x86_64-repodata" ] || { echo "Dépôt Plume absent : lance d'abord void/depot.sh" >&2; exit 1; }

echo "==> Outils (void-mklive)"
xbps-install -Syu xbps >/dev/null 2>&1 || true
xbps-install -Sy git bash kmod xz squashfs-tools xorriso dosfstools mtools grub-x86_64-efi grub-i386-efi \
	grub liblz4 e2fsprogs >/dev/null
M=/tmp/mklive
rm -rf "$M"
git clone -q --depth 1 https://github.com/void-linux/void-mklive "$M"

# la clé publique du dépôt Plume, pour que xbps l'accepte sans question
AVANT=$(ls /var/db/xbps/keys/)
yes | xbps-install -S -R "$DEPOT" >/dev/null 2>&1 || true
NOUV=""
for k in /var/db/xbps/keys/*.plist; do
	echo "$AVANT" | grep -qxF "$(basename "$k")" || NOUV=$k
done
[ -n "$NOUV" ] || { echo "Clé du dépôt Plume non importée" >&2; exit 1; }
cp "$NOUV" "$M/keys/"
echo "    clé Plume : $(basename "$NOUV")"

echo "==> Fichiers Plume (communs + propres à Void)"
I=/tmp/include
rm -rf "$I" && mkdir -p "$I"
cp -a "$W/rootfs/." "$I/"
cp -a "$W/void/rootfs/." "$I/"
mkdir -p "$I/etc/plume/packages"
rm -f "$I"/etc/plume/packages/*.list
sed 's/#.*//' "$W/void/paquets.list" | grep -v '^[[:space:]]*$' > "$I/etc/plume/packages/core.list"
mkdir -p "$I/var/db/xbps/keys"
cp "$M"/keys/*.plist "$I/var/db/xbps/keys/"
chown -R 0:0 "$I"

PKGS=$(sed 's/#.*//' "$W/void/paquets.list" | tr -s ' \t\n' ' ')
VERSION=$(cat "$W/paquets/VERSION")
OUTISO=$W/out/plume-void-$VERSION-x86_64.iso
LSM="lsm=landlock,lockdown,yama,loadpin,safesetid,integrity,apparmor"

echo "==> Construction de l'ISO (plusieurs minutes)"
cd "$M"
./mklive.sh -a x86_64 \
	-r "$DEPOT" \
	-p "$PKGS" \
	-I "$I" \
	-x "$W/void/postsetup.sh" \
	-S "dbus seatd greetd iwd dhcpcd bluetoothd nftables zramen chronyd polkitd cupsd avahi-daemon power-profiles-daemon" \
	-k fr -l fr_FR.UTF-8 \
	-T "Plume Linux (Void)" \
	-C "live.user=plume $LSM ${PLUME_CMDLINE:-}" \
	-o "$OUTISO"
chown "${HOST_UID:-0}:${HOST_GID:-0}" "$OUTISO"
ls -lh "$OUTISO"
