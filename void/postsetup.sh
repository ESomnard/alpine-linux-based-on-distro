#!/bin/sh
# Lancé par void-mklive sur le système de l'ISO ($1 = sa racine), avant l'initramfs.
# Règle ce que desktop.sh règle sur Plume Alpine.
set -eu
R=$1
P=$R/etc/plume

# Papirus : seulement les icônes d'applis en 64 px (8 000 fichiers au lieu de 103 000 ; même règle
# pour les mises à jour : /etc/xbps.d/30-plume-papirus.conf)
for t in "$R"/usr/share/icons/*Papirus*; do
	[ -d "$t" ] || continue
	case "${t##*/}" in
	Papirus) find "$t" -mindepth 1 -maxdepth 1 ! -name 64x64 ! -name index.theme -exec rm -rf {} + ;
		find "$t/64x64" -mindepth 1 -maxdepth 1 ! -name apps -exec rm -rf {} + 2>/dev/null || true ;;
	*) rm -rf "$t" ;;
	esac
done

# scripts : exécutables, sudoers lisible par sudo seulement
chmod 0755 "$P"/*.sh "$P"/bin/apt "$P"/conf/plume-* 2>/dev/null || true
chmod 0440 "$R/etc/sudoers.d/plume"
chmod 0600 "$R/etc/doas.d/plume.conf" 2>/dev/null || true
chmod 0755 "$R"/etc/skel/.config/sway/*.sh
# apt, apt-get, apt-cache partout
for c in apt apt-get apt-cache; do ln -sf /etc/plume/bin/apt "$R/usr/local/bin/$c"; done

# bash (shell par défaut de Void) lit .profile (XDG_RUNTIME_DIR, sway sur tty1) et .ashrc
cat > "$R/etc/skel/.bash_profile" <<'B'
# Plume : réglages de session communs (~/.profile), puis ceux du shell (~/.bashrc)
[ -f ~/.profile ] && . ~/.profile
[ -f ~/.bashrc ] && . ~/.bashrc
B
grep -q ashrc "$R/etc/skel/.bashrc" 2>/dev/null || echo '[ -f ~/.ashrc ] && . ~/.ashrc   # Plume : invite, alias, fiche système' >> "$R/etc/skel/.bashrc"
# dossiers personnels habituels
for d in Bureau Documents Téléchargements Images Musique Vidéos; do mkdir -p "$R/etc/skel/$d"; done
# PipeWire : pas de pipewire-launcher sur Void, pipewire lance wireplumber et pipewire-pulse
sed -i 's|^exec /usr/libexec/pipewire-launcher|exec pipewire|' "$R/etc/skel/.config/sway/config"
# veille : swayidle lancé par plume-veille (délai réglable dans Paramètres), plus le bloc figé du modèle
sed -i '/^exec swayidle -w/,/before-sleep/d' "$R/etc/skel/.config/sway/config"
mkdir -p "$R/etc/pipewire/pipewire.conf.d"
ln -sf /usr/share/examples/wireplumber/10-wireplumber.conf "$R/etc/pipewire/pipewire.conf.d/"
ln -sf /usr/share/examples/pipewire/20-pipewire-pulse.conf "$R/etc/pipewire/pipewire.conf.d/"
mkdir -p "$R/etc/alsa/conf.d"
ln -sf /usr/share/alsa/alsa.conf.d/50-pipewire.conf "$R/etc/alsa/conf.d/" 2>/dev/null || true
ln -sf /usr/share/alsa/alsa.conf.d/99-pipewire-default.conf "$R/etc/alsa/conf.d/" 2>/dev/null || true

# écran de connexion HUD
install -Dm644 "$P/conf/greetd.toml" "$R/etc/greetd/config.toml"
# l'utilisateur de greetd doit pouvoir ouvrir l'écran (seatd) et lire les claviers
chroot "$R" usermod -aG _seatd,video,input _greeter

# wifi : iwd gère tout (DHCP compris) ; dhcpcd ne s'occupe que du câble
install -Dm644 "$P/conf/iwd-main.conf" "$R/etc/iwd/main.conf"
grep -q '^denyinterfaces' "$R/etc/dhcpcd.conf" || echo 'denyinterfaces wlan* wl*' >> "$R/etc/dhcpcd.conf"

# zram : moitié de la RAM, zstd
cat > "$R/etc/sv/zramen/conf" <<'Z'
export ZRAM_COMP_ALGORITHM=zstd
export ZRAM_SIZE=50
Z

# sécurité : pare-feu + durcissement du noyau + AppArmor
install -Dm644 "$P/conf/nftables.nft" "$R/etc/nftables.nft"
mkdir -p "$R/etc/nftables.d"
echo 'include "/etc/nftables.nft"' > "$R/etc/nftables.conf"
install -Dm644 "$P/conf/sysctl-plume.conf" "$R/etc/sysctl.d/60-plume.conf"
if [ -f "$R/etc/default/apparmor" ]; then
	sed -i 's/^#*APPARMOR=.*/APPARMOR=enforce/' "$R/etc/default/apparmor"
fi
mkdir -p "$R/etc/apparmor.d/disable"
[ -f "$R/etc/apparmor.d/unix-chkpwd" ] && ln -sf /etc/apparmor.d/unix-chkpwd "$R/etc/apparmor.d/disable/unix-chkpwd"
# profil « firefox » de Void : non confiné et sur le même programme que plume-firefox (le vrai)
ln -sf /etc/apparmor.d/firefox "$R/etc/apparmor.d/disable/firefox"
# lecteur d'écran : voix française par défaut (espeak-ng)
[ -f "$R/etc/speech-dispatcher/speechd.conf" ] && sed -i \
	-e 's/^#* *DefaultLanguage .*/DefaultLanguage "fr"/' \
	-e 's/^#* *DefaultModule .*/DefaultModule espeak-ng/' "$R/etc/speech-dispatcher/speechd.conf"
# lanceur : un seul « Foot » (voir /etc/xbps.d/20-plume-lanceur.conf)
rm -f "$R/usr/share/applications/footclient.desktop" "$R/usr/share/applications/foot-server.desktop"
# Alpine seulement : desktop.sh (le bureau est déjà dans l'ISO) ; slim-firmware.sh a sa version Void
rm -f "$P/desktop.sh"
echo "Plume Void : réglages appliqués"
