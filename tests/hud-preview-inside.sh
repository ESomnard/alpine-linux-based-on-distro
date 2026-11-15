#!/bin/sh
set -eu
sed -i 's|^#\(.*community\)|\1|' /etc/apk/repositories
apk add -q gcc musl-dev make pkgconf wayland-dev wayland-protocols cairo-dev pango-dev libxkbcommon-dev \
	linux-pam-dev sway grim wtype font-dejavu python3 shadow dbus >/dev/null 2>&1
cp -r /src /build && make -s -C /build >/dev/null
install -Dm755 /build/plume-hud /usr/local/bin/plume-hud
install -Dm644 /build/pam.d/plume-hud /etc/pam.d/plume-hud

adduser -D esomnard >/dev/null 2>&1 || true
echo 'esomnard:plume123' | chpasswd
export XDG_RUNTIME_DIR=/tmp/rt && mkdir -pm700 $XDG_RUNTIME_DIR && chown esomnard $XDG_RUNTIME_DIR
cat > /tmp/sway.conf <<'CONF'
output HEADLESS-1 resolution 1600x900
CONF
chown esomnard /tmp/sway.conf
run() { su esomnard -s /bin/sh -c "export XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR WAYLAND_DISPLAY=wayland-1; $*"; }
su esomnard -s /bin/sh -c "export XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1; sway -c /tmp/sway.conf >/tmp/sway.log 2>&1 &"
sleep 4
ls $XDG_RUNTIME_DIR; tail -5 /tmp/sway.log

echo "== mode test (fenêtre)"
run "plume-hud --dev > /tmp/dev.log 2>&1 &"
sleep 2.5
run "grim /out/1-dev.png"
run "pkill -f 'plume-hud --dev'" || true

echo "== mode verrou"
run "plume-hud --lock > /tmp/lock.log 2>&1 &"
sleep 2.5
run "grim /out/2-lock.png"
# un seul clavier virtuel : mauvais mot de passe, pause 4 s, bon mot de passe
run "wtype faux -k Return -s 4000 plume123 -k Return &"
sleep 3
run "grim /out/3-lock-erreur.png"
sleep 3.5
if pgrep -f 'plume-hud --lock' >/dev/null; then echo "ECHEC : toujours verrouillé"; else echo "OK : déverrouillé"; fi
cat /tmp/lock.log

echo "== mode greeter (faux greetd)"
python3 /tests/fake-greetd.py /tmp/greetd.sock &
sleep 1
run "GREETD_SOCK=/tmp/greetd.sock plume-hud --greeter > /tmp/greeter.log 2>&1 &"
sleep 2.5
run "grim /out/4-greeter.png"
run "wtype mauvais -k Return -s 4000 plume123 -k Return &"
sleep 3
run "grim /out/5-greeter-erreur.png"
sleep 3.5
if pgrep -f 'plume-hud --greeter' >/dev/null; then echo "ECHEC : greeter toujours ouvert"; else echo "OK : greeter fermé après connexion"; fi
cat /tmp/greetd.log /tmp/greeter.log
