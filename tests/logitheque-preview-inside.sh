#!/bin/sh
set -eu
sed -i 's|^#\(.*community\)|\1|' /etc/apk/repositories
apk add -q gcc musl-dev make pkgconf wayland-dev wayland-protocols wlr-protocols cairo-dev pango-dev libxkbcommon-dev \
	sway grim font-dejavu font-jetbrains-mono dbus wtype sudo libx11 >/dev/null 2>&1
cp -r /lg /buildlg && make -s -C /buildlg >/dev/null && make -s -C /buildlg install PREFIX=/usr/local >/dev/null
# souris virtuelle de test
WLR=$(pkg-config --variable=pkgdatadir wlr-protocols)/unstable/wlr-virtual-pointer-unstable-v1.xml
mkdir -p /vp && cd /vp && wayland-scanner client-header "$WLR" wlr-virtual-pointer-unstable-v1-client-protocol.h \
	&& wayland-scanner private-code "$WLR" vp-protocol.c \
	&& gcc -O2 -I. -o /usr/local/bin/vpointer /tests/vpointer.c vp-protocol.c $(pkg-config --cflags --libs wayland-client) && cd /
# sudo comme sur Plume ; utilisateur admin avec mot de passe
install -m 0440 /rootfs/etc/sudoers.d/plume /etc/sudoers.d/plume
mkdir -p /etc/plume && cp -a /rootfs/etc/plume/packages /etc/plume/
adduser -D esomnard && echo "esomnard:plume123" | chpasswd && addgroup esomnard wheel
export XDG_RUNTIME_DIR=/tmp/rt && mkdir -pm700 $XDG_RUNTIME_DIR && chown esomnard $XDG_RUNTIME_DIR
printf 'output HEADLESS-1 resolution 1280x760\noutput * bg #02060b solid_color\ndefault_border none\ngaps inner 0\n' > /tmp/sway.conf
U="export HOME=/home/esomnard XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR LANG=fr_FR.UTF-8; cd"
su esomnard -s /bin/sh -c "$U; WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 dbus-run-session sway -c /tmp/sway.conf > /tmp/sway.log 2>&1 &"
sleep 4
W="$U; export WAYLAND_DISPLAY=wayland-1 SWAYSOCK=\$(ls $XDG_RUNTIME_DIR/sway-ipc.*.sock)"
run() { su esomnard -s /bin/sh -c "$W; $*"; }
click() { run "vpointer 1280 760 click $1 $2"; sleep 1; }
ok() { echo "OK  $*"; }
ko() { echo "ÉCHEC  $*"; FAIL=1; }
FAIL=0

run "plume-logitheque > /tmp/lg.log 2>&1 &"
sleep 6
run "grim /out/1-explorer.png"

# recherche « micro », Installer, mauvais mot de passe, puis le bon
run "wtype -s 400 micro"; sleep 1.5
run "grim /out/2-recherche.png"
click 1183 328
run "wtype -s 400 mauvais -k Return"; sleep 5
run "grim /out/3-mauvais-mot-de-passe.png"
apk info -e micro >/dev/null && ko "micro installé avec un mauvais mot de passe" || ok "mauvais mot de passe refusé"
run "wtype -s 400 plume123 -k Return"; sleep 10
run "grim /out/4-installe.png"
apk info -e micro >/dev/null && ok "micro installé" || ko "micro pas installé"

# fiche, puis Supprimer (mot de passe gardé en mémoire : pas redemandé)
click 600 328; sleep 0.5
run "grim /out/5-fiche.png"
click 263 283; sleep 6
apk info -e micro >/dev/null && ko "micro pas supprimé" || ok "micro supprimé sans redemander le mot de passe"

# onglet Installées : les paquets vitaux n'ont pas de bouton Supprimer
click 373 32; sleep 1
run "grim /out/6-installees.png"

# Mises à jour
click 516 32; click 149 103; sleep 12
run "grim /out/7-mises-a-jour.png"

# Flathub : activation (installe flatpak, ajoute le dépôt, télécharge son catalogue)
click 250 32; click 1152 165
for i in $(seq 1 60); do
	sleep 5
	su esomnard -c "flatpak remotes --columns=name" 2>/dev/null | grep -q flathub &&
		ls /home/esomnard/.local/share/flatpak/appstream/flathub/*/active >/dev/null 2>&1 && break
done
sleep 5
su esomnard -c "flatpak remotes --columns=name" 2>/dev/null | grep -q flathub && ok "Flathub activé" || ko "Flathub non activé"
run "wtype -s 400 steam"; sleep 8
run "grim /out/8-recherche-flathub.png"

echo "== journal"; cat /tmp/lg.log
[ "$FAIL" = 0 ] && echo "SUCCÈS" || { echo "ÉCHEC"; exit 1; }
