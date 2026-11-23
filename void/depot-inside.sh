#!/bin/sh
# Dans le conteneur Void : compile chaque composant Plume, en fait un paquet .xbps,
# signe le tout et crée l'index -> /work/out/depot-void/x86_64/
# Variables : VERSION (paquets/VERSION), REL (révision)
set -eu
W=/work
OUT=$W/out/depot-void/x86_64
ARCH=x86_64
: "${VERSION:=$(cat $W/paquets/VERSION)}"
: "${REL:=1}"

echo "==> Outils de compilation"
xbps-install -Syu xbps >/dev/null 2>&1 || true
xbps-install -Sy gcc make pkg-config wayland-devel wayland-protocols cairo-devel pango-devel \
	libxkbcommon-devel gtk+3-devel pam-devel libvterm-devel librsvg-devel gdk-pixbuf-devel dejavu-fonts-ttf \
	openssl curl unzip >/dev/null

# clé de signature Void (créée une seule fois, gardée dans cles/, jamais publiée)
KEY=$W/cles/plume-void.pem
if [ ! -f "$KEY" ]; then
	echo "==> Nouvelle clé de signature Plume (Void)"
	openssl genrsa -out "$KEY" 4096 2>/dev/null
	chmod 600 "$KEY"
fi

rm -rf "$OUT" && mkdir -p "$OUT"
D=/tmp/plume-dest
WLR=$W/void/protocoles
RT="wayland>=0 cairo>=0 pango>=0 libxkbcommon>=0"

# paquet NOM DOSSIER "description" "dépendances" [arch]
paquet() {
	nom=$1 dir=$2 desc=$3 dep=$4 arch=${5:-$ARCH}
	rm -rf "$D" /tmp/b && cp -r "$W/$dir" /tmp/b
	make -s -C /tmp/b clean >/dev/null 2>&1 || true
	# PLUME_PASTILLES : pastilles fermer / réduire / plein écran dans les fenêtres (fenetre.h) ;
	# PLUME_VOID : corrections propres à Plume Void (ex. flèche de la souris dans le lanceur)
	make -s -C /tmp/b WLR_PROTOCOLS="$WLR" CPPFLAGS="-DPLUME_PASTILLES -DPLUME_VOID" >/dev/null || { echo "ÉCHEC compilation : $nom" >&2; exit 1; }
	make -s -C /tmp/b install DESTDIR="$D" PREFIX=/usr WLR_PROTOCOLS="$WLR" CPPFLAGS="-DPLUME_PASTILLES -DPLUME_VOID" >/dev/null
	[ -z "${EXTRA:-}" ] || eval "$EXTRA"
	EXTRA=""
	finir "$nom" "$desc" "$dep" "$arch"
}
finir() {
	nom=$1 desc=$2 dep=$3 arch=$4
	# logos Plume (void/icones, paquet plume-icones) à la place des icônes génériques
	for f in "$D"/usr/share/applications/plume-*.desktop; do
		[ -f "$f" ] || continue
		n=$(basename "$f" .desktop)
		[ -f "$W/void/icones/$n.svg" ] && sed -i "s/^Icon=.*/Icon=$n/" "$f"
	done
	(cd "$OUT" && xbps-create -q -A "$arch" -n "$nom-${VERSION}_$REL" -s "$desc" \
		-l GPL-3.0-or-later -H "https://github.com/ESomnard/alpine-linux-based-on-distro" \
		-m "Plume <plume@localhost>" ${dep:+-D "$dep"} "$D") || { echo "ÉCHEC paquet : $nom" >&2; exit 1; }
	echo "    $nom-${VERSION}_$REL"
}

echo "==> Paquets Plume $VERSION (révision $REL)"
# plume-hud : + fond d'écran par défaut, PAM à la façon de Void
rm -rf "$D" /tmp/b && cp -r "$W/hud" /tmp/b
# texte du fond d'écran : Void au lieu d'Alpine (sur la copie : les sources de hud/ ne changent pas)
sed -i 's/ALPINE LINUX  ·  HUD/VOID LINUX  ·  HUD/' /tmp/b/wall.c
grep -q 'VOID LINUX' /tmp/b/wall.c || { echo "ÉCHEC : texte du fond introuvable dans hud/wall.c" >&2; exit 1; }
make -s -C /tmp/b clean >/dev/null 2>&1 || true
make -s -C /tmp/b >/dev/null && make -s -C /tmp/b install DESTDIR="$D" PREFIX=/usr >/dev/null
printf '# PAM de l écran de verrouillage plume-hud (comme swaylock)\nauth include system-auth\n' > "$D/etc/pam.d/plume-hud"
mkdir -p "$D/usr/share/plume" && "$D/usr/bin/plume-wall" "$D/usr/share/plume/wallpaper.png" 2560 1440
finir plume-hud "Écran de connexion et de verrouillage HUD" "$RT pam>=0 greetd>=0 cage>=0" $ARCH

paquet plume-shell shell "Barre et dock HUD (sway)" "$RT librsvg>=0"
paquet plume-alimentation void/alimentation "Écran Éteindre, Redémarrer, Veille, Déconnexion, Verrouiller" "$RT"
paquet plume-icones-bureau void/bureau "Fichiers du Bureau sur le bureau (ouvrir, renommer, corbeille…)" "$RT librsvg>=0 gdk-pixbuf>=0 xdg-utils>=0 wl-clipboard>=0"
paquet plume-term term "Terminal HUD et poste de pilotage" "$RT libvterm>=0"
paquet plume-lanceur lanceur "Toutes les applications, en grille" "$RT librsvg>=0"
paquet plume-fonds fonds "Apparence : fond d'écran et couleur du thème" "$RT gdk-pixbuf>=0 dejavu-fonts-ttf>=0"
paquet plume-wifi wifi "Panneau wifi HUD (iwd)" "$RT iwd>=0"
paquet plume-son son "Panneau son HUD (PipeWire)" "$RT wireplumber>=0"
paquet plume-bluetooth bluetooth "Panneau Bluetooth HUD (bluez)" "$RT bluez>=0"
# catalogue de la Logithèque : noms des paquets Void
EXTRA='install -Dm644 "$W/void/catalogue.txt" "$D/usr/share/plume/logitheque.txt"'
paquet plume-logitheque logitheque "Logithèque : installer des applications" "$RT sudo>=0"
# pastilles rouge / jaune / verte dans le gestionnaire de fichiers (module GTK 3)
paquet plume-pastilles-gtk void/pastilles-gtk "Pastilles de fenêtre Plume dans PCManFM (module GTK 3)" "gtk+3>=0"

# scripts Plume : communs, remplacés par leur version Void quand elle existe
rm -rf "$D" && mkdir -p "$D/usr/bin"
for f in plume-maj plume-parefeu plume-theme plume-emulation plume-jeux plume-manette plume-ecrans plume-pilotes plume-vpn plume-batterie plume-nuit plume-portail plume-retour plume-sauvegarde plume-accessibilite plume-raccourcis plume-notifications plume-linux plume-menu plume-bar plume-terminal plume-greeter plume-secureboot; do
	src=$W/rootfs/etc/plume/conf/$f
	[ -f "$W/void/rootfs/etc/plume/conf/$f" ] && src=$W/void/rootfs/etc/plume/conf/$f
	install -m755 "$src" "$D/usr/bin/$f"
done
for d in plume-ecrans plume-vpn plume-batterie plume-nuit plume-sauvegarde plume-accessibilite plume-retour plume-raccourcis plume-term plume-pilotage plume-notifications; do
	install -Dm644 "$W/void/$d.desktop" "$D/usr/share/applications/$d.desktop"
done
finir plume-outils "Scripts Plume : mises à jour, pare-feu, thème, écrans, jeux, manettes, émulation, lanceurs" "nftables>=0 sudo>=0 jq>=0" noarch

# police JetBrains Mono (terminal, barre, HUD) : absente des dépôts Void
rm -rf "$D" && mkdir -p "$D/usr/share/fonts/TTF" /tmp/jb
JB=2.304
[ -f /tmp/jb.zip ] || curl -fsSL -o /tmp/jb.zip "https://github.com/JetBrains/JetBrainsMono/releases/download/v$JB/JetBrainsMono-$JB.zip"
unzip -qo /tmp/jb.zip 'fonts/ttf/*' -d /tmp/jb
cp /tmp/jb/fonts/ttf/*.ttf "$D/usr/share/fonts/TTF/"
(cd "$OUT" && xbps-create -q -A noarch -n "font-jetbrains-mono-${JB}_1" -s "Police JetBrains Mono (OFL-1.1)" \
	-l OFL-1.1 -H https://www.jetbrains.com/lp/mono/ -m "Plume <plume@localhost>" "$D")
echo "    font-jetbrains-mono-${JB}_1"

# logos des applis Plume (void/icones) : grille des applis, dock, menus
rm -rf "$D" && mkdir -p "$D/usr/share/icons/hicolor/scalable/apps"
install -m644 "$W"/void/icones/plume-*.svg "$D/usr/share/icons/hicolor/scalable/apps/"
finir plume-icones "Logos des applications Plume" "" noarch

# réglages de Plume dans /etc (dock, sway, NVIDIA, AppArmor…) : livrés en paquet pour suivre les
# mises à jour (plume-maj), au lieu d'être seulement copiés depuis l'ISO à l'installation
rm -rf "$D" && mkdir -p "$D"
R=$W/void/rootfs
cd "$R"
for f in etc/plume/dock.conf etc/sway/config.d/*.conf etc/modprobe.d/plume-*.conf etc/dracut.conf.d/plume-*.conf \
	etc/profile.d/plume-void.sh etc/xbps.d/*.conf etc/apparmor.d/plume-* etc/apparmor.d/abstractions/plume-* \
	etc/xdg/xdg-desktop-portal-wlr/config etc/xdg/xdg-desktop-portal/sway-portals.conf usr/lib/plume/*; do
	[ -f "$R/$f" ] || continue
	install -Dm644 "$R/$f" "$D/$f"
done
cd /
chmod 755 "$D"/usr/lib/plume/*
install -m755 "$W/void/reglages/INSTALL" "$D/INSTALL"
finir plume-reglages "Réglages de Plume (dock, sway, portables NVIDIA, AppArmor), mis à jour avec le système" "" noarch

rm -rf "$D" && mkdir -p "$D"
finir plume-bureau "Bureau Plume complet (composants HUD)" \
	"plume-hud>=0 plume-reglages>=0 plume-alimentation>=0 plume-icones>=0 plume-icones-bureau>=0 plume-shell>=0 plume-term>=0 plume-lanceur>=0 plume-fonds>=0 plume-wifi>=0 plume-son>=0 plume-bluetooth>=0 plume-logitheque>=0 plume-pastilles-gtk>=0 plume-outils>=0 font-jetbrains-mono>=0" noarch

echo "==> Index et signature"
cd "$OUT"
xbps-rindex -a ./*.xbps >/dev/null
xbps-rindex --privkey "$KEY" --sign --signedby "Plume Linux" . >/dev/null 2>&1
xbps-rindex --privkey "$KEY" --sign-pkg ./*.xbps >/dev/null 2>&1
ls "$OUT"
