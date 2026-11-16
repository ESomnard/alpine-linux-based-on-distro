#!/bin/sh -e
# Génère l'overlay (apkovl) appliqué au démarrage du live : contient /etc/plume,
# /etc/skel, la config de base... Ces fichiers sont recopiés sur le disque par setup-alpine.

HOSTNAME="$1"
[ -n "$HOSTNAME" ] || { echo "usage: $0 hostname" >&2; exit 1; }
[ -d "$PLUME_ROOTFS" ] || { echo "PLUME_ROOTFS introuvable" >&2; exit 1; }

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

rc_add() {
	mkdir -p "$tmp/etc/runlevels/$2"
	ln -sf "/etc/init.d/$1" "$tmp/etc/runlevels/$2/$1"
}

mkdir -p "$tmp/etc/apk" "$tmp/etc/network"
echo "$HOSTNAME" > "$tmp/etc/hostname"
# sudo dans « world » : installé sur le live, puis sur le disque par setup-alpine
printf 'alpine-base\nsudo\n' > "$tmp/etc/apk/world"
cat > "$tmp/etc/network/interfaces" <<IFACES
auto lo
iface lo inet loopback

auto eth0
iface eth0 inet dhcp
IFACES

# Nos fichiers
cp -a "$PLUME_ROOTFS/." "$tmp/"
chmod 0755 "$tmp"/etc/plume/*.sh
chmod 0600 "$tmp"/etc/doas.d/*.conf
chmod 0440 "$tmp"/etc/sudoers.d/plume
chmod 0755 "$tmp"/etc/plume/bin/apt
chmod 0644 "$tmp"/etc/profile.d/plume.sh
chmod 0755 "$tmp"/etc/skel/.config/sway/*.sh "$tmp"/etc/plume/conf/plume-greeter "$tmp"/etc/plume/conf/plume-bar \
	"$tmp"/etc/plume/conf/plume-terminal "$tmp"/etc/plume/conf/plume-menu \
	"$tmp"/etc/plume/conf/plume-theme "$tmp"/etc/plume/conf/plume-emulation "$tmp"/etc/plume/conf/plume-maj
chown -R 0:0 "$tmp"

# Services minimum du live
rc_add devfs sysinit
rc_add dmesg sysinit
rc_add mdev sysinit
rc_add hwdrivers sysinit
rc_add modloop sysinit

rc_add hwclock boot
rc_add modules boot
rc_add sysctl boot
rc_add hostname boot
rc_add bootmisc boot
rc_add syslog boot

rc_add mount-ro shutdown
rc_add killprocs shutdown
rc_add savecache shutdown

tar -c -C "$tmp" etc | gzip -9n > "$HOSTNAME.apkovl.tar.gz"
