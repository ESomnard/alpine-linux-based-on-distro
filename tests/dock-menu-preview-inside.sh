#!/bin/sh
set -eu
apk add -q gcc musl-dev make pkgconf wayland-dev wayland-protocols wlr-protocols cairo-dev pango-dev libxkbcommon-dev \
	libvterm-dev librsvg-dev sway grim jq font-dejavu font-jetbrains-mono dbus >/dev/null 2>&1
F="CPPFLAGS=-DPLUME_PASTILLES -DPLUME_VOID"
for d in shell term; do
	rm -rf /b && cp -r /w/$d /b && make -s -C /b clean >/dev/null 2>&1 || true
	make -s -C /b WLR_PROTOCOLS=/w/void/protocoles "$F" 2>&1 | grep -iE "warning|error" || true
	make -s -C /b install PREFIX=/usr/local WLR_PROTOCOLS=/w/void/protocoles "$F" >/dev/null
done
mkdir -p /usr/share/icons/hicolor/scalable/apps && cp /w/void/icones/*.svg /usr/share/icons/hicolor/scalable/apps/
WLR=$(pkg-config --variable=pkgdatadir wlr-protocols)/unstable/wlr-virtual-pointer-unstable-v1.xml
mkdir -p /vp && cd /vp && wayland-scanner client-header "$WLR" wlr-virtual-pointer-unstable-v1-client-protocol.h \
	&& wayland-scanner private-code "$WLR" vp-protocol.c \
	&& gcc -O2 -I. -o /usr/local/bin/vpointer /w/tests/vpointer.c vp-protocol.c $(pkg-config --cflags --libs wayland-client) && cd /
adduser -D esomnard
export XDG_RUNTIME_DIR=/tmp/rt && mkdir -pm700 $XDG_RUNTIME_DIR && chown esomnard $XDG_RUNTIME_DIR
printf 'output HEADLESS-1 resolution 1280x760\noutput * bg #02060b solid_color\ndefault_border none\n' > /tmp/sway.conf
U="export HOME=/home/esomnard XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR LANG=fr_FR.UTF-8; cd"
su esomnard -s /bin/sh -c "$U; WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 dbus-run-session sway -c /tmp/sway.conf > /tmp/sway.log 2>&1 &"
sleep 4
W="$U; export WAYLAND_DISPLAY=wayland-1 SWAYSOCK=\$(ls $XDG_RUNTIME_DIR/sway-ipc.*.sock)"
run() { su esomnard -s /bin/sh -c "$W; $*"; }
click() { run "vpointer 1280 760 click $1 $2 ${3:-}"; sleep 1.5; }
arbre() { run "swaymsg -t get_tree -r"; }
nterm() { arbre | jq "[.. | objects | select(.app_id? == \"plume-term\")] | length"; }
ok() { echo "OK  $*"; }
ko() { echo "ÉCHEC  $*"; FAIL=1; }
FAIL=0
# dock de Plume Void ; « plume-terminal » (script de Plume) n'existe pas ici : plume-term directement
mkdir -p /home/esomnard/.config/plume
sed 's/| plume-terminal |/| plume-term |/' /w/void/rootfs/etc/plume/dock.conf > /home/esomnard/.config/plume/dock.conf
chown -R esomnard /home/esomnard/.config
run "plume-shell > /tmp/shell.log 2>&1 &"; sleep 3
run "plume-term > /tmp/t.log 2>&1 &"; sleep 4
# dock de Plume Void (11 boutons) : icône Terminal au centre (345, 706), Firefox (403, 706).
# Le menu s'ouvre au-dessus de l'icône ; ses lignes du bas restent à la même hauteur :
#   Nouvelle fenêtre y=534 · Masquer/Afficher y=564 · Retirer du Dock y=594 · Quitter y=633
# menu X Y CHOIX_Y [capture] : clic droit sur l'icône, (capture du menu), puis clic sur le choix
menu() {
	run "vpointer 1280 760 menu $1 706 $1 $2 2500 &"; sleep 2
	[ -z "${3:-}" ] || run "grim /out/$3.png"
	sleep 2.5
}
menu 345 534 1-menu-terminal                       # Nouvelle fenêtre
[ "$(nterm)" = 2 ] && ok "menu : Nouvelle fenêtre (2 terminaux)" || ko "menu : Nouvelle fenêtre ($(nterm))"
menu 345 564 2-menu-deux-fenetres                  # Masquer
arbre | jq -e '[.. | objects | select(.name? == "__i3_scratch") | .. | objects | select(.app_id? == "plume-term")] | length == 2' >/dev/null \
	&& ok "menu : Masquer (les 2 fenêtres rangées)" || ko "menu : Masquer"
menu 345 564 3-menu-masque                         # Afficher
arbre | jq -e '[.. | objects | select(.app_id? == "plume-term" and .visible == true)] | length >= 1' >/dev/null \
	&& ok "menu : Afficher (fenêtres revenues)" || ko "menu : Afficher"
menu 345 300                                       # clic à côté : referme sans rien faire
[ "$(nterm)" = 2 ] && ok "menu : clic à côté le referme sans rien faire" || ko "menu : clic à côté a agi"
menu 345 633                                       # Quitter
sleep 1; [ "$(nterm)" = 0 ] && ok "menu : Quitter (toutes les fenêtres fermées)" || ko "menu : Quitter ($(nterm) restantes)"
menu 403 633 4-menu-firefox-ferme                  # Firefox fermé : Ouvrir · Retirer du Dock
sleep 1
if [ -f /home/esomnard/.config/plume/dock.conf ] && ! grep -q '| Firefox |' /home/esomnard/.config/plume/dock.conf \
	&& grep -q '| Terminal |' /home/esomnard/.config/plume/dock.conf; then ok "menu : Retirer du Dock (Firefox retiré, le reste gardé)"
else ko "menu : Retirer du Dock"; fi
run "grim /out/5-dock-sans-firefox.png"
# zone du milieu (comme sur Mac) : une appli ouverte mais pas épinglée apparaît après un séparateur
# (après « Retirer Firefox » : 8 épinglées | essai | ⏻ -> l appli « essai » est au centre x=872)
run "plume-term -a essai-app > /tmp/e.log 2>&1 &"; sleep 4
run "grim /out/6-dock-appli-ouverte.png"
run "vpointer 1280 760 menu 872 706 872 594 2500 &"; sleep 2; run "grim /out/7-menu-garder.png"; sleep 2.5
grep -q '| essai-app$' /home/esomnard/.config/plume/dock.conf && ok "menu : Garder dans le Dock (épinglée)" || ko "menu : Garder dans le Dock"
run "swaymsg -q '[app_id=\"^essai-app\$\"] kill'"; sleep 2
run "grim /out/8-dock-garde.png"
pgrep -x plume-shell >/dev/null && ok "plume-shell toujours en marche" || { ko "plume-shell s'est arrêté"; tail -5 /tmp/shell.log; }
[ $FAIL = 0 ] && echo "=== TOUT OK ===" || echo "=== AU MOINS UN ÉCHEC ==="
