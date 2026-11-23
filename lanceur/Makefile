# plume-lanceur — toutes les applications en grille (à la GNOME), style HUD
PREFIX  ?= /usr/local
PKGS     = wayland-client wayland-cursor xkbcommon cairo pangocairo librsvg-2.0
CFLAGS  ?= -O2
CFLAGS  += -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation $(shell pkg-config --cflags $(PKGS))
LDLIBS  += $(shell pkg-config --libs $(PKGS)) -lm

WL_PROTOCOLS  := $(shell pkg-config --variable=pkgdatadir wayland-protocols)
WLR_PROTOCOLS ?= $(shell pkg-config --variable=pkgdatadir wlr-protocols 2>/dev/null || echo /usr/share/wlr-protocols)
SCANNER       := $(shell pkg-config --variable=wayland_scanner wayland-scanner 2>/dev/null || echo wayland-scanner)
LAYER_XML      = $(WLR_PROTOCOLS)/unstable/wlr-layer-shell-unstable-v1.xml
XDG_XML        = $(WL_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml

PROTO_H = wlr-layer-shell-unstable-v1-client-protocol.h xdg-shell-client-protocol.h
OBJ = main.o apps.o draw.o wlr-layer-shell-unstable-v1-protocol.o xdg-shell-protocol.o

all: plume-lanceur

plume-lanceur: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

$(OBJ): $(PROTO_H) lanceur.h

wlr-layer-shell-unstable-v1-client-protocol.h:
	$(SCANNER) client-header $(LAYER_XML) $@
wlr-layer-shell-unstable-v1-protocol.c:
	$(SCANNER) private-code $(LAYER_XML) $@
xdg-shell-client-protocol.h:
	$(SCANNER) client-header $(XDG_XML) $@
xdg-shell-protocol.c:
	$(SCANNER) private-code $(XDG_XML) $@

install: all
	install -Dm755 plume-lanceur $(DESTDIR)$(PREFIX)/bin/plume-lanceur

clean:
	rm -f plume-lanceur *.o *-protocol.c *-client-protocol.h

.PHONY: all install clean
