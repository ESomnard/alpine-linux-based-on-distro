# plume-wifi — panneau wifi HUD (iwd par iwctl), sans GTK
PREFIX  ?= /usr/local
PKGS     = wayland-client wayland-cursor xkbcommon cairo pangocairo
CFLAGS  ?= -O2
CFLAGS  += -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation $(shell pkg-config --cflags $(PKGS))
LDLIBS  += $(shell pkg-config --libs $(PKGS)) -lm

WL_PROTOCOLS := $(shell pkg-config --variable=pkgdatadir wayland-protocols)
SCANNER      := $(shell pkg-config --variable=wayland_scanner wayland-scanner 2>/dev/null || echo wayland-scanner)
XDG_XML       = $(WL_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml

OBJ = main.o ui.o iwd.o xdg-shell-protocol.o

all: plume-wifi

plume-wifi: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

$(OBJ): xdg-shell-client-protocol.h wifi.h

xdg-shell-client-protocol.h:
	$(SCANNER) client-header $(XDG_XML) $@
xdg-shell-protocol.c:
	$(SCANNER) private-code $(XDG_XML) $@

install: all
	install -Dm755 plume-wifi $(DESTDIR)$(PREFIX)/bin/plume-wifi
	install -Dm644 plume-wifi.desktop $(DESTDIR)$(PREFIX)/share/applications/plume-wifi.desktop

clean:
	rm -f plume-wifi *.o *-protocol.c *-client-protocol.h

.PHONY: all install clean
