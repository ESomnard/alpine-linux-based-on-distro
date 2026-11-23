#!/bin/sh
set -eu
apk add -q gcc musl-dev make pkgconf wayland-dev wayland-protocols wlr-protocols cairo-dev pango-dev libxkbcommon-dev \
	libvterm-dev librsvg-dev sway grim jq font-dejavu font-jetbrains-mono dbus pcmanfm adwaita-icon-theme \
	papirus-icon-theme glib firefox >/dev/null 2>&1 || true
F="CPPFLAGS=-DPLUME_PASTILLES -DPLUME_VOID"
for d in shell term; do
	rm -rf /b && cp -r /w/$d /b && make -s -C /b clean >/dev/null 2>&1 || true
	make -s -C /b WLR_PROTOCOLS=/w/void/protocoles "$F" 2>&1 | grep -iE "warning|error" || true
	make -s -C /b install PREFIX=/usr/local WLR_PROTOCOLS=/w/void/protocoles "$F" >/dev/null
done
mkdir -p /usr/share/icons/hicolor/scalable/apps && cp /w/void/icones/*.svg /usr/share/icons/hicolor/scalable/apps/
printf '#!/bin/sh\necho "$@" >> /tmp/editeur.log\n' > /usr/local/bin/editeur-test && chmod 755 /usr/local/bin/editeur-test
WLR=$(pkg-config --variable=pkgdatadir wlr-protocols)/unstable/wlr-virtual-pointer-unstable-v1.xml
mkdir -p /vp && cd /vp && wayland-scanner client-header "$WLR" wlr-virtual-pointer-unstable-v1-client-protocol.h \
	&& wayland-scanner private-code "$WLR" vp-protocol.c \
	&& gcc -O2 -I. -o /usr/local/bin/vpointer /w/tests/vpointer.c vp-protocol.c $(pkg-config --cflags --libs wayland-client) && cd /
adduser -D u
H=/home/u; mkdir -p $H/.config/plume $H/Documents
echo r > $H/Documents/rapport.txt; echo c > $H/Documents/compte.txt
# dock de Plume Void ; « plume-terminal » (script) -> plume-term ; Éditeur -> faux éditeur qui note ses fichiers
sed -e 's/| plume-terminal |/| plume-term |/' -e 's/| plume-terminal -a micro micro |/| editeur-test |/' \
	/w/void/rootfs/etc/plume/dock.conf > $H/.config/plume/dock.conf
chown -R u $H
export XDG_RUNTIME_DIR=/tmp/rt && mkdir -pm700 $XDG_RUNTIME_DIR && chown u $XDG_RUNTIME_DIR
printf 'output HEADLESS-1 resolution 1280x760\noutput * bg #1d3b5c solid_color\ndefault_border none\nfor_window [app_id="pcmanfm"] floating enable, resize set 620 460, move position 20 20\n' > /tmp/sway.conf
U="export HOME=$H USER=u XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR LANG=fr_FR.UTF-8; cd"
su u -s /bin/sh -c "$U; WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 dbus-run-session sway -c /tmp/sway.conf > /tmp/sway.log 2>&1 &"
sleep 4
W="$U; export WAYLAND_DISPLAY=wayland-1 SWAYSOCK=\$(ls $XDG_RUNTIME_DIR/sway-ipc.*.sock)"
run() { su u -s /bin/sh -c "$W; $*"; }
arbre() { run "swaymsg -t get_tree -r"; }
ordre() { grep -v '^#' $H/.config/plume/dock.conf | grep ' | ' | cut -d'|' -f2 | sed 's/^ //;s/ $//' | tr '\n' ',' ; }
ok() { echo "OK  $*"; }
ko() { echo "ÉCHEC  $*"; FAIL=1; }
FAIL=0
run "plume-shell > /tmp/shell.log 2>&1 &"; sleep 3
# dock : 10 épinglées | corbeille | ⏻ ; centre de l'appli n°i : x = 310 + 58 i, y = 706
run "vpointer 1280 760 move 716 706 3000 &"; sleep 1.5; run "grim /out/1-loupe.png"; sleep 2

# clic (au relâchement) sur Terminal : il s'ouvre
run "vpointer 1280 760 click 310 706"; sleep 0.4; run "grim /out/2-rebond.png"; sleep 4
[ "$(arbre | jq '[.. | objects | select(.app_id? == "plume-term")] | length')" = 1 ] && ok "clic sur Terminal : ouvert" || ko "clic Terminal"

# glisser Firefox juste avant Paramètres
run "vpointer 1280 760 drag 368 706 752 706"; sleep 1.5
ordre | grep -q "Logithèque,Firefox,Paramètres" && ok "glisser : Firefox déplacé avant Paramètres" || { ko "réordonner"; ordre; echo; }

# tirer PDF hors du dock : retiré (ordre : Terminal, Fichiers, Éditeur, Vidéo et musique, PDF…)
run "vpointer 1280 760 drag 542 706 542 450"; sleep 1.5
ordre | grep -q ",PDF," && { ko "PDF toujours là"; ordre; echo; } || ok "tirer hors du dock : PDF retiré"
run "grim /out/3-apres-glisser.png"

# réduire le terminal : il apparaît à droite (9 épinglées | réduite | corbeille | ⏻), puis revient au clic
run "swaymsg -q '[app_id=\"^plume-term\$\"] move scratchpad'"; sleep 1.5
run "grim /out/4-fenetre-reduite.png"
run "vpointer 1280 760 click 838 706"; sleep 1.5
arbre | jq -e '[.. | objects | select(.app_id? == "plume-term" and .visible == true)] | length == 1' >/dev/null \
	&& ok "fenêtre réduite dans le dock : un clic la fait revenir" || ko "fenêtre réduite"

# corbeille (9 épinglées | corbeille | ⏻ : corbeille en x=872) : clic droit -> menu
run "vpointer 1280 760 menu 872 706 300 300 2500 &"; sleep 2; run "grim /out/5-menu-corbeille.png"; sleep 2.5

# fichiers lâchés depuis Fichiers : sur la corbeille, puis sur l'Éditeur (x = 339 + 58*2 = 455)
run "pcmanfm $H/Documents > /tmp/fm.log 2>&1 &"; sleep 8
run "vpointer 1280 760 drag 318 143 872 706 left 1500"; sleep 3
ls $H/.local/share/Trash/files/rapport.txt >/dev/null 2>&1 && [ ! -e $H/Documents/rapport.txt ] \
	&& ok "fichier lâché sur la corbeille : jeté" || { ko "corbeille (dépôt)"; ls $H/Documents; }
run "grim /out/6-corbeille-pleine.png"
run "vpointer 1280 760 drag 226 143 455 706 left 1500"; sleep 3
grep -q "compte.txt" /tmp/editeur.log 2>/dev/null && [ -f $H/Documents/compte.txt ] \
	&& ok "fichier lâché sur une appli : ouvert avec elle (et laissé à sa place)" || { ko "ouvrir avec"; cat /tmp/editeur.log 2>/dev/null; }
pgrep -x plume-shell >/dev/null && ok "plume-shell toujours en marche" || { ko "plume-shell arrêté"; tail -5 /tmp/shell.log; }
[ $FAIL = 0 ] && echo "=== TOUT OK ===" || echo "=== AU MOINS UN ÉCHEC ==="
