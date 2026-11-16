#!/bin/sh
set -eu
sed -i 's|^#\(.*community\)|\1|' /etc/apk/repositories
apk add -q gcc musl-dev make pkgconf wayland-dev wayland-protocols wlr-protocols cairo-dev pango-dev libxkbcommon-dev \
	gdk-pixbuf-dev sway swaybg grim font-dejavu font-jetbrains-mono dbus imagemagick wtype >/dev/null 2>&1
install -m755 /rootfs/etc/plume/conf/plume-theme /usr/local/bin/plume-theme
cp -r /fonds /bf && make -s -C /bf >/dev/null && make -s -C /bf install PREFIX=/usr/local >/dev/null
WLR=$(pkg-config --variable=pkgdatadir wlr-protocols)/unstable/wlr-virtual-pointer-unstable-v1.xml
mkdir -p /vp && cd /vp && wayland-scanner client-header "$WLR" wlr-virtual-pointer-unstable-v1-client-protocol.h \
	&& wayland-scanner private-code "$WLR" vp-protocol.c \
	&& gcc -O2 -I. -o /usr/local/bin/vpointer /tests/vpointer.c vp-protocol.c $(pkg-config --cflags --libs wayland-client) && cd /
adduser -D esomnard
mkdir -p /home/esomnard/Images/vacances
# de vraies photos JPEG pour « Mes images »
magick -size 1920x1080 gradient:'#ff7e5f-#2b5876' /home/esomnard/Images/coucher-de-soleil.jpg
magick -size 1600x1200 plasma:fractal -blur 0x2 /home/esomnard/Images/vacances/plage.jpg
magick -size 400x400 pattern:checkerboard -fill '#4fb8e0' -colorize 40 /home/esomnard/Images/motif.png
chown -R esomnard /home/esomnard
export XDG_RUNTIME_DIR=/tmp/rt && mkdir -pm700 $XDG_RUNTIME_DIR && chown esomnard $XDG_RUNTIME_DIR
printf 'output HEADLESS-1 resolution 1280x760\noutput * bg #02060b solid_color\nexec_always plume-fonds --appliquer\ndefault_border none\n' > /tmp/sway.conf
U="export HOME=/home/esomnard XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR LANG=fr_FR.UTF-8; cd"
su esomnard -s /bin/sh -c "$U; WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 dbus-run-session sway -c /tmp/sway.conf > /tmp/sway.log 2>&1 &"
sleep 4
W="$U; export WAYLAND_DISPLAY=wayland-1 SWAYSOCK=\$(ls $XDG_RUNTIME_DIR/sway-ipc.*.sock)"
run() { su esomnard -s /bin/sh -c "$W; $*"; }
click() { run "vpointer 1280 760 click $1 $2"; sleep 1.5; }
ok() { echo "OK  $*"; }
ko() { echo "ÉCHEC  $*"; FAIL=1; }
FAIL=0
CONF=/home/esomnard/.config/plume/fond.conf

run "plume-fonds > /tmp/f.log 2>&1 &"; sleep 5
run "grim /out/1-fenetre.png"
click 387 558                                   # fond « Aurore »
grep -q "fond=.*aurore.png" $CONF && ok "Aurore appliqué et enregistré" || ko "Aurore non enregistré"
click 780 252                                   # mode « Ajuster »
click 842 340                                   # pastille de thème « Rouge »
grep -q "accent=ff5470" /home/esomnard/.config/plume/theme.conf && ok "thème Rouge enregistré" || ko "thème non enregistré"
run "grim /out/2b-theme-rouge.png"
click 634 340                                   # retour au thème Cyan
grep -q "mode=fit" $CONF && ok "mode Ajuster enregistré" || ko "mode non enregistré"
run "grim /out/2-aurore.png"
run "wtype -s 400 -k Next -k Next"; sleep 2
run "grim /out/3-mes-images.png"
click 640 575                                   # photo « coucher-de-soleil.jpg »
grep -q "coucher-de-soleil.jpg" $CONF && ok "photo JPEG appliquée" || ko "photo non appliquée"
run "wtype -s 400 -k Escape"; sleep 1.5
pgrep -x plume-fonds >/dev/null && ko "Échap ne ferme pas" || ok "Échap ferme"
run "grim /out/4-bureau-photo.png"
run "swaymsg output '*' bg '#ff0000' solid_color" >/dev/null; sleep 1
run "swaymsg reload" >/dev/null; sleep 3
run "grim /out/5-apres-rechargement.png"
grep -q "coucher-de-soleil.jpg" $CONF && ok "choix gardé après rechargement de sway" || ko "choix perdu"
rm /home/esomnard/Images/coucher-de-soleil.jpg
run "plume-fonds --appliquer" && ok "image supprimée : retour au fond par défaut, sans erreur" || ko "--appliquer en échec"
run "swaymsg reload" >/dev/null; sleep 3
grep -qi "swaynag\|config.*error" /tmp/sway.log && ko "erreur de configuration sway" || ok "aucune erreur de configuration sway"
[ "$FAIL" = 0 ] && echo "SUCCÈS" || { echo "ÉCHEC"; exit 1; }
