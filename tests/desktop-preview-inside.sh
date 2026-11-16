#!/bin/sh
set -eu
sed -i 's|^#\(.*community\)|\1|' /etc/apk/repositories
apk add -q gcc musl-dev make pkgconf wayland-dev wayland-protocols wlr-protocols cairo-dev pango-dev \
	libxkbcommon-dev linux-pam-dev sway swaybg grim foot fuzzel mako font-dejavu font-jetbrains-mono \
	font-wqy-zenhei fastfetch dbus wtype libnotify pipewire wireplumber >/dev/null 2>&1
cp -r /src /build && make -s -C /build >/dev/null && make -s -C /build install PREFIX=/usr/local >/dev/null
cp -r /shell /buildshell && make -s -C /buildshell >/dev/null && make -s -C /buildshell install PREFIX=/usr/local >/dev/null
install -m755 /rootfs/etc/plume/conf/plume-bar /usr/local/bin/plume-bar
mkdir -p /usr/share/plume && plume-wall /usr/share/plume/wallpaper.png 1600 900

adduser -D esomnard >/dev/null 2>&1 || true
cp -a /rootfs/etc/skel/. /home/esomnard/
H=/home/esomnard
mkdir -p $H/Bureau $H/Documents/projets/plume $H/Documents/cours $H/Téléchargements $H/Images $H/Musique $H/Vidéos
echo "notes du jour" > $H/Bureau/todo.txt
head -c 2400000 /dev/urandom > $H/Documents/rapport-final.pdf
echo "# Plume" > $H/Documents/projets/plume/README.md
head -c 48000 /dev/urandom > $H/Documents/cv.pdf
printf 'a,b\n1,2\n' > $H/Documents/budget.csv
head -c 3800000 /dev/urandom > $H/Images/vacances.jpg
head -c 9000000 /dev/urandom > $H/Musique/morceau.mp3
chown -R esomnard /home/esomnard
sed -i 's/^output \* bg \/usr/output HEADLESS-1 resolution 1600x900\noutput * bg \/usr/' /home/esomnard/.config/sway/config
export XDG_RUNTIME_DIR=/tmp/rt && mkdir -pm700 $XDG_RUNTIME_DIR && chown esomnard $XDG_RUNTIME_DIR
U="export PLUME_SHELL_HOVER=${PLUME_SHELL_HOVER:-1} HOME=/home/esomnard XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR ENV=/home/esomnard/.ashrc; cd"
su esomnard -s /bin/sh -c "$U; WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 dbus-run-session sway > /tmp/sway.log 2>&1 &"
sleep 5
W="$U; export WAYLAND_DISPLAY=wayland-1 SWAYSOCK=\$(ls $XDG_RUNTIME_DIR/sway-ipc.*.sock)"
run() { su esomnard -s /bin/sh -c "$W; $*"; }

run "grim /out/1-fond.png"
run "swaymsg exec 'foot sh -i'" >/dev/null; sleep 3
run "swaymsg exec 'foot sh -c \"cd; ENV=~/.ashrc sh -i\"'" >/dev/null; sleep 3
run "grim /out/2-terminaux.png"
run "swaymsg exec fuzzel" >/dev/null; sleep 2
run "grim /out/3-lanceur.png"
run "pkill fuzzel" || true
run "notify-send 'Plume' 'Mise à jour terminée — 0 paquet à installer'" 2>/dev/null || run "swaymsg exec \"mako\"" >/dev/null
sleep 2
grep -i -E 'error' /tmp/sway.log | grep -v -i xwayland | head -5 || true

# le poste de pilotage (plume-term --hud) a son propre test : tests/term-preview.sh
