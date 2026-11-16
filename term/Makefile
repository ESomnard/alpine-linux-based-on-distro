# plume-term — terminal HUD de Plume Linux (Wayland + libvterm)
PREFIX  ?= /usr/local
PKGS     = wayland-client wayland-cursor xkbcommon cairo pangocairo vterm
CFLAGS  ?= -O2
CFLAGS  += -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation $(shell pkg-config --cflags $(PKGS))
LDLIBS  += $(shell pkg-config --libs $(PKGS)) -lm

WL_PROTOCOLS := $(shell pkg-config --variable=pkgdatadir wayland-protocols)
SCANNER      := $(shell pkg-config --variable=wayland_scanner wayland-scanner 2>/dev/null || echo wayland-scanner)
XDG_XML       = $(WL_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml

OBJ = main.o tab.o render.o hud.o sysinfo.o xdg-shell-protocol.o

all: plume-term

plume-term: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

$(OBJ): xdg-shell-client-protocol.h term.h hud.h

xdg-shell-client-protocol.h:
	$(SCANNER) client-header $(XDG_XML) $@
xdg-shell-protocol.c:
	$(SCANNER) private-code $(XDG_XML) $@

install: all
	install -Dm755 plume-term $(DESTDIR)$(PREFIX)/bin/plume-term

clean:
	rm -f plume-term *.o *-protocol.c *-client-protocol.h

.PHONY: all install clean
