# Plume — ~/.profile
export EDITOR=micro
export PATH="$HOME/.local/bin:$PATH"

# Pas de systemd/elogind : on crée nous-mêmes le dossier runtime (requis par Wayland/pipewire)
if [ -z "${XDG_RUNTIME_DIR:-}" ]; then
	export XDG_RUNTIME_DIR="/tmp/$(id -u)-runtime-dir"
	mkdir -pm 0700 "$XDG_RUNTIME_DIR"
fi

# Méthode de saisie (pinyin) pour les applis GTK/Qt/X11
export GTK_IM_MODULE=ibus QT_IM_MODULE=ibus XMODIFIERS=@im=ibus

# Démarre sway automatiquement en se connectant sur tty1
if [ "$(tty)" = /dev/tty1 ] && command -v sway >/dev/null; then
	exec dbus-run-session sway
fi
