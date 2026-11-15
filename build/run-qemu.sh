#!/bin/sh
# Teste l'ISO dans une VM QEMU (macOS : `brew install qemu`).
set -eu
cd "$(dirname "$0")/.."

ISO="$(ls -t out/*.iso 2>/dev/null | head -1)"
[ -n "$ISO" ] || { echo "Aucune ISO dans out/ — lance d'abord 'make iso'." >&2; exit 1; }

DISK=out/disk.qcow2
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
	-accel "$ACCEL" -cpu max -m 2048 -smp 2 \
	-cdrom "$ISO" -boot once=d \
	-drive file="$DISK",if=virtio \
	-nic user,model=virtio-net-pci \
	-vga virtio \
	$AUDIO
