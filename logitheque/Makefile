# Logithèque Plume — installer des applications (Alpine + Flathub), style HUD
PREFIX  ?= /usr/local
PKGS     = wayland-client wayland-cursor xkbcommon cairo pangocairo
CFLAGS  ?= -O2
CFLAGS  += -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation $(shell pkg-config --cflags $(PKGS))
LDLIBS  += $(shell pkg-config --libs $(PKGS)) -lm

WL_PROTOCOLS := $(shell pkg-config --variable=pkgdatadir wayland-protocols)
SCANNER      := $(shell pkg-config --variable=wayland_scanner wayland-scanner 2>/dev/null || echo wayland-scanner)
XDG_XML       = $(WL_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml

OBJ = main.o ui.o backend.o proc.o xdg-shell-protocol.o

all: plume-logitheque

plume-logitheque: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

$(OBJ): xdg-shell-client-protocol.h logitheque.h

xdg-shell-client-protocol.h:
	$(SCANNER) client-header $(XDG_XML) $@
xdg-shell-protocol.c:
	$(SCANNER) private-code $(XDG_XML) $@

install: all
	install -Dm755 plume-logitheque $(DESTDIR)$(PREFIX)/bin/plume-logitheque
	install -Dm644 catalogue.txt $(DESTDIR)$(PREFIX)/share/plume/logitheque.txt
	install -Dm644 plume-logitheque.desktop $(DESTDIR)$(PREFIX)/share/applications/plume-logitheque.desktop

clean:
	rm -f plume-logitheque *.o *-protocol.c *-client-protocol.h

.PHONY: all install clean
