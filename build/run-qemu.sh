#!/bin/sh
# Teste l'ISO dans une VM QEMU (macOS : `brew install qemu`).
set -eu
cd "$(dirname "$0")/.."

# PLUME=void ./build/run-qemu.sh : même chose pour Plume Void
if [ "${PLUME:-alpine}" = void ]; then
	ISO="$(ls -t out/plume-void-*.iso 2>/dev/null | head -1)" DISK=out/disk-void.qcow2 MI="make iso-void"
else
	ISO="$(ls -t out/alpine-plume-*.iso 2>/dev/null | head -1)" DISK=out/disk.qcow2 MI="make iso"
fi
[ -n "$ISO" ] || { echo "Aucune ISO dans out/ — lance d'abord '$MI'." >&2; exit 1; }

[ -f "$DISK" ] || qemu-img create -f qcow2 "$DISK" 8G

ACCEL=tcg
AUDIO=""
case "$(uname -s)-$(uname -m)" in
	Darwin-x86_64) ACCEL=hvf ;;
	Linux-*) [ -w /dev/kvm ] && ACCEL=kvm ;;
esac
[ "$(uname -s)" = Darwin ] && \
	AUDIO="-audiodev coreaudio,id=snd0 -device intel-hda -device hda-duplex,audiodev=snd0"

# Le CD n'est utilisé qu'au premier démarrage ; ensuite la VM démarre sur le disque.
# shellcheck disable=SC2086
exec qemu-system-x86_64 \
	-accel "$ACCEL" -cpu max -m 3072 -smp 2 \
	-cdrom "$ISO" -boot once=d \
	-drive file="$DISK",if=virtio \
	-nic user,model=virtio-net-pci \
	-vga virtio \
	$AUDIO
