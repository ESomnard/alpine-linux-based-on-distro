#!/bin/sh
# Test automatique complet dans une VM QEMU lancée dans Docker (aucune install sur l'hôte).
# Résultats : out/test/vm.log (console complète) et out/test/screenshot.png (bureau sway).
set -eu
cd "$(dirname "$0")/.."

ISO="$(ls -t out/*.iso 2>/dev/null | head -1)"
[ -n "$ISO" ] || { echo "Aucune ISO dans out/ — lance d'abord 'make iso'." >&2; exit 1; }
rm -rf out/test && mkdir -p out/test

docker run --rm --platform linux/amd64 \
	-v "$PWD/out":/o -v "$PWD/tests":/tests:ro \
	alpine:latest sh -c "
		set -e
		apk add -q --no-cache qemu-system-x86_64 qemu-img python3 py3-pexpect xorriso >/dev/null
		cd /tmp
		xorriso -osirrox on -indev /o/$(basename "$ISO") \
			-extract /boot/vmlinuz-lts vmlinuz -extract /boot/initramfs-lts initramfs >/dev/null 2>&1
		qemu-img create -q -f qcow2 /o/test/disk.qcow2 8G
		python3 -u -I /tests/vm-test.py /o/$(basename "$ISO")
	"

# Récupère les captures d'écran transmises en base64 par la console série
for name in greeter bureau terminal logitheque verrou; do
	tr -d '\r' < out/test/vm.log | sed -n "/^===B64START $name===\$/,/^===B64END===\$/p" \
		| sed '1d;$d' | base64 -d > "out/test/$name.png" 2>/dev/null || true
	[ -s "out/test/$name.png" ] && echo "Capture : out/test/$name.png" || rm -f "out/test/$name.png"
done
grep -a '^### ' out/test/vm.log | tr -d '\r' || true
