# plume-fonds — choisir le fond d'écran (fonds Plume, couleurs, images), style HUD
PREFIX  ?= /usr/local
PKGS     = wayland-client wayland-cursor xkbcommon cairo pangocairo gdk-pixbuf-2.0
CFLAGS  ?= -O2
CFLAGS  += -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation $(shell pkg-config --cflags $(PKGS))
LDLIBS  += $(shell pkg-config --libs $(PKGS)) -lm

WL_PROTOCOLS := $(shell pkg-config --variable=pkgdatadir wayland-protocols)
SCANNER      := $(shell pkg-config --variable=wayland_scanner wayland-scanner 2>/dev/null || echo wayland-scanner)
XDG_XML       = $(WL_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml

OBJ = main.o ui.o images.o gen.o xdg-shell-protocol.o

all: plume-fonds

plume-fonds: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

$(OBJ): xdg-shell-client-protocol.h fonds.h

xdg-shell-client-protocol.h:
	$(SCANNER) client-header $(XDG_XML) $@
xdg-shell-protocol.c:
	$(SCANNER) private-code $(XDG_XML) $@

install: all
	install -Dm755 plume-fonds $(DESTDIR)$(PREFIX)/bin/plume-fonds
	install -Dm644 plume-fonds.desktop $(DESTDIR)$(PREFIX)/share/applications/plume-fonds.desktop
	./plume-fonds --generer $(DESTDIR)$(PREFIX)/share/plume/fonds

clean:
	rm -f plume-fonds *.o *-protocol.c *-client-protocol.h

.PHONY: all install clean
