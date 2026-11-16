#!/bin/sh
set -eu
sed -i 's|^#\(.*community\)|\1|' /etc/apk/repositories
apk add -q gcc musl-dev make pkgconf wayland-dev wayland-protocols wlr-protocols cairo-dev pango-dev libxkbcommon-dev \
	librsvg-dev librsvg sway grim font-dejavu font-jetbrains-mono dbus wtype hicolor-icon-theme \
	foot mpv pcmanfm mousepad galculator imv zathura fuzzel htop >/dev/null 2>&1
for d in lanceur shell; do cp -r /$d /b$d && make -s -C /b$d >/dev/null && make -s -C /b$d install PREFIX=/usr/local >/dev/null; done
install -m755 /rootfs/etc/plume/conf/plume-bar /usr/local/bin/plume-bar
WLR=$(pkg-config --variable=pkgdatadir wlr-protocols)/unstable/wlr-virtual-pointer-unstable-v1.xml
mkdir -p /vp && cd /vp && wayland-scanner client-header "$WLR" wlr-virtual-pointer-unstable-v1-client-protocol.h \
	&& wayland-scanner private-code "$WLR" vp-protocol.c \
	&& gcc -O2 -I. -o /usr/local/bin/vpointer /tests/vpointer.c vp-protocol.c $(pkg-config --cflags --libs wayland-client) && cd /
adduser -D esomnard
cp -a /rootfs/etc/skel/. /home/esomnard/ && chown -R esomnard /home/esomnard
export XDG_RUNTIME_DIR=/tmp/rt && mkdir -pm700 $XDG_RUNTIME_DIR && chown esomnard $XDG_RUNTIME_DIR
printf 'output HEADLESS-1 resolution 1280x760\noutput * bg #02060b solid_color\ndefault_border none\nbar swaybar_command plume-bar\n' > /tmp/sway.conf
U="export HOME=/home/esomnard XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR LANG=fr_FR.UTF-8; cd"
su esomnard -s /bin/sh -c "$U; WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 dbus-run-session sway -c /tmp/sway.conf > /tmp/sway.log 2>&1 &"
sleep 5
W="$U; export WAYLAND_DISPLAY=wayland-1 SWAYSOCK=\$(ls $XDG_RUNTIME_DIR/sway-ipc.*.sock)"
run() { su esomnard -s /bin/sh -c "$W; $*"; }
ok() { echo "OK  $*"; }
ko() { echo "ÉCHEC  $*"; FAIL=1; }
FAIL=0
alive() { pgrep -x plume-lanceur >/dev/null; }

run "plume-lanceur > /tmp/l.log 2>&1 &"; sleep 2
run "grim /out/1-grille.png"
alive && ok "grille ouverte" || ko "grille non ouverte"
run "wtype -s 400 calc"; sleep 1
run "grim /out/2-recherche.png"
run "wtype -s 400 -k Return"; sleep 3
alive && ko "grille encore ouverte après Entrée" || ok "Entrée lance l'appli et ferme la grille"
run "swaymsg -t get_tree" | grep -q '"app_id": "galculator"\|"class": "Galculator"\|galculator' && ok "galculator lancé" || ko "galculator pas lancé"
run "grim /out/3-appli-lancee.png"
# bascule : lancer deux fois ferme
run "plume-lanceur > /tmp/l.log 2>&1 &"; sleep 1.5
run "plume-lanceur"; sleep 1
alive && ko "la 2e commande n'a pas fermé la grille" || ok "relancer la commande ferme la grille"
# clic sur une icône (souris virtuelle)
run "plume-lanceur > /tmp/l.log 2>&1 &"; sleep 2
run "grim /out/4-avant-clic.png"
run "vpointer 1280 760 click 330 250"; sleep 2
alive && ko "clic sur une icône : grille encore ouverte" || ok "clic sur une icône : appli lancée"
# Échap ferme
run "plume-lanceur > /tmp/l.log 2>&1 &"; sleep 1.5
run "wtype -s 400 -k Escape"; sleep 1
alive && ko "Échap ne ferme pas" || ok "Échap ferme"
echo "== journal"; cat /tmp/l.log
[ "$FAIL" = 0 ] && echo "SUCCÈS" || { echo "ÉCHEC"; exit 1; }
