# plume-shell — barre + dock HUD de Plume Linux
PREFIX  ?= /usr/local
PKGS     = wayland-client wayland-cursor cairo pangocairo
# Plume Void : logos des applis dans le dock (SVG)
ifneq ($(findstring PLUME_VOID,$(CPPFLAGS)),)
PKGS    += librsvg-2.0
endif
CFLAGS  ?= -O2
CFLAGS  += -std=c11 -Wall -Wextra -Wno-unused-parameter $(shell pkg-config --cflags $(PKGS))
LDLIBS  += $(shell pkg-config --libs $(PKGS)) -lm

WL_PROTOCOLS  := $(shell pkg-config --variable=pkgdatadir wayland-protocols)
WLR_PROTOCOLS ?= /usr/share/wlr-protocols
SCANNER       := $(shell pkg-config --variable=wayland_scanner wayland-scanner 2>/dev/null || echo wayland-scanner)
LAYER_XML      = $(WLR_PROTOCOLS)/unstable/wlr-layer-shell-unstable-v1.xml
XDG_XML        = $(WL_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml

PROTO_H = wlr-layer-shell-unstable-v1-client-protocol.h xdg-shell-client-protocol.h
OBJ = main.o draw.o ipc.o json.o sys.o menu.o wlr-layer-shell-unstable-v1-protocol.o xdg-shell-protocol.o

all: plume-shell

plume-shell: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

$(OBJ): $(PROTO_H) shell.h

wlr-layer-shell-unstable-v1-client-protocol.h:
	$(SCANNER) client-header $(LAYER_XML) $@
wlr-layer-shell-unstable-v1-protocol.c:
	$(SCANNER) private-code $(LAYER_XML) $@
xdg-shell-client-protocol.h:
	$(SCANNER) client-header $(XDG_XML) $@
xdg-shell-protocol.c:
	$(SCANNER) private-code $(XDG_XML) $@

install: all
	install -Dm755 plume-shell $(DESTDIR)$(PREFIX)/bin/plume-shell

clean:
	rm -f plume-shell *.o *-protocol.c *-client-protocol.h

.PHONY: all install clean
