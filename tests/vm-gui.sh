#!/bin/sh
# Démarre Plume dans une VM graphique (QEMU dans Docker) accessible en VNC.
# macOS : Finder > Aller > Se connecter au serveur… > vnc://localhost:5901 (mot de passe : plume)
#
#   ./tests/vm-gui.sh          démarre le disque installé (créé par `make test`)
#   ./tests/vm-gui.sh live     démarre l'ISO live (pour tester l'installation toi-même)
#   PLUME=void ./tests/vm-gui.sh [live]   même chose pour Plume Void (make test-void)
set -eu
cd "$(dirname "$0")/.."

MODE="${1:-disk}"
if [ "${PLUME:-alpine}" = void ]; then
	ISO="$(ls -t out/plume-void-*.iso 2>/dev/null | head -1)" T=out/test-void N=plume-void MT="make test-void" MI="make iso-void"
else
	ISO="$(ls -t out/alpine-plume-*.iso 2>/dev/null | head -1)" T=out/test N=plume MT="make test" MI="make iso"
fi
mkdir -p out/vm

case "$MODE" in
	disk)
		[ -f $T/disk.qcow2 ] || { echo "Pas de disque installé : lance d'abord '$MT'." >&2; exit 1; }
		if docker ps --format '{{.Command}}' --no-trunc 2>/dev/null | grep -q 'vm-test'; then
			echo "Un test (make test) est en train d'installer le disque : attends qu'il soit fini." >&2
			exit 1
		fi
		if ! grep -q 'SUCCES' $T/vm.log 2>/dev/null; then
			echo "Le dernier test n'a pas réussi : le disque est peut-être incomplet. Relance '$MT'." >&2
			exit 1
		fi
		# copie de travail : le disque du test reste intact
		[ -f out/vm/$N.qcow2 ] || cp $T/disk.qcow2 out/vm/$N.qcow2
		BOOT="-drive file=/o/vm/$N.qcow2,if=virtio"
		;;
	live)
		[ -n "$ISO" ] || { echo "Aucune ISO : lance '$MI'." >&2; exit 1; }
		[ -f out/vm/live-disk-$N.qcow2 ] || docker run --rm -v "$PWD/out":/o alpine:latest \
			sh -c "apk add -q qemu-img >/dev/null && qemu-img create -q -f qcow2 /o/vm/live-disk-$N.qcow2 8G"
		BOOT="-cdrom /o/$(basename "$ISO") -boot once=d -drive file=/o/vm/live-disk-$N.qcow2,if=virtio"
		;;
	*) echo "usage: $0 [disk|live]" >&2; exit 1 ;;
esac

echo "VM en cours de démarrage (émulation logicielle : c'est lent, sois patient)."
echo "Connexion : vnc://localhost:5901   mot de passe : plume"
echo "Arrêt     : Ctrl+C ici, ou éteindre depuis la VM."

# terminal interactif -> on reste attaché ; sinon la VM tourne en arrière-plan
[ -t 0 ] && IT=-it || IT=-d
exec docker run --rm $IT --name plume-vm --platform linux/amd64 \
	-p 127.0.0.1:5901:5901 -v "$PWD/out":/o \
	alpine:latest sh -c "
		apk add -q --no-cache qemu-system-x86_64 qemu-hw-display-virtio-vga qemu-hw-display-virtio-gpu >/dev/null 2>&1 \
			|| apk add -q --no-cache qemu-system-x86_64 >/dev/null
		exec qemu-system-x86_64 -accel tcg,thread=multi -cpu max -smp 4 -m 3072 \
			$BOOT -nic user,model=virtio-net-pci \
			-vga virtio -usb -device usb-tablet \
			-k fr -object secret,id=vncpw,data=plume -vnc 0.0.0.0:1,password-secret=vncpw
	"
