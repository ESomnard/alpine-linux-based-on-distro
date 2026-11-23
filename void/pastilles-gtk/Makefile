# Plume Void — pastilles de fenêtre pour les applis GTK 3 (module GTK)
PREFIX ?= /usr
CFLAGS ?= -O2 -Wall -Wextra
PKGS = gtk+-3.0
LIB = libplume-pastilles.so

all: $(LIB)

$(LIB): pastilles.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -shared $$(pkg-config --cflags $(PKGS)) -o $@ pastilles.c $$(pkg-config --libs $(PKGS))

install: $(LIB)
	install -Dm755 $(LIB) $(DESTDIR)$(PREFIX)/lib/gtk-3.0/modules/$(LIB)

clean:
	rm -f $(LIB)

.PHONY: all install clean
