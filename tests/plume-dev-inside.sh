#!/bin/sh
# Test du pack développeur (plume-dev) dans un conteneur Void, lancé par tests/plume-dev.sh.
xbps-install -Syu xbps >/dev/null 2>&1; xbps-install -Syu >/dev/null 2>&1
xbps-install -Sy bash nftables >/dev/null 2>&1
install -m755 /w/void/rootfs/etc/plume/conf/plume-dev /usr/bin/plume-dev
install -Dm644 /w/void/rootfs/etc/plume/projets.conf /etc/plume/projets.conf
useradd -m -s /bin/bash testeur

echo "=== partie système"
plume-dev --systeme testeur 2>&1 | tail -6
echo "=== JDK"; ls /usr/lib/jvm/
echo "=== groupes"; id testeur
echo "=== bashrc.d lu par bash ?"; grep -n 'bashrc.d' /etc/bash/bashrc
echo "=== variables dans un terminal"
su testeur -s /bin/bash -c 'cd; bash -ic "echo JAVA_HOME=\$JAVA_HOME; java -version 2>&1 | head -1"' 2>/dev/null
echo "=== caffeinate"; caffeinate -dims echo "caffeinate marche"
echo "=== liste"; su testeur -s /bin/sh -c 'cd; plume-dev liste'

echo "=== partie compte (SDK Android, Flutter, Claude Code)"
cat > /tmp/compte.sh <<'EOF'
cd
# mêmes fonctions que « plume-dev installer » (partie compte, après sudo)
sed -n '/^android_sdk()/,/^installer()/p' /usr/bin/plume-dev | sed '$d' > /tmp/f.sh
SDK=$HOME/Android/Sdk FLUTTER=$HOME/development/flutter ANDROID_API=36 ANDROID_BUILD_TOOLS=36.0.0
titre() { printf '\n==> %s\n' "$1"; }; ok() { echo "  ok $1"; }; ko() { echo "  KO $1"; }
ENV_FILE=/etc/profile.d/plume-dev.sh; charger_env() { . "$ENV_FILE"; }
. /tmp/f.sh
compte
EOF
su testeur -s /bin/sh -c 'export HOME=/home/testeur; sh /tmp/compte.sh' 2>&1 | tail -25
echo "=== vérification finale"
su testeur -s /bin/sh -c 'export HOME=/home/testeur; cd; plume-dev verifier' 2>&1 | tail -25
echo "=== clonage (sans compte GitHub : seuls les dépôts publics passent)"
su testeur -s /bin/sh -c 'export HOME=/home/testeur; cd; PLUME_PROJETS=/tmp/p plume-dev projets' </dev/null 2>&1 | tail -8
