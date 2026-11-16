#!/bin/sh
set -eu
apk add -q gcc musl-dev make pkgconf wayland-dev wayland-protocols wlr-protocols cairo-dev pango-dev libxkbcommon-dev \
	sway grim font-dejavu font-jetbrains-mono dbus wtype python3 >/dev/null 2>&1
for d in son bt; do cp -r /$d /b$d; make -C /b$d 2>&1 | grep -iE "warning|error" || true; make -s -C /b$d install PREFIX=/usr/local >/dev/null; done
install -m755 /tests/fake-wpctl /usr/local/bin/wpctl
install -m755 /tests/fake-bluetoothctl /usr/local/bin/bluetoothctl
WLR=$(pkg-config --variable=pkgdatadir wlr-protocols)/unstable/wlr-virtual-pointer-unstable-v1.xml
mkdir -p /vp && cd /vp && wayland-scanner client-header "$WLR" wlr-virtual-pointer-unstable-v1-client-protocol.h \
	&& wayland-scanner private-code "$WLR" vp-protocol.c \
	&& gcc -O2 -I. -o /usr/local/bin/vpointer /tests/vpointer.c vp-protocol.c $(pkg-config --cflags --libs wayland-client) && cd /
adduser -D esomnard
export XDG_RUNTIME_DIR=/tmp/rt && mkdir -pm700 $XDG_RUNTIME_DIR && chown esomnard $XDG_RUNTIME_DIR
printf 'output HEADLESS-1 resolution 1280x760\noutput * bg #02060b solid_color\ndefault_border none\ndefault_floating_border none\nfor_window [app_id="plume-son|plume-bluetooth"] floating enable, resize set 560 600, move position center\n' > /tmp/sway.conf
U="export HOME=/home/esomnard XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR LANG=fr_FR.UTF-8; cd"
su esomnard -s /bin/sh -c "$U; WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 dbus-run-session sway -c /tmp/sway.conf > /tmp/sway.log 2>&1 &"
sleep 4
W="$U; export WAYLAND_DISPLAY=wayland-1"
run() { su esomnard -s /bin/sh -c "$W; $*"; }
click() { run "vpointer 1280 760 click $1 $2"; sleep 1.5; }
ok() { echo "OK  $*"; }
ko() { echo "ÉCHEC  $*"; FAIL=1; }
FAIL=0
# fenêtre 560×600 centrée : x=360, y=80. Contenu : list_top = 80+72, section 24 px, rangée 70 px
# SORTIE : rangée 0 y=80+72+8+24=184 (curseur à y+34..+56 -> 229), rangée 1 y=254
run "plume-son > /tmp/son.log 2>&1 &"; sleep 4
run "grim /out/1-son.png"
grep -q "status" /tmp/wpctl.log && ok "son : état lu" || ko "son : pas de wpctl status"
# curseur des haut-parleurs (de x=374 à 374+560-48-28-58=800) : clic aux 3/4 -> ~75 %
click 694 229
grep -qE "set-volume 43 0\.(69|7)" /tmp/wpctl.log && ok "son : volume réglé au clic sur le curseur (75 %)" || ko "son : volume non réglé"
click 856 202                                    # bouton SON/MUET de la 1re sortie
grep -q "set-mute 43 1" /tmp/wpctl.log && ok "son : muet" || ko "son : muet non envoyé"
click 500 272                                    # ligne « Casque USB » : sortie par défaut
grep -q "set-default 48" /tmp/wpctl.log && ok "son : casque choisi comme sortie" || ko "son : sortie non changée"
sleep 2; run "grim /out/2-son-reglages.png"
grep -q "get-volume 60" /tmp/wpctl.log && ok "son : volume de Firefox lu" || ko "son : flux non lu"
run "wtype -s 300 -k Escape -s 300"; sleep 1
pgrep -x plume-son >/dev/null && ko "son : Échap ne ferme pas" || ok "son : Échap ferme"

# --- Bluetooth : MES APPAREILS rangée 0 y=184 (milieu 208) ; À PROXIMITÉ : section après
run "plume-bluetooth > /tmp/bt.log 2>&1 &"; sleep 4
run "grim /out/3-bt.png"
grep -q "info AA:BB:CC:11:22:33" /tmp/btctl.log && ok "bt : appareils lus" || ko "bt : pas d'info"
click 600 208                                    # casque : se déplie
click 460 256                                    # CONNECTER
sleep 1
grep -q "connect AA:BB:CC:11:22:33" /tmp/btctl.log && ok "bt : connexion du casque" || ko "bt : casque non connecté"
sleep 1; run "grim /out/4-bt-connecte.png"
click 752 112                                    # ⟳ rechercher
sleep 3
grep -q "scan on" /tmp/btctl.log && ok "bt : recherche lancée" || ko "bt : pas de recherche"
run "grim /out/5-bt-recherche.png"
# « Manette Xbox » : section À PROXIMITÉ, rangée 0 : y = 184 + 54 + 8 + 24 = 270 (milieu 294)
click 600 294
click 460 342                                    # ASSOCIER
sleep 2
grep -q "pair DE:AD:BE:EF:00:01" /tmp/btctl.log && grep -q "trust DE:AD:BE:EF:00:01" /tmp/btctl.log \
	&& grep -q "connect DE:AD:BE:EF:00:01" /tmp/btctl.log && ok "bt : manette associée, approuvée, connectée" || ko "bt : association ratée"
grep -q "11-22-33" /tmp/bt.log && ko "bt : appareil sans nom affiché" || true
sleep 2; run "grim /out/6-bt-manette.png"
click 840 112                                    # ACTIVÉ -> désactivé
grep -q "power off" /tmp/btctl.log && ok "bt : désactivé" || ko "bt : bouton activer/désactiver"
sleep 1; run "grim /out/7-bt-off.png"
cat /tmp/son.log /tmp/bt.log
[ "$FAIL" = 0 ] && echo "SUCCÈS" || { echo "ÉCHEC"; cat /tmp/btctl.log; exit 1; }
