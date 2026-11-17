#!/bin/sh
# Plume (Void) — installateur graphique (session live) : questions à la souris,
# puis l'installation (install.sh --auto) se déroule dans le terminal Plume.
set -u
command -v yad >/dev/null || exec plume-terminal sh -c 'sudo sh /etc/plume/install.sh; exec bash'

TITRE="Installer Plume Linux"
erreur() { yad --title "$TITRE" --image dialog-error --text "$1" --button "Corriger:0" --center --width 420 >/dev/null 2>&1; }

# disques possibles (ni la clé USB du live, ni les CD, ni la mémoire compressée)
LIVE=$(lsblk -no PKNAME "$(findmnt -no SOURCE /run/initramfs/live 2>/dev/null)" 2>/dev/null | head -1)
DISQUES=$(lsblk -dno NAME,SIZE,MODEL,TYPE | awk -v l="$LIVE" '$NF=="disk" && $1!=l && $1!~/^(zram|loop|sr)/ {
	$NF=""; n=$1; $1=""; sub(/^ +/,""); sub(/ +$/,""); printf "%s%s — %s", (c++ ? "!" : ""), n, $0 }')
[ -n "$DISQUES" ] || { yad --title "$TITRE" --image dialog-error --text "Aucun disque trouvé pour installer Plume." --button OK:0 --center; exit 1; }

U="" M="plume" F="Europe/Paris" C="FALSE" A="TRUE" K="fr"
while :; do
	R=$(yad --title "$TITRE" --window-icon system-software-install --center --width 560 \
		--form --separator='\n' --item-separator='!' \
		--text "<big><b>Installer Plume Linux sur ce PC</b></big>\n\nLe disque choisi sera <b>entièrement effacé</b>. Durée : 5 à 15 minutes, sans Internet." \
		--field "Disque:CB" "$DISQUES" \
		--field "Ton identifiant (minuscules, sans espace)" "$U" \
		--field "Mot de passe:H" "" \
		--field "Mot de passe (encore une fois):H" "" \
		--field "Nom de l'ordinateur" "$M" \
		--field "Clavier:CB" "^$K!fr!us!be!ch!ca!de!es!it" \
		--field "Fuseau horaire" "$F" \
		--field "Chiffrer le disque (protège tes fichiers si le PC est volé):CHK" "$C" \
		--field "Phrase secrète du chiffrement (8 caractères min., demandée à chaque démarrage):H" "" \
		--field "Garder les applications (Firefox, lecteur vidéo…):CHK" "$A" \
		--button "Annuler:1" --button "Continuer:0") || exit 0
	{ read -r D; read -r U; read -r P1; read -r P2; read -r M; read -r K; read -r F; read -r C; read -r PH; read -r A; } <<EOF
$R
EOF
	DISQUE=/dev/${D%% *}
	echo "$U" | grep -Eq '^[a-z_][a-z0-9_-]*$' || { erreur "Identifiant invalide : minuscules, chiffres, - ou _ (ex. : marie)."; continue; }
	[ -n "$P1" ] || { erreur "Choisis un mot de passe."; continue; }
	[ "$P1" = "$P2" ] || { erreur "Les deux mots de passe sont différents."; continue; }
	echo "$M" | grep -Eq '^[a-zA-Z0-9-]+$' || M=plume
	[ -f "/usr/share/zoneinfo/$F" ] || { erreur "Fuseau horaire inconnu : $F (ex. : Europe/Paris, America/Montreal)."; continue; }
	if [ "$C" = TRUE ] && [ ${#PH} -lt 8 ]; then erreur "Phrase secrète trop courte (8 caractères minimum)."; continue; fi
	break
done

yad --title "$TITRE" --image dialog-warning --center --width 460 \
	--text "<b>Dernière vérification</b>\n\nTout le contenu de <b>$D</b> va être <b>effacé</b>.\nCompte : <b>$U</b> — clavier $K — chiffrement : $([ "$C" = TRUE ] && echo oui || echo non)" \
	--button "Annuler:1" --button "Effacer et installer:0" || exit 0

# réglages pour install.sh --auto (lisibles par toi seul, effacés à la fin)
q() { printf "'%s'" "$(printf '%s' "$1" | sed "s/'/'\\\\''/g")"; }
umask 077
CONF=$(mktemp /tmp/plume-installer.XXXXXX)
{
	echo "DISQUE=$(q "$DISQUE")"
	echo "UTILISATEUR=$(q "$U")"
	echo "MOT_DE_PASSE=$(q "$P1")"
	echo "CLAVIER=$(q "$K")"
	echo "FUSEAU=$(q "$F")"
	echo "MACHINE=$(q "$M")"
	echo "APPLIS=$([ "$A" = TRUE ] && echo oui || echo non)"
	echo "CHIFFREMENT=$([ "$C" = TRUE ] && echo oui || echo non)"
	echo "PHRASE_CHIFFREMENT=$(q "$PH")"
	echo "REDEMARRER=non"
} > "$CONF"

exec plume-terminal -T "Installation de Plume" sh -c "
	sudo sh /etc/plume/install.sh --auto '$CONF'; r=\$?
	rm -f '$CONF'
	echo
	if [ \$r -eq 0 ]; then
		printf 'Plume est installé ! Retire la clé USB, puis Entrée pour redémarrer (n + Entrée pour rester) : '
		read a; case \"\$a\" in n*|N*) exec bash ;; esac
		sudo reboot
	else
		echo 'L installation a échoué (voir ci-dessus). Rien n a été changé si le partitionnement n a pas commencé.'
		exec bash
	fi"
