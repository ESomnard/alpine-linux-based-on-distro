#!/bin/sh
# Test automatique de Plume Void dans une VM QEMU lancée dans Docker.
#   ./tests/vm-test-void.sh                 installation simple
#   PLUME_TEST_CHIFFRE=1 ./tests/vm-test-void.sh   disque chiffré (LUKS2)
# Résultats : out/test-void/vm.log et out/test-void/*.png
set -eu
cd "$(dirname "$0")/.."
ISO="$(ls -t out/plume-void-*.iso 2>/dev/null | head -1)"
[ -n "$ISO" ] || { echo "Aucune ISO Void dans out/ — lance d'abord 'make iso-void'." >&2; exit 1; }
if [ "${PLUME_TEST_PHASE2:-}" = 1 ]; then
	[ -f out/test-void/disk.qcow2 ] || { echo "Pas de disque installé : lance d'abord sans PLUME_TEST_PHASE2." >&2; exit 1; }
	mv -f out/test-void/vm.log out/test-void/vm-phase1.log 2>/dev/null || true
else
	rm -rf out/test-void && mkdir -p out/test-void
fi

docker run --rm --platform linux/amd64 --name plume-void-test -e PLUME_TEST_CHIFFRE -e PLUME_TEST_PHASE2 \
	-v "$PWD/out":/o -v "$PWD/tests":/tests:ro \
	alpine:latest sh -c "
		set -e
		apk add -q --no-cache qemu-system-x86_64 qemu-img python3 py3-pexpect xorriso dosfstools mtools >/dev/null
		# fausse clé USB (FAT) pour le test de montage sans mot de passe
		truncate -s 64M /tmp/usb.img && mkfs.vfat -n PLUMEUSB /tmp/usb.img >/dev/null
		echo cle-usb-lue > /tmp/bonjour.txt && mcopy -i /tmp/usb.img /tmp/bonjour.txt ::/bonjour.txt
		cd /tmp
		xorriso -osirrox on -indev /o/$(basename "$ISO") -extract /boot /tmp/isoboot >/dev/null 2>&1
		[ \"\${PLUME_TEST_PHASE2:-}\" = 1 ] || qemu-img create -q -f qcow2 /o/test-void/disk.qcow2 12G
		python3 -u -I /tests/vm-test-void.py /o/$(basename "$ISO")
	"
for name in greeter; do
	tr -d '\r' < out/test-void/vm.log | sed -n "/===B64START $name===\$/,/^===B64END===\$/p" \
		| sed '1d;$d' | base64 -d > "out/test-void/$name.png" 2>/dev/null || true
	[ -s "out/test-void/$name.png" ] && echo "Capture : out/test-void/$name.png" || rm -f "out/test-void/$name.png"
done
grep -a '^### ' out/test-void/vm.log | tr -d '\r' || true
