#!/bin/sh
set -eu
apk add -q gcc musl-dev make pkgconf wayland-dev wayland-protocols wlr-protocols cairo-dev pango-dev libxkbcommon-dev \
	libvterm-dev librsvg-dev gdk-pixbuf-dev sway grim jq font-dejavu font-jetbrains-mono dbus wtype >/dev/null 2>&1
for d in son wifi logitheque fonds term; do
	rm -rf /b && cp -r /w/$d /b && make -s -C /b clean >/dev/null 2>&1 || true
	make -s -C /b CPPFLAGS=-DPLUME_PASTILLES 2>&1 | grep -iE "warning|error" || true
	make -s -C /b install PREFIX=/usr/local CPPFLAGS=-DPLUME_PASTILLES >/dev/null
done
install -m755 /w/tests/fake-wpctl /usr/local/bin/wpctl
install -m755 /w/tests/fake-iwctl /usr/local/bin/iwctl
WLR=$(pkg-config --variable=pkgdatadir wlr-protocols)/unstable/wlr-virtual-pointer-unstable-v1.xml
mkdir -p /vp && cd /vp && wayland-scanner client-header "$WLR" wlr-virtual-pointer-unstable-v1-client-protocol.h \
	&& wayland-scanner private-code "$WLR" vp-protocol.c \
	&& gcc -O2 -I. -o /usr/local/bin/vpointer /w/tests/vpointer.c vp-protocol.c $(pkg-config --cflags --libs wayland-client) && cd /
adduser -D esomnard
export XDG_RUNTIME_DIR=/tmp/rt && mkdir -pm700 $XDG_RUNTIME_DIR && chown esomnard $XDG_RUNTIME_DIR
# comme dans Plume : panneaux flottants centrés
printf 'output HEADLESS-1 resolution 1280x760\noutput * bg #02060b solid_color\ndefault_border none\ndefault_floating_border none\nfor_window [app_id="plume-son"] floating enable, resize set 560 600, move position center\nfor_window [app_id="plume-wifi"] floating enable, resize set 560 680, move position center\n' > /tmp/sway.conf
U="export HOME=/home/esomnard XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR LANG=fr_FR.UTF-8; cd"
su esomnard -s /bin/sh -c "$U; WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 dbus-run-session sway -c /tmp/sway.conf > /tmp/sway.log 2>&1 &"
sleep 4
W="$U; export WAYLAND_DISPLAY=wayland-1 SWAYSOCK=\$(ls $XDG_RUNTIME_DIR/sway-ipc.*.sock)"
run() { su esomnard -s /bin/sh -c "$W; $*"; }
# survol : la souris virtuelle reste 4 s en place pendant la capture
survol() { run "vpointer 1280 760 move $1 $2 4000 &"; sleep 2.5; }
click() { run "vpointer 1280 760 click $1 $2"; sleep 1.5; }
dblclick() { run "vpointer 1280 760 dblclick $1 $2"; sleep 1.5; }
arbre() { run "swaymsg -t get_tree -r"; }
ok() { echo "OK  $*"; }
ko() { echo "ÉCHEC  $*"; FAIL=1; }
FAIL=0

# --- son : fenêtre 560×600 centrée (x=360, y=80) ; pastilles à y = 80 + 32, x = 360 + 24 + 6.5 (+20, +40)
run "plume-son > /tmp/son.log 2>&1 &"; sleep 4
run "grim /out/1-son.png"
survol 400 112; run "grim /out/2-son-survol.png"
run "grim -g '360,80 300x64' /out/2b-son-survol-zoom.png"
click 411 112   # jaune : réduire
arbre | jq -e '[.. | objects | select(.name? == "__i3_scratch") | .. | objects | select(.app_id? == "plume-son")] | length == 1' >/dev/null \
	&& ok "jaune : fenêtre réduite (rangée de côté)" || ko "jaune : fenêtre pas réduite"
run "grim /out/3-son-reduit.png"
run "plume-son >/dev/null 2>&1"; sleep 2  # relancer depuis la barre : la même fenêtre revient
n=$(pgrep -x plume-son | wc -l); [ "$n" = 1 ] && ok "relance : pas de 2e fenêtre" || ko "relance : $n fenêtres"
arbre | jq -e '[.. | objects | select(.app_id? == "plume-son" and .visible == true)] | length == 1' >/dev/null \
	&& ok "relance : la fenêtre réduite revient" || ko "relance : la fenêtre reste cachée"
run "grim /out/4-son-revenu.png"
click 431 112   # verte : plein écran
arbre | jq -e '[.. | objects | select(.app_id? == "plume-son" and .fullscreen_mode == 1)] | length == 1' >/dev/null \
	&& ok "verte : plein écran" || ko "verte : pas de plein écran"
run "grim /out/5-son-plein-ecran.png"
sleep 1.5; click 71 32     # verte (en plein écran, les pastilles sont en haut à gauche de l'écran)
arbre | jq -e '[.. | objects | select(.app_id? == "plume-son" and .fullscreen_mode == 0)] | length == 1' >/dev/null \
	&& ok "verte encore : retour à la taille normale" || ko "verte : reste en plein écran"
# double-clic dans le vide de l'en-tête (comme sur Mac) : plein écran, puis retour
plein() { arbre | jq -e "[.. | objects | select(.app_id? == \"$1\" and .fullscreen_mode == $2)] | length == 1" >/dev/null; }
dblclick 760 112; plein plume-son 1 && ok "double-clic sur l'en-tête : plein écran" || ko "double-clic : pas de plein écran"
dblclick 760 32;  plein plume-son 0 && ok "double-clic encore : taille normale" || ko "double-clic : reste en plein écran"
click 760 112;    plein plume-son 0 && ok "simple clic sur l'en-tête : rien ne change" || ko "simple clic : plein écran à tort"
sleep 1
# tenir l'en-tête et glisser (comme sur Mac) : la fenêtre suit la souris
posx() { arbre | jq -r "[.. | objects | select(.app_id? == \"$1\")][0].rect.x"; }
x0=$(posx plume-son)
run "vpointer 1280 760 drag 760 112 860 162"; sleep 1.5
x1=$(posx plume-son)
[ "$x1" -ge $((x0 + 80)) ] && ok "glisser l'en-tête : fenêtre déplacée ($x0 -> $x1)" || ko "glisser : fenêtre pas déplacée ($x0 -> $x1)"
run "grim /out/5b-son-deplace.png"
click $((391 + x1 - x0)) 162   # rouge : fermer (à la nouvelle place)
sleep 1
click 391 112   # (déjà fermée : sans effet)
pgrep -x plume-son >/dev/null && ko "rouge : l'appli ne se ferme pas" || ok "rouge : appli fermée"

# --- autres applis : captures avec la souris sur les pastilles
run "plume-wifi > /tmp/wifi.log 2>&1 &"; sleep 5
survol 400 72; run "grim /out/6-wifi.png"
sleep 2; click 391 72
pgrep -x plume-wifi >/dev/null && ko "wifi : rouge ne ferme pas" || ok "wifi : rouge ferme"
# terminal : double-clic dans le vide de la barre d'onglets
run "plume-term > /tmp/t0.log 2>&1 &"; sleep 5
dblclick 800 22; plein plume-term 1 && ok "terminal : double-clic sur la barre d'onglets = plein écran" || ko "terminal : double-clic sans effet"
dblclick 800 22; sleep 1
run "vpointer 1280 760 drag 800 22 700 200"; sleep 1.5
arbre | jq -e '[.. | objects | select(.app_id? == "plume-term" and .type == "floating_con")] | length == 1' >/dev/null \
	&& ok "terminal : glisser la barre d'onglets = fenêtre libre déplacée" || ko "terminal : glisser sans effet"
run "grim /out/5c-term-deplace.png"
run "swaymsg -q '[app_id=\"^plume-term\$\"] kill'" || true; sleep 1
for a in plume-logitheque plume-fonds plume-term; do
	run "$a > /tmp/$a.log 2>&1 &"; sleep 6
	survol 40 32; run "grim /out/7-$a.png"; sleep 2
	run "swaymsg -q '[app_id=\"^$a\$\"] kill'" || true; sleep 1
done
tail -3 /tmp/son.log /tmp/plume-term.log 2>/dev/null || true
[ $FAIL = 0 ] && echo "=== TOUT OK ===" || echo "=== AU MOINS UN ÉCHEC ==="
