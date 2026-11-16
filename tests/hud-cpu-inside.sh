#!/bin/sh
# plume-hud : consommation CPU (écran allumé / éteint) et clavier débranché puis rebranché.
set -eu
sed -i 's|^#\(.*community\)|\1|' /etc/apk/repositories
apk add -q gcc musl-dev make pkgconf wayland-dev wayland-protocols cairo-dev pango-dev libxkbcommon-dev \
	linux-pam-dev sway wtype font-dejavu shadow dbus >/dev/null 2>&1
cp -r /src /build && make -s -C /build >/dev/null
install -Dm755 /build/plume-hud /usr/local/bin/plume-hud
install -Dm644 /build/pam.d/plume-hud /etc/pam.d/plume-hud
adduser -D esomnard >/dev/null 2>&1 || true
echo 'esomnard:plume123' | chpasswd >/dev/null
export XDG_RUNTIME_DIR=/tmp/rt && mkdir -pm700 $XDG_RUNTIME_DIR && chown esomnard $XDG_RUNTIME_DIR
echo 'output HEADLESS-1 resolution 1920x1080' > /tmp/sway.conf
U="export XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR WAYLAND_DISPLAY=wayland-1"
run() { su esomnard -s /bin/sh -c "$U; export SWAYSOCK=\$(ls $XDG_RUNTIME_DIR/sway-ipc.*.sock 2>/dev/null); $*"; }
su esomnard -s /bin/sh -c "export XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1; sway -c /tmp/sway.conf >/tmp/sway.log 2>&1 &"
sleep 4; tail -3 /tmp/sway.log; ls $XDG_RUNTIME_DIR
HZ=$(getconf CLK_TCK)
ticks() { awk '{print $14 + $15}' /proc/$1/stat; }
mesure() { # mesure PID SECONDES -> % d'un cœur
	a=$(ticks $1); sleep $2; b=$(ticks $1)
	echo $(( (b - a) * 100 / (HZ * $2) ))
}
run "plume-hud --lock > /tmp/lock.log 2>&1 &"
sleep 6
PID=$(pgrep -f 'plume-hud --lock')
echo "CPU écran allumé (repos, 20 s) : $(mesure $PID 20) %"
run "swaymsg 'output * power off'" >/dev/null 2>&1 || true
sleep 2
echo "CPU écran éteint (20 s) : $(mesure $PID 20) %"
run "swaymsg 'output * power on'" >/dev/null 2>&1 || true
sleep 2
# clavier n°1 : mauvais mot de passe, puis il disparaît (fin de wtype)
run "wtype -s 500 faux -k Return -s 500"
sleep 4; tail -3 /tmp/sway.log; ls $XDG_RUNTIME_DIR
# clavier n°2 (nouveau) : le bon mot de passe
run "wtype -s 500 plume123 -k Return -s 500"
sleep 3
if pgrep -f 'plume-hud --lock' >/dev/null; then echo "CLAVIER : ÉCHEC (2e clavier ignoré, toujours verrouillé)"; else echo "CLAVIER : OK (2e clavier pris en compte, déverrouillé)"; fi
