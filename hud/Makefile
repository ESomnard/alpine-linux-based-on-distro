# plume-hud — greeter + écran de verrouillage HUD de Plume Linux
PREFIX  ?= /usr/local
PKGS     = wayland-client wayland-cursor xkbcommon cairo pangocairo pam
CFLAGS  ?= -O2
CFLAGS  += -std=c11 -Wall -Wextra -Wno-unused-parameter $(shell pkg-config --cflags $(PKGS))
LDLIBS  += $(shell pkg-config --libs $(PKGS)) -lm

WL_PROTOCOLS := $(shell pkg-config --variable=pkgdatadir wayland-protocols)
SCANNER      := $(shell pkg-config --variable=wayland_scanner wayland-scanner 2>/dev/null || echo wayland-scanner)

PROTO_XML = $(WL_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml \
            $(WL_PROTOCOLS)/staging/ext-session-lock/ext-session-lock-v1.xml
PROTO_H   = xdg-shell-client-protocol.h ext-session-lock-v1-client-protocol.h
PROTO_C   = xdg-shell-protocol.c ext-session-lock-v1-protocol.c

OBJ = main.o draw.o sys.o auth.o xdg-shell-protocol.o ext-session-lock-v1-protocol.o

plume-hud: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

$(OBJ): $(PROTO_H) hud.h

xdg-shell-client-protocol.h:
	$(SCANNER) client-header $(WL_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@
xdg-shell-protocol.c:
	$(SCANNER) private-code $(WL_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@
ext-session-lock-v1-client-protocol.h:
	$(SCANNER) client-header $(WL_PROTOCOLS)/staging/ext-session-lock/ext-session-lock-v1.xml $@
ext-session-lock-v1-protocol.c:
	$(SCANNER) private-code $(WL_PROTOCOLS)/staging/ext-session-lock/ext-session-lock-v1.xml $@

install: plume-hud
	install -Dm755 plume-hud $(DESTDIR)$(PREFIX)/bin/plume-hud
	install -Dm644 pam.d/plume-hud $(DESTDIR)/etc/pam.d/plume-hud

clean:
	rm -f plume-hud *.o $(PROTO_H) $(PROTO_C)

.PHONY: install clean
