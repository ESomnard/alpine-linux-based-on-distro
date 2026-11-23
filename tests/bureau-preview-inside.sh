#!/bin/sh
set -eu
apk add -q gcc musl-dev make pkgconf wayland-dev wayland-protocols wlr-protocols cairo-dev pango-dev libxkbcommon-dev \
	librsvg-dev gdk-pixbuf-dev sway grim font-dejavu dbus wtype imagemagick >/dev/null 2>&1
rm -rf /b && cp -r /w/void/bureau /b && make -s -C /b WLR_PROTOCOLS=/w/void/protocoles 2>&1 | grep -iE "warning|error" || true
make -s -C /b install PREFIX=/usr/local WLR_PROTOCOLS=/w/void/protocoles >/dev/null
mkdir -p /usr/share/icons/hicolor/scalable/apps && cp /w/void/icones/*.svg /usr/share/icons/hicolor/scalable/apps/
# faux xdg-open : note ce qu'on lui demande d'ouvrir
printf '#!/bin/sh\necho "$@" >> /tmp/xdg.log\n' > /usr/local/bin/xdg-open && chmod 755 /usr/local/bin/xdg-open
WLR=$(pkg-config --variable=pkgdatadir wlr-protocols)/unstable/wlr-virtual-pointer-unstable-v1.xml
mkdir -p /vp && cd /vp && wayland-scanner client-header "$WLR" wlr-virtual-pointer-unstable-v1-client-protocol.h \
	&& wayland-scanner private-code "$WLR" vp-protocol.c \
	&& gcc -O2 -I. -o /usr/local/bin/vpointer /w/tests/vpointer.c vp-protocol.c $(pkg-config --cflags --libs wayland-client) && cd /
adduser -D esomnard
H=/home/esomnard; D=$H/Bureau
mkdir -p $D "$D/Dossier test" $H/.config/plume
echo "%PDF-1.4" > $D/rapport.pdf; echo notes > $D/notes.txt; echo a > $D/a-supprimer.txt; echo b > $D/b-supprimer.txt
convert -size 300x200 gradient:'#ff6ec4-#4fb8e0' $D/photo.png
printf '[Desktop Entry]\nType=Application\nName=Mon appli\nExec=touch /tmp/appli-lancee\nIcon=plume-wifi\n' > $D/appli.desktop
# places fixes (col 0 = à droite) pour connaître les positions à l'écran
printf '0\t0\tDossier test\n0\t1\trapport.pdf\n0\t2\tnotes.txt\n1\t0\tphoto.png\n1\t1\tappli.desktop\n2\t0\ta-supprimer.txt\n2\t1\tb-supprimer.txt\n' > $H/.config/plume/bureau-places
chown -R esomnard $H
export XDG_RUNTIME_DIR=/tmp/rt && mkdir -pm700 $XDG_RUNTIME_DIR && chown esomnard $XDG_RUNTIME_DIR
printf 'output HEADLESS-1 resolution 1280x760\noutput * bg #1d3b5c solid_color\n' > /tmp/sway.conf
U="export HOME=$H XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR LANG=fr_FR.UTF-8; cd"
su esomnard -s /bin/sh -c "$U; WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 dbus-run-session sway -c /tmp/sway.conf > /tmp/sway.log 2>&1 &"
sleep 4
W="$U; export WAYLAND_DISPLAY=wayland-1 SWAYSOCK=\$(ls $XDG_RUNTIME_DIR/sway-ipc.*.sock)"
run() { su esomnard -s /bin/sh -c "$W; $*"; }
ok() { echo "OK  $*"; }
ko() { echo "ÉCHEC  $*"; FAIL=1; }
FAIL=0
T=$H/.local/share/Trash/files
# grille : case (col, ligne) -> x = 1280-18-(col+1)*104 .. +104 ; y = 18 + ligne*112 .. +112
run "plume-icones-bureau > /tmp/b.log 2>&1 &"; sleep 4
run "grim /out/1-bureau.png"

# 1) glisser dans le vide avec le bouton GAUCHE : sélectionne a- et b-supprimer (colonne 2), puis Suppr
run "vpointer 1280 760 drag 900 8 1030 250"; sleep 1
run "grim /out/2-selection-gauche.png"
run "wtype -k Delete"; sleep 1.5
[ -f "$T/a-supprimer.txt" ] && [ -f "$T/b-supprimer.txt" ] && [ ! -e "$D/a-supprimer.txt" ] \
	&& ok "glisser (gauche) + Suppr : 2 fichiers à la corbeille" || ko "sélection gauche / corbeille"
[ -f "$H/.local/share/Trash/info/a-supprimer.txt.trashinfo" ] && ok "corbeille au format standard (restaurable)" || ko "fichier .trashinfo absent"

# 2) glisser avec le bouton DROIT (comme demandé) : rapport.pdf + notes.txt, puis Suppr
run "vpointer 1280 760 drag 1158 128 1272 360 right"; sleep 1
run "grim /out/3-selection-droite.png"
run "wtype -k Delete"; sleep 1.5
[ -f "$T/rapport.pdf" ] && [ -f "$T/notes.txt" ] && ok "glisser (droit) + Suppr : sélection de groupe" || ko "sélection avec le bouton droit"

# 3) clic droit dans le vide -> Nouveau dossier, puis on tape son nom
run "vpointer 1280 760 menu 600 400 680 420 2500 &"; sleep 2; run "grim /out/4-menu-vide.png"; sleep 2.5
run "grim /out/5-nouveau-dossier.png"
run "wtype 'Photos de vacances' -k Return"; sleep 1.5
[ -d "$D/Photos de vacances" ] && ok "clic droit > Nouveau dossier, nom tapé tout de suite" || { ko "nouveau dossier"; ls $D; }

# 4) double-clic : image -> ouverte (xdg-open) ; raccourci d'appli -> lancée
run "vpointer 1280 760 dblclick 1106 70"; sleep 1.5
grep -q "photo.png" /tmp/xdg.log 2>/dev/null && ok "double-clic sur une image : ouverte" || ko "double-clic image"
run "vpointer 1280 760 dblclick 1106 182"; sleep 1.5
[ -f /tmp/appli-lancee ] && ok "double-clic sur un raccourci d'appli : lancée" || ko "double-clic appli"

# 5) déplacer « Dossier test » de 3 colonnes à gauche et 2 lignes plus bas
run "vpointer 1280 760 drag 1210 60 898 284"; sleep 1.5
grep -q "^3	2	Dossier test$" $H/.config/plume/bureau-places && ok "icône déplacée, place gardée" || { ko "déplacement"; cat $H/.config/plume/bureau-places; }
run "grim /out/6-deplace.png"

# 6) clic droit sur l'icône déplacée -> Renommer -> « Projets »
run "vpointer 1280 760 menu 898 284 960 341 2500 &"; sleep 2; run "grim /out/7-menu-icone.png"; sleep 2.5
run "wtype 'Projets' -k Return"; sleep 1.5
[ -d "$D/Projets" ] && [ ! -e "$D/Dossier test" ] && ok "clic droit > Renommer" || { ko "renommer"; ls $D; }

# 7) un fichier créé par une autre appli apparaît tout seul
echo x > "$D/arrive-tout-seul.txt"; chown esomnard "$D/arrive-tout-seul.txt"; sleep 1.5
grep -q "arrive-tout-seul.txt" $H/.config/plume/bureau-places && ok "nouveau fichier affiché tout de suite (inotify)" || ko "inotify"
run "grim /out/8-final.png"
pgrep -x plume-icones-bur >/dev/null || pgrep -f plume-icones-bureau >/dev/null && ok "toujours en marche" || { ko "arrêté"; cat /tmp/b.log; }
[ $FAIL = 0 ] && echo "=== TOUT OK ===" || echo "=== AU MOINS UN ÉCHEC ==="
