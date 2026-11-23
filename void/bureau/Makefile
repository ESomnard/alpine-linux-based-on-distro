# plume-icones-bureau — les fichiers du Bureau sur le bureau (Plume Void)
PREFIX  ?= /usr/local
PKGS     = wayland-client wayland-cursor xkbcommon cairo pangocairo librsvg-2.0 gdk-pixbuf-2.0
CFLAGS  ?= -O2
CFLAGS  += -std=c11 -Wall -Wextra -Wno-unused-parameter $(shell pkg-config --cflags $(PKGS))
LDLIBS  += $(shell pkg-config --libs $(PKGS)) -lm

WL_PROTOCOLS  := $(shell pkg-config --variable=pkgdatadir wayland-protocols)
WLR_PROTOCOLS ?= $(shell pkg-config --variable=pkgdatadir wlr-protocols 2>/dev/null || echo /usr/share/wlr-protocols)
SCANNER       := $(shell pkg-config --variable=wayland_scanner wayland-scanner 2>/dev/null || echo wayland-scanner)
LAYER_XML      = $(WLR_PROTOCOLS)/unstable/wlr-layer-shell-unstable-v1.xml
XDG_XML        = $(WL_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml

PROTO_H = wlr-layer-shell-unstable-v1-client-protocol.h xdg-shell-client-protocol.h
OBJ = main.o wlr-layer-shell-unstable-v1-protocol.o xdg-shell-protocol.o

all: plume-icones-bureau

plume-icones-bureau: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

$(OBJ): $(PROTO_H)

wlr-layer-shell-unstable-v1-client-protocol.h:
	$(SCANNER) client-header $(LAYER_XML) $@
wlr-layer-shell-unstable-v1-protocol.c:
	$(SCANNER) private-code $(LAYER_XML) $@
xdg-shell-client-protocol.h:
	$(SCANNER) client-header $(XDG_XML) $@
xdg-shell-protocol.c:
	$(SCANNER) private-code $(XDG_XML) $@

install: all
	install -Dm755 plume-icones-bureau $(DESTDIR)$(PREFIX)/bin/plume-icones-bureau

clean:
	rm -f plume-icones-bureau *.o *-protocol.c *-client-protocol.h

.PHONY: all install clean
