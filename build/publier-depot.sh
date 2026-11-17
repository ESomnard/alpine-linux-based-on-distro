#!/bin/sh
# Prépare la publication des dépôts Plume sur GitHub Pages (branche gh-pages) :
# .apk (Plume Alpine, musl) dans plume/ et .xbps (Plume Void, glibc) dans plume-void/.
#   ./build/publier-depot.sh
# 1. construit les deux dépôts signés (make depot depot-void) ;
# 2. les copie dans out/gh-pages/, un « worktree » git de la branche gh-pages
#    (branche à part, sans le code : seulement les paquets) ;
# 3. AFFICHE les commandes pour commiter et pousser — il ne commite ni ne pousse rien.
# Une seule fois sur GitHub : Settings → Pages → Source « Deploy from a branch »,
# branche gh-pages, dossier / (root).
# Adresse du dépôt ensuite : https://<compte>.github.io/<dépôt>/plume
set -eu
cd "$(dirname "$0")/.."
WT=out/gh-pages

./build/depot.sh
./void/depot.sh

if [ ! -e "$WT/.git" ]; then
	if git show-ref -q --verify refs/heads/gh-pages || git ls-remote -q --exit-code --heads origin gh-pages >/dev/null 2>&1; then
		git show-ref -q --verify refs/heads/gh-pages || git fetch -q origin gh-pages:gh-pages
		git worktree add -q "$WT" gh-pages
	else
		# première publication : branche gh-pages vide, sans historique du code
		git worktree add -q --detach "$WT"
		git -C "$WT" checkout -q --orphan gh-pages
		git -C "$WT" rm -rq --cached . >/dev/null 2>&1 || true
		find "$WT" -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf {} +
	fi
fi

# seulement la dernière version des paquets
rm -rf "$WT/plume" "$WT/plume-void"
cp -r out/depot/plume "$WT/plume"
cp -r out/depot-void "$WT/plume-void"
touch "$WT/.nojekyll"   # GitHub sert les fichiers tels quels
URL=$(git remote get-url origin 2>/dev/null | sed -E 's#^(git@github.com:|https://github.com/)##; s#\.git$##')
COMPTE=$(echo "${URL%%/*}" | tr 'A-Z' 'a-z')
DEPOT=${URL#*/}
PAGES="https://$COMPTE.github.io/$DEPOT/plume"
RELEASES="https://github.com/$URL/releases/latest"
VERSION=$(ls "$WT"/plume/*/plume-bureau-*.apk | sed 's/.*plume-bureau-//; s/\.apk$//')
cat > "$WT/index.html" <<EOF
<!doctype html>
<html lang="fr"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Dépôt Plume</title>
<style>body{background:#02060b;color:#cdeefc;font:16px/1.6 "JetBrains Mono",monospace;max-width:720px;margin:40px auto;padding:0 16px}
h1{color:#4fb8e0;letter-spacing:.2em}code{color:#9fe4ff}</style></head>
<body><h1>◆ DÉPÔT PLUME</h1>
<p>Paquets signés des outils de Plume Linux (version $VERSION).</p>
<p><b>Télécharger Plume</b> (ISO Alpine et ISO Void) : <a style="color:#4fb8e0" href="$RELEASES">$RELEASES</a></p>
<p>Plume Alpine (musl), dans <code>/etc/apk/repositories</code> :<br><code>$PAGES</code></p>
<p>Plume Void (glibc), dans <code>/etc/xbps.d/10-plume.conf</code> :<br><code>repository=$PAGES-void/x86_64</code></p>
<p>Déjà configuré sur Plume : <code>plume-maj</code> suffit. Logiciel libre sous GPL-3.0-or-later.</p>
</body></html>
EOF
git -C "$WT" add -A
echo
echo "Dépôts prêts dans $WT (version $VERSION) : $PAGES et $PAGES-void"
echo "Pour le publier :"
echo "    git -C $WT commit -m \"Dépôt Plume $VERSION\""
echo "    git -C $WT push -u origin gh-pages"
