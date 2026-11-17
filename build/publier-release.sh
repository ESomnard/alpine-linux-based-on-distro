#!/bin/sh
# Prépare la mise en téléchargement des deux ISO de Plume sur GitHub (une « Release ») :
#   ./build/publier-release.sh            utilise les ISO déjà dans out/
# Il calcule les sommes SHA-256, écrit les notes de version et AFFICHE la commande
# à lancer (gh) ou la marche à suivre sur le site — il ne publie rien lui-même.
# Taille max d'un fichier de Release : 2 Go (les ISO de Plume passent).
set -eu
cd "$(dirname "$0")/.."
VERSION=$(cat paquets/VERSION)
TAG="v$VERSION"
ALP=$(ls -t out/alpine-plume-*.iso 2>/dev/null | head -1 || true)
VOI=$(ls -t out/plume-void-*.iso 2>/dev/null | head -1 || true)
[ -n "$ALP" ] || { echo "ISO Alpine absente : make iso" >&2; exit 1; }
[ -n "$VOI" ] || { echo "ISO Void absente : make iso-void" >&2; exit 1; }

R=out/release
rm -rf "$R" && mkdir -p "$R"
# noms clairs pour le téléchargement (liens physiques : pas de copie de 2 Go)
A="$R/plume-alpine-$VERSION-x86_64.iso"
V="$R/plume-void-$VERSION-x86_64.iso"
ln "$ALP" "$A" 2>/dev/null || cp "$ALP" "$A"
ln "$VOI" "$V" 2>/dev/null || cp "$VOI" "$V"
(cd "$R" && shasum -a 256 ./*.iso | sed 's| \./| |' > SHA256SUMS)
taille() { du -h "$1" | cut -f1; }
cat > "$R/notes.md" <<NOTES
## Plume Linux $VERSION

Deux versions, le même bureau HUD (barre, dock, terminal, lanceur, panneaux wifi / son /
Bluetooth, Logithèque, mises à jour en un clic, pare-feu, AppArmor, disque chiffré en option).

| Fichier | Base | Pour qui |
|---|---|---|
| \`$(basename "$A")\` ($(taille "$A")) | Alpine Linux (musl, OpenRC) | le plus léger ; bureau installé au premier démarrage (Internet nécessaire) |
| \`$(basename "$V")\` ($(taille "$V")) | Void Linux (glibc, runit) | compatibilité maximale (logiciels binaires, jeux, pilotes NVIDIA) ; bureau déjà dans l'ISO, essai en live, installation sans Internet |

### Installer
1. Copier l'ISO sur une clé USB (Balena Etcher, Ventoy, ou \`dd\`).
2. Démarrer dessus (BIOS ou UEFI).
3. **Alpine** : se connecter en \`root\` puis \`sh /etc/plume/install.sh\`.
   **Void** : session live \`plume\` / mot de passe \`voidlinux\`, puis dans un terminal \`sudo sh /etc/plume/install.sh\`.

### Vérifier le téléchargement
\`\`\`
sha256sum -c SHA256SUMS
\`\`\`

Logiciel libre sous licence GPL-3.0-or-later.
NOTES

echo "Fichiers prêts dans $R :"
ls -lh "$R"
cat <<FIN

Pour les mettre en téléchargement sur GitHub :

  A) avec l'outil gh (brew install gh, puis gh auth login) :
     gh release create $TAG --title "Plume Linux $VERSION" --notes-file $R/notes.md \\
         $A $V $R/SHA256SUMS

  B) ou sur le site : ton dépôt → Releases → « Draft a new release », tag $TAG,
     colle le contenu de $R/notes.md, glisse les deux .iso et SHA256SUMS, « Publish release ».

Lien de téléchargement ensuite : https://github.com/ESomnard/alpine-linux-based-on-distro/releases/latest
(le dépôt doit être public pour que tout le monde puisse télécharger).
FIN
