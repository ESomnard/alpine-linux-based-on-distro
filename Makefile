# Plume Linux — deux versions : Alpine (musl) et Void (glibc)
ARCH ?= x86_64

.PHONY: iso depot run test vm vm-live clean iso-void depot-void test-void release isos vm-void vm-live-void run-void

depot:          ## Construit le dépôt .apk Plume signé dans out/depot/ (via Docker)
	./build/depot.sh

iso: depot      ## Construit l'ISO dans out/ (via Docker), avec le dépôt Plume
	ARCH=$(ARCH) ./build/build-iso.sh

depot-void:     ## Dépôt .xbps Plume signé (Void, glibc) dans out/depot-void/
	./void/depot.sh

iso-void:       ## ISO Plume Void (glibc) dans out/, bureau inclus (via Docker)
	./void/build-iso.sh

isos: iso iso-void  ## Les deux ISO

release:        ## Prépare la mise en téléchargement des deux ISO (GitHub Releases)
	./build/publier-release.sh

test-void:      ## Test automatique de Plume Void dans une VM
	./tests/vm-test-void.sh

vm-void:        ## VM graphique (VNC) sur le disque installé par `make test-void`
	PLUME=void ./tests/vm-gui.sh disk

vm-live-void:   ## VM graphique (VNC) sur l'ISO live Void
	PLUME=void ./tests/vm-gui.sh live

run-void:       ## Teste l'ISO Void dans QEMU (fenêtre)
	PLUME=void ./build/run-qemu.sh

run:            ## Teste l'ISO dans QEMU
	./build/run-qemu.sh

vm:             ## VM graphique (VNC) sur le disque installé par `make test`
	./tests/vm-gui.sh disk

vm-live:        ## VM graphique (VNC) sur l'ISO live
	./tests/vm-gui.sh live

clean:
	rm -rf out

test:           ## Test automatique complet dans une VM (Docker + QEMU)
	./tests/vm-test.sh
