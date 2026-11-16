#!/bin/sh
set -eu
sed -i 's|^#\(.*community\)|\1|' /etc/apk/repositories
apk add -q gcc musl-dev make pkgconf wayland-dev wayland-protocols cairo-dev pango-dev libxkbcommon-dev \
	libvterm-dev libvterm sway grim font-dejavu font-jetbrains-mono font-wqy-zenhei fastfetch dbus wtype \
	wl-clipboard htop ncurses procps libx11 wlr-protocols >/dev/null 2>&1
cp -r /term /buildterm && make -s -C /buildterm >/dev/null && make -s -C /buildterm install PREFIX=/usr/local >/dev/null
ls -l /usr/local/bin/plume-term
# souris virtuelle de test
WLR=$(pkg-config --variable=pkgdatadir wlr-protocols)/unstable/wlr-virtual-pointer-unstable-v1.xml
mkdir -p /vp && cd /vp && wayland-scanner client-header "$WLR" wlr-virtual-pointer-unstable-v1-client-protocol.h \
	&& wayland-scanner private-code "$WLR" vp-protocol.c \
	&& gcc -O2 -I. -o /usr/local/bin/vpointer /tests/vpointer.c vp-protocol.c $(pkg-config --cflags --libs wayland-client) && cd /

adduser -D esomnard >/dev/null 2>&1 || true
cp -a /rootfs/etc/skel/. /home/esomnard/
H=/home/esomnard
mkdir -p $H/Documents/cours $H/Images $H/Musique $H/Bureau
echo "notes" > $H/Bureau/todo.txt; head -c 300000 /dev/urandom > $H/Images/photo.jpg; echo "# Plume" > $H/Documents/README.md
chown -R esomnard $H
# sway minimal : un seul écran, pas de barre
cat > /tmp/sway.conf <<'CONF'
output HEADLESS-1 resolution 1280x760
output * bg #02060b solid_color
default_border none
gaps inner 0
CONF
export XDG_RUNTIME_DIR=/tmp/rt && mkdir -pm700 $XDG_RUNTIME_DIR && chown esomnard $XDG_RUNTIME_DIR
U="export HOME=$H XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR ENV=$H/.ashrc SHELL=/bin/ash LANG=fr_FR.UTF-8; cd"
su esomnard -s /bin/sh -c "$U; WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 dbus-run-session sway -c /tmp/sway.conf > /tmp/sway.log 2>&1 &"
sleep 4
W="$U; export WAYLAND_DISPLAY=wayland-1 SWAYSOCK=\$(ls $XDG_RUNTIME_DIR/sway-ipc.*.sock)"
run() { su esomnard -s /bin/sh -c "$W; $*"; }
type_line() {
	printf '%s' "$1" > /tmp/line && chmod 644 /tmp/line
	run "wtype -s 400 -d 6 - -k Return < /tmp/line"
	sleep 0.4
}

run "plume-term > /tmp/term.log 2>&1 &"
sleep 4
run "grim /out/1-demarrage.png"

type_line 'clear; ls -la --color=always /etc | head -12'
type_line 'printf "\033[1mgras\033[0m \033[3mitalique\033[0m \033[4msouligné\033[0m \033[9mbarré\033[0m \033[7minversé\033[0m\n"'
type_line 'for i in 0 1 2 3 4 5 6 7; do printf "\033[4${i}m  \033[0m"; done; for i in 0 1 2 3 4 5 6 7; do printf "\033[10${i}m  \033[0m"; done; echo'
type_line 'i=0; while [ $i -lt 72 ]; do printf "\033[48;2;$((i*3));$((80+i*2));$((200-i*2))m \033[0m"; i=$((i+1)); done; echo'
type_line 'echo "Accents : é è à ù ç ê ô — Chinois : 你好世界 — Boîtes : ┌──┬──┐ │▓▒░│ └──┴──┘ — ➜ ◆ ✓"'
sleep 1
run "grim /out/2-couleurs-unicode.png"

type_line 'seq 1 400'
sleep 1
run "wtype -s 400 -M shift -k Prior -m shift"
sleep 0.5
run "grim /out/3-historique.png"
run "wtype -s 400 -M shift -k End -m shift"

type_line 'htop'
sleep 3
run "grim /out/4-htop.png"
run "wtype -s 400 q"
sleep 1

# touches mortes (AZERTY) : ^ puis e = ê, ¨ puis i = ï
run "wtype -s 400 'echo touches mortes : ' -k dead_circumflex e -k dead_circumflex o -k dead_diaeresis i -k Return"
sleep 1
run "grim /out/4b-touches-mortes.png"

# nouvel onglet, dossier Documents
run "wtype -s 400 -M ctrl -M shift t -m shift -m ctrl"
sleep 2
type_line 'cd ~/Documents && echo "deuxième onglet" && pwd'
sleep 1.5
run "grim /out/5-onglets.png"

# coller depuis le presse-papiers
run "printf 'echo collé depuis le presse-papiers ✓' | wl-copy"
sleep 0.5
run "wtype -s 400 -M ctrl -M shift v -m shift -m ctrl"
sleep 1
run "wtype -s 400 -k Return"
sleep 1
# grand texte
run "wtype -s 400 -M ctrl -k plus -m ctrl"
run "wtype -s 400 -M ctrl -k plus -m ctrl"
sleep 1
run "grim /out/6-coller-zoom.png"

echo "== mémoire (Ko, RSS) : plume-term avec 2 onglets"
ps -C plume-term -o rss=,args=
apk add -q foot >/dev/null 2>&1
run "foot > /dev/null 2>&1 &"
sleep 3
echo "== pour comparer : foot"
ps -C foot -o rss=,args=
run "pkill foot" || true
sleep 1

# mode HUD (plein écran, panneaux) : Ctrl+Maj+H
run "wtype -s 400 -M ctrl -M shift h -m shift -m ctrl"
sleep 3
type_line 'cd ~ && ls'
run "wtype -s 400 -P a -s 300 -p a -k BackSpace"
sleep 0.2
run "grim /out/8-hud.png"
# clics (souris virtuelle) : coordonnées pour un écran 1280×760
click_at() { run "vpointer 1280 760 click $1 $2 ${3:-left}"; sleep 0.6; }
# 1. raccourci « Documents » : l'explorateur y va, le terminal ne bouge pas
click_at 320 634
run "grim /out/9a-raccourci.png"
# 2. recherche : clic dans le champ, « read », Entrée = ouvre README.md dans un nouvel onglet
click_at 400 573
run "wtype -s 400 read"
sleep 0.5
run "grim /out/9b-recherche.png"
run "wtype -s 400 -k Return"
sleep 1.5
run "grim /out/9c-ouvert.png"
run "wtype -s 400 -k Escape -s 300 ':q!' -k Return"
sleep 1
# 3. « cd ici » : le terminal va dans Documents ; puis l, s, Entrée au clavier à l'écran
click_at 640 573
click_at 900 652
click_at 765 652
click_at 980 632
sleep 1
run "grim /out/9d-cd-ici.png"
# 4. fil d'Ariane « ~ » puis fichiers cachés
click_at 365 549
click_at 578 573
sleep 0.5
run "grim /out/9e-caches.png"
# sélection à la souris (glisser sur le début de la 1re ligne), copie avec Ctrl+Maj+C
run "vpointer 1280 760 drag 287 103 420 103"
run "wtype -s 400 -M ctrl -M shift c -m shift -m ctrl"
sleep 0.5
echo "presse-papiers après sélection : [$(run 'wl-paste -n' 2>&1)]"
echo "== mémoire en mode HUD (Ko)"
ps -C plume-term -o rss=,args=
# CPU au repos sur 10 s (tics d'horloge utilisés / tics écoulés)
PID=$(pgrep -x plume-term)
sleep 2; T0=$(awk '{print $14+$15}' /proc/$PID/stat); sleep 10; T1=$(awk '{print $14+$15}' /proc/$PID/stat)
echo "CPU au repos en mode HUD : $(( (T1-T0) * 100 / (10*$(getconf CLK_TCK)) )) % d'un cœur"
run "wtype -s 400 -M ctrl -M shift h -m shift -m ctrl"
sleep 1

# fin du shell : l'onglet se ferme, puis la fenêtre
type_line 'exit'
sleep 2
echo "onglets fermés : 1 -> fenêtres plume-term : $(run "swaymsg -t get_tree" | grep -c '"app_id": "plume-term"')"
run "grim /out/7-apres-exit.png"
type_line 'exit'
sleep 2
echo "onglets fermés : 2 -> fenêtres plume-term : $(run "swaymsg -t get_tree" | grep -c '"app_id": "plume-term"')"
ps -C plume-term -o pid=,args= || echo "plume-term terminé proprement"
echo "== journal plume-term"; cat /tmp/term.log
grep -i error /tmp/sway.log | grep -v -i xwayland | head -5 || true
