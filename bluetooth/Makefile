# plume-bluetooth — panneau Bluetooth HUD (bluez par bluetoothctl), sans GTK
PREFIX  ?= /usr/local
PKGS     = wayland-client wayland-cursor xkbcommon cairo pangocairo
CFLAGS  ?= -O2
CFLAGS  += -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation $(shell pkg-config --cflags $(PKGS))
LDLIBS  += $(shell pkg-config --libs $(PKGS)) -lm

WL_PROTOCOLS := $(shell pkg-config --variable=pkgdatadir wayland-protocols)
SCANNER      := $(shell pkg-config --variable=wayland_scanner wayland-scanner 2>/dev/null || echo wayland-scanner)
XDG_XML       = $(WL_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml

OBJ = main.o ui.o bt.o run.o kit.o xdg-shell-protocol.o

all: plume-bluetooth

plume-bluetooth: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

$(OBJ): xdg-shell-client-protocol.h panel.h panel-base.h

xdg-shell-client-protocol.h:
	$(SCANNER) client-header $(XDG_XML) $@
xdg-shell-protocol.c:
	$(SCANNER) private-code $(XDG_XML) $@

install: all
	install -Dm755 plume-bluetooth $(DESTDIR)$(PREFIX)/bin/plume-bluetooth
	install -Dm644 plume-bluetooth.desktop $(DESTDIR)$(PREFIX)/share/applications/plume-bluetooth.desktop

clean:
	rm -f plume-bluetooth *.o *-protocol.c *-client-protocol.h

.PHONY: all install clean
