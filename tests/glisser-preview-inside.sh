#!/bin/sh
set -eu
apk add -q gcc musl-dev make pkgconf wayland-dev wayland-protocols wlr-protocols cairo-dev pango-dev libxkbcommon-dev \
	librsvg-dev gdk-pixbuf-dev sway grim font-dejavu dbus wtype jq pcmanfm adwaita-icon-theme >/dev/null 2>&1
rm -rf /b && cp -r /w/void/bureau /b && make -s -C /b WLR_PROTOCOLS=/w/void/protocoles 2>&1 | grep -iE "warning|error" || true
make -s -C /b install PREFIX=/usr/local WLR_PROTOCOLS=/w/void/protocoles >/dev/null
printf '#!/bin/sh\necho "imv $@" >> /tmp/apercu.log\nsleep 30\n' > /usr/local/bin/imv && chmod 755 /usr/local/bin/imv
printf '#!/bin/sh\necho "$@" >> /tmp/xdg.log\n' > /usr/local/bin/xdg-open && chmod 755 /usr/local/bin/xdg-open
WLR=$(pkg-config --variable=pkgdatadir wlr-protocols)/unstable/wlr-virtual-pointer-unstable-v1.xml
mkdir -p /vp && cd /vp && wayland-scanner client-header "$WLR" wlr-virtual-pointer-unstable-v1-client-protocol.h \
	&& wayland-scanner private-code "$WLR" vp-protocol.c \
	&& gcc -O2 -I. -o /usr/local/bin/vpointer /w/tests/vpointer.c vp-protocol.c $(pkg-config --cflags --libs wayland-client) && cd /
adduser -D u
H=/home/u; D=$H/Bureau
mkdir -p "$D/Projets" $H/Documents $H/.config/plume
echo r > $H/Documents/rapport.txt; echo c > $H/Documents/compte.txt
printf '\211PNG\r\n\032\n' > "$D/photo.png"
printf '0\t0\tProjets\n1\t0\tphoto.png\n' > $H/.config/plume/bureau-places
chown -R u $H
export XDG_RUNTIME_DIR=/tmp/rt && mkdir -pm700 $XDG_RUNTIME_DIR && chown u $XDG_RUNTIME_DIR
printf 'output HEADLESS-1 resolution 1280x760\noutput * bg #1d3b5c solid_color\nfor_window [app_id="pcmanfm"] floating enable, resize set 620 460, move position 20 20\n' > /tmp/sway.conf
U="export HOME=$H USER=u XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR LANG=fr_FR.UTF-8; cd"
su u -s /bin/sh -c "$U; WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 dbus-run-session sway -c /tmp/sway.conf > /tmp/sway.log 2>&1 &"
sleep 4
W="$U; export WAYLAND_DISPLAY=wayland-1 SWAYSOCK=\$(ls $XDG_RUNTIME_DIR/sway-ipc.*.sock)"
run() { su u -s /bin/sh -c "$W; $*"; }
ok() { echo "OK  $*"; }
ko() { echo "ÉCHEC  $*"; FAIL=1; }
FAIL=0
run "plume-icones-bureau > /tmp/b.log 2>&1 &"; sleep 3
run "pcmanfm $H/Documents > /tmp/fm.log 2>&1 &"; sleep 8
run "grim /out/1-depart.png"

# 1) Fichiers -> bureau (case libre vers x=900, y=400) : déplacé, à l'endroit lâché
run "vpointer 1280 760 drag 318 143 900 400"; sleep 2.5
[ -f "$D/rapport.txt" ] && [ ! -e "$H/Documents/rapport.txt" ] && ok "Fichiers -> bureau : rapport.txt déplacé sur le bureau" \
	|| { ko "Fichiers -> bureau"; ls $D $H/Documents; }
grep -q "^3	3	rapport.txt$" $H/.config/plume/bureau-places && ok "posé là où on l'a lâché" || { ko "place"; cat $H/.config/plume/bureau-places; }
run "grim /out/2-pose-sur-bureau.png"

# 2) Fichiers -> le dossier « Projets » du bureau (en haut à droite)
run "vpointer 1280 760 drag 226 143 1210 60 left 1200"; sleep 2.5
[ -f "$D/Projets/compte.txt" ] && ok "Fichiers -> dossier du bureau : rangé dans Projets" || { ko "dépôt sur dossier"; ls -R $D; }

# 3) bureau -> autre place du bureau (déplacement d'icône par glisser-déposer)
run "vpointer 1280 760 drag 898 400 690 620"; sleep 2.5
grep -q "^5	5	rapport.txt$" $H/.config/plume/bureau-places && [ -f "$D/rapport.txt" ] && ok "icône déplacée sur le bureau (fichier inchangé)" \
	|| { ko "déplacement sur le bureau"; cat $H/.config/plume/bureau-places; }

# 4) bureau -> Fichiers : Copier sur le bureau (Ctrl+C), Coller dans Fichiers (Ctrl+V)
#    (glisser du bureau vers PCManFM : PCManFM, appli GTK 3, ne sait pas recevoir un glisser sous Wayland)
run "vpointer 1280 760 click 690 620"; sleep 1
run "wtype -M ctrl -k c -m ctrl"; sleep 1
run "vpointer 1280 760 click 450 330"; sleep 1
# Coller par le menu Édition de PCManFM (le Ctrl+V simulé par wtype ne lui parvient pas ; un vrai clavier oui)
run "vpointer 1280 760 click 81 57"; sleep 1.5; run "vpointer 1280 760 click 110 158"; sleep 3
[ -f "$H/Documents/rapport.txt" ] && [ -f "$D/rapport.txt" ] && ok "Copier (bureau) puis Coller (Fichiers) : copié dans Documents" \
	|| { ko "bureau -> Fichiers par Copier/Coller"; ls $H/Documents $D; }
run "grim /out/3-vers-fichiers.png"
# 4b) Fichiers -> bureau : Copier dans Fichiers, Coller sur le bureau (nom libre : « rapport 2.txt »)
run "vpointer 1280 760 click 226 143"; sleep 1
run "wtype -M ctrl -k c -m ctrl"; sleep 1
run "vpointer 1280 760 click 400 600"; sleep 1
run "wtype -M ctrl -k v -m ctrl"; sleep 3
[ -f "$D/rapport 2.txt" ] && ok "Copier (Fichiers) puis Coller (bureau) : « rapport 2.txt »" || { ko "Fichiers -> bureau par Coller"; ls $D; }

# 5) aperçu rapide : clic sur la photo, Espace
run "vpointer 1280 760 click 1106 60"; sleep 1
run "wtype ' '"; sleep 1.5
grep -q "photo.png" /tmp/apercu.log 2>/dev/null && ok "Espace : aperçu rapide de la photo" || ko "aperçu rapide"

# 6) clé USB : montée -> apparaît sur le bureau ; double-clic l'ouvre
mkdir -p /run/media/u/CLE_PLUME && mount -t tmpfs -o size=1m tmpfs /run/media/u/CLE_PLUME; sleep 2
grep -q "disque:CLE_PLUME" $H/.config/plume/bureau-places && ok "clé USB affichée sur le bureau" || ko "clé USB"
run "grim /out/4-cle-usb.png"
umount /run/media/u/CLE_PLUME; sleep 2
run "grim /out/5-cle-retiree.png"
grep -q "disque:CLE_PLUME" $H/.config/plume/bureau-places && ko "clé retirée mais toujours affichée" || ok "clé retirée : elle disparaît du bureau"
pgrep -f plume-icones-bureau >/dev/null && ok "toujours en marche" || { ko "arrêté"; cat /tmp/b.log; }
[ $FAIL = 0 ] && echo "=== TOUT OK ===" || echo "=== AU MOINS UN ÉCHEC ==="
