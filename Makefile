# Plume Linux — distro ultra-légère basée sur Alpine
ARCH ?= x86_64

.PHONY: iso depot run test vm vm-live clean

depot:          ## Construit le dépôt .apk Plume signé dans out/depot/ (via Docker)
	./build/depot.sh

iso: depot      ## Construit l'ISO dans out/ (via Docker), avec le dépôt Plume
	ARCH=$(ARCH) ./build/build-iso.sh

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
