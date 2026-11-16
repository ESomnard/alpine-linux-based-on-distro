#!/bin/sh
set -eu
apk add -q gcc musl-dev make pkgconf wayland-dev wayland-protocols wlr-protocols cairo-dev pango-dev libxkbcommon-dev \
	sway grim font-dejavu font-jetbrains-mono dbus wtype >/dev/null 2>&1
cp -r /wifi /bw && make -s -C /bw >/dev/null && make -s -C /bw install PREFIX=/usr/local >/dev/null
install -m755 /tests/fake-iwctl /usr/local/bin/iwctl
WLR=$(pkg-config --variable=pkgdatadir wlr-protocols)/unstable/wlr-virtual-pointer-unstable-v1.xml
mkdir -p /vp && cd /vp && wayland-scanner client-header "$WLR" wlr-virtual-pointer-unstable-v1-client-protocol.h \
	&& wayland-scanner private-code "$WLR" vp-protocol.c \
	&& gcc -O2 -I. -o /usr/local/bin/vpointer /tests/vpointer.c vp-protocol.c $(pkg-config --cflags --libs wayland-client) && cd /
adduser -D esomnard
export XDG_RUNTIME_DIR=/tmp/rt && mkdir -pm700 $XDG_RUNTIME_DIR && chown esomnard $XDG_RUNTIME_DIR
# comme dans la config de Plume : fenêtre flottante 560×680, centrée
printf 'output HEADLESS-1 resolution 1280x760\noutput * bg #02060b solid_color\ndefault_border none\ndefault_floating_border none\nfor_window [app_id="plume-wifi"] floating enable, resize set 560 680, move position center\n' > /tmp/sway.conf
U="export HOME=/home/esomnard XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR LANG=fr_FR.UTF-8; cd"
su esomnard -s /bin/sh -c "$U; WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 dbus-run-session sway -c /tmp/sway.conf > /tmp/sway.log 2>&1 &"
sleep 4
W="$U; export WAYLAND_DISPLAY=wayland-1 SWAYSOCK=\$(ls $XDG_RUNTIME_DIR/sway-ipc.*.sock)"
run() { su esomnard -s /bin/sh -c "$W; $*"; }
click() { run "vpointer 1280 760 click $1 $2"; sleep 1.5; }
ok() { echo "OK  $*"; }
ko() { echo "ÉCHEC  $*"; FAIL=1; }
FAIL=0
LOG=/tmp/iwctl.log
# fenêtre à x=360, y=40 ; rangée i de la liste : y = 40 + 156 + 54 i (milieu : +24)
ROW0=220

run "plume-wifi > /tmp/w.log 2>&1 &"; sleep 6
run "grim /out/1-liste.png"
grep -q "get-networks" $LOG && grep -q "station wlan0 scan" $LOG && ok "recherche et liste demandées" || ko "pas de recherche"
click 600 $ROW0                                  # « Maison Fibre » (meilleur signal)
run "wtype -s 60 'mauvais' -k Return"; sleep 2.5
grep -q -- "--passphrase mauvais station wlan0 connect Maison Fibre" $LOG && ok "connexion avec le mot de passe tapé" || ko "mot de passe non transmis"
run "grim /out/2-mauvais-mdp.png"
grep -q "^cur=\"\"$" /tmp/wifi-etat && ok "mauvais mot de passe : pas connecté" || ko "connecté à tort"
run "wtype -s 60 -k BackSpace -k BackSpace -k BackSpace -k BackSpace -k BackSpace -k BackSpace -k BackSpace"
run "wtype -s 60 'secret123' -k Return"; sleep 3
grep -q "^cur=\"Maison Fibre\"$" /tmp/wifi-etat && ok "connecté à « Maison Fibre » (accents et espaces OK)" || ko "pas connecté"
sleep 2; run "grim /out/3-connecte.png"
click 600 $ROW0                                  # le réseau connecté (en tête) se déplie
click 460 $((ROW0 + 54))                         # « DÉCONNECTER »
grep -q "^cur=\"\"$" /tmp/wifi-etat && ok "déconnexion" || ko "déconnexion ratée"
sleep 1; run "grim /out/4-deconnecte.png"
click 600 $((ROW0 + 54))                         # « Bureau » (connu) : se connecte sans mot de passe
run "grim /out/5-connu.png"
click 460 $((ROW0 + 54 + 54))                    # « SE CONNECTER »
grep -q -- "--dont-ask station wlan0 connect Bureau" $LOG && grep -q "^cur=\"Bureau\"$" /tmp/wifi-etat \
	&& ok "réseau connu : connexion sans mot de passe" || ko "réseau connu non connecté"
click 840 72                                     # « ACTIVÉ » -> désactivé
grep -q "set-property Powered off" $LOG && ok "wifi désactivé" || ko "bouton activer/désactiver"
sleep 2; run "grim /out/6-desactive.png"
run "wtype -k Escape"; sleep 0.5; run "wtype -k Escape"; sleep 1.5
pgrep -x plume-wifi >/dev/null && ko "Échap ne ferme pas" || ok "Échap ferme"
cat /tmp/w.log; cat $LOG
[ "$FAIL" = 0 ] && echo "SUCCÈS" || { echo "ÉCHEC"; exit 1; }
