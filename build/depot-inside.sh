#!/bin/sh
# Dans le conteneur : APKBUILD de chaque composant, construction, signature, index.
set -eu
sed -i 's|^#\(.*community\)|\1|' /etc/apk/repositories
apk add -q alpine-sdk wayland-dev wayland-protocols wlr-protocols cairo-dev pango-dev libxkbcommon-dev \
	linux-pam-dev libvterm-dev librsvg-dev gdk-pixbuf-dev font-dejavu >/dev/null

# clé de signature (créée une seule fois, gardée dans cles/)
mkdir -p /root/.abuild
if ! ls /cles/*.rsa >/dev/null 2>&1; then
	echo "==> Nouvelle clé de signature Plume"
	PACKAGER="Plume <plume@localhost>" abuild-keygen -n >/dev/null 2>&1
	cp /root/.abuild/*.rsa /root/.abuild/*.rsa.pub /cles/
fi
KEY=$(ls /cles/*.rsa | head -1)
cp /cles/*.rsa /cles/*.rsa.pub /root/.abuild/
echo "PACKAGER_PRIVKEY=/root/.abuild/$(basename "$KEY")" > /root/.abuild/abuild.conf
cp /cles/*.rsa.pub /etc/apk/keys/

W=/tmp/plume
rm -rf "$W" && mkdir -p "$W"
# paquet NOM DOSSIER "description" "makedepends" "depends" [arch]
paquet() {
	nom=$1 dir=$2 desc=$3 mk=$4 dep=$5 arch=${6:-all}
	mkdir -p "$W/$nom"
	tar -czf "$W/$nom/$nom-$VERSION.tar.gz" -C /src --exclude='*.o' --exclude='*-protocol.[ch]' \
		--transform "s|^$dir|$nom-$VERSION|" "$dir"
	cat > "$W/$nom/APKBUILD" <<EOF
# Plume Linux — généré par build/depot-inside.sh
pkgname=$nom
pkgver=$VERSION
pkgrel=$REL
pkgdesc="$desc"
url="https://localhost/plume"
arch="$arch"
license="GPL-3.0-or-later"
depends="$dep"
makedepends="$mk"
source="$nom-$VERSION.tar.gz"
builddir="\$srcdir/$nom-$VERSION"
options="!check"

build() {
	make
}

package() {
	make install DESTDIR="\$pkgdir" PREFIX=/usr
	$EXTRA
}
EOF
	(cd "$W/$nom" && abuild -F checksum >/dev/null && REPODEST=/depot abuild -F -d -q) \
		|| { echo "ÉCHEC : $nom" >&2; exit 1; }
	EXTRA=""
}
WL="wayland-dev wayland-protocols cairo-dev pango-dev libxkbcommon-dev"
EXTRA='mkdir -p "$pkgdir"/usr/share/plume && "$pkgdir"/usr/bin/plume-wall "$pkgdir"/usr/share/plume/wallpaper.png 2560 1440'
paquet plume-hud hud "Écran de connexion et de verrouillage HUD" "$WL linux-pam-dev" "greetd cage"
paquet plume-shell shell "Barre et dock HUD (sway)" "$WL wlr-protocols" ""
paquet plume-term term "Terminal HUD et poste de pilotage" "$WL libvterm-dev" ""
paquet plume-lanceur lanceur "Toutes les applications, en grille" "$WL wlr-protocols librsvg-dev" ""
paquet plume-fonds fonds "Apparence : fond d'écran et couleur du thème" "$WL gdk-pixbuf-dev font-dejavu" ""
paquet plume-wifi wifi "Panneau wifi HUD (iwd)" "$WL" "iwd"
paquet plume-son son "Panneau son HUD (PipeWire)" "$WL" "wireplumber"
paquet plume-bluetooth bluetooth "Panneau Bluetooth HUD (bluez)" "$WL" "bluez"
paquet plume-logitheque logitheque "Logithèque : installer des applications" "$WL" "sudo"

# scripts Plume (sans compilation)
mkdir -p /tmp/outils/plume-outils-$VERSION
cp /src/rootfs/etc/plume/conf/plume-maj /src/rootfs/etc/plume/conf/plume-parefeu /src/rootfs/etc/plume/conf/plume-theme \
	/src/rootfs/etc/plume/conf/plume-emulation /src/rootfs/etc/plume/conf/plume-menu /src/rootfs/etc/plume/conf/plume-bar \
	/src/rootfs/etc/plume/conf/plume-terminal /src/rootfs/etc/plume/conf/plume-greeter /src/rootfs/etc/plume/conf/plume-secureboot /tmp/outils/plume-outils-$VERSION/
printf 'install:\n\tfor f in plume-*; do install -Dm755 $$f $(DESTDIR)$(PREFIX)/bin/$$f; done\nall:\n' \
	> /tmp/outils/plume-outils-$VERSION/Makefile
mkdir -p "$W/plume-outils"
tar -czf "$W/plume-outils/plume-outils-$VERSION.tar.gz" -C /tmp/outils "plume-outils-$VERSION"
cat > "$W/plume-outils/APKBUILD" <<EOF
pkgname=plume-outils
pkgver=$VERSION
pkgrel=$REL
pkgdesc="Scripts Plume : mises à jour, pare-feu, thème, émulation, lanceurs"
url="https://localhost/plume"
arch="noarch"
license="GPL-3.0-or-later"
depends="nftables sudo"
source="plume-outils-$VERSION.tar.gz"
builddir="\$srcdir/plume-outils-$VERSION"
options="!check"
package() {
	make install DESTDIR="\$pkgdir" PREFIX=/usr
}
EOF
(cd "$W/plume-outils" && abuild -F checksum >/dev/null && REPODEST=/depot abuild -F -d -q)

# méta-paquet : tout le bureau Plume d'un coup
mkdir -p "$W/plume-bureau"
cat > "$W/plume-bureau/APKBUILD" <<EOF
pkgname=plume-bureau
pkgver=$VERSION
pkgrel=$REL
pkgdesc="Bureau Plume complet (composants HUD)"
url="https://localhost/plume"
arch="noarch"
license="GPL-3.0-or-later"
depends="plume-hud plume-shell plume-term plume-lanceur plume-fonds plume-wifi plume-son plume-bluetooth plume-logitheque plume-outils"
options="!check"
package() {
	mkdir -p "\$pkgdir"
}
EOF
(cd "$W/plume-bureau" && REPODEST=/depot abuild -F -d -q)
cp /cles/*.rsa.pub /depot/plume/
ls -l /depot/plume/*/ | tail -n +2
