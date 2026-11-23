#!/bin/sh
# Plume (Void, glibc) — installation sur disque depuis l'ISO live.
#   sudo sh /etc/plume/install.sh                  questions, puis installation
#   sudo sh /etc/plume/install.sh --auto FICHIER   100 % automatique (modèle :
#                                                  /etc/plume/install-auto.conf)
# Le système live (bureau compris) est recopié sur le disque : pas besoin d'Internet.
# Disque : GPT, démarre en BIOS ET en UEFI ; racine chiffrée en LUKS2 si demandé
# (/boot reste en clair pour pouvoir démarrer).
set -eu

[ "$(id -u)" -eq 0 ] || exec sudo sh "$0" "$@"

ask() { # ask "question" "défaut"
	printf '%s [%s] : ' "$1" "$2" >&2
	read -r _r
	echo "${_r:-$2}"
}
secret() { # secret "question" -> lu sans écho
	printf '%s : ' "$1" >&2
	stty -echo 2>/dev/null || true
	read -r _r
	stty echo 2>/dev/null || true
	echo >&2
	echo "$_r"
}
log() { printf '\033[1;36m==>\033[0m %s\n' "$*"; }

CLAVIER="fr" FUSEAU="Europe/Paris" MACHINE="plume" DISQUE="" UTILISATEUR="" NOM_COMPLET=""
MOT_DE_PASSE="" MOT_DE_PASSE_ROOT="" WIFI_NOM="" WIFI_MOT_DE_PASSE=""
APPLIS="oui" REDEMARRER="oui" CHIFFREMENT="non" PHRASE_CHIFFREMENT=""
NOYAU_EXTRA=""   # options du noyau en plus (ex. « nomodeset », « console=ttyS0 »)

if [ "${1:-}" = --auto ]; then
	AUTO="${2:-}"
	[ -r "$AUTO" ] || { echo "Fichier de réponses introuvable : $AUTO (modèle : /etc/plume/install-auto.conf)" >&2; exit 1; }
	# shellcheck disable=SC1090
	. "$AUTO"
	CLAVIER=${CLAVIER%% *}   # « fr fr » (format Alpine) -> « fr »
	for v in DISQUE UTILISATEUR MOT_DE_PASSE; do
		eval "[ -n \"\${$v}\" ]" || { echo "Réponse manquante dans $AUTO : $v" >&2; exit 1; }
	done
else
	echo "=== Installation de Plume Linux (Void) ==="
	echo
	lsblk -dno NAME,SIZE,MODEL | grep -v -e '^loop' -e '^sr' -e '^zram' | sed 's/^/  /'
	echo
	DISQUE="/dev/$(ask 'Disque cible (il sera EFFACÉ)' "$(lsblk -dno NAME | grep -v -e loop -e sr -e zram | head -1)")"
	CLAVIER="$(ask 'Disposition clavier (fr, us, ch…)' fr)"
	FUSEAU="$(ask 'Fuseau horaire' 'Europe/Paris')"
	MACHINE="$(ask 'Nom de la machine' plume)"
	NOM_COMPLET="$(ask 'Ton nom (affiché, accents et espaces permis)' '')"
	UTILISATEUR="$(ask 'Ton identifiant pour te connecter (lettres, majuscules permises, sans espace)' moi)"
	while :; do
		MOT_DE_PASSE="$(secret 'Ton mot de passe (aussi celui de sudo)')"
		[ -n "$MOT_DE_PASSE" ] && [ "$MOT_DE_PASSE" = "$(secret 'Encore une fois')" ] && break
		echo "Vide ou différent, recommence." >&2
	done
	echo
	echo "Chiffrer le disque ? Sans ta phrase secrète, personne ne peut lire tes fichiers"
	echo "(PC volé, disque retiré). Elle sera demandée à chaque démarrage. Oubliée = données perdues."
	case "$(ask 'Chiffrer le disque (o/n)' n)" in [oOyY]*) CHIFFREMENT=oui ;; esac
	if [ "$CHIFFREMENT" = oui ]; then
		while :; do
			PHRASE_CHIFFREMENT="$(secret 'Phrase secrète (8 caractères minimum)')"
			[ ${#PHRASE_CHIFFREMENT} -ge 8 ] && [ "$PHRASE_CHIFFREMENT" = "$(secret 'Encore une fois')" ] && break
			echo "Trop courte ou différente, recommence." >&2
		done
	fi
	case "$(ask 'Garder les applications (Firefox, mpv…) (o/n)' o)" in [nN]*) APPLIS=non ;; esac
	echo
	printf 'TOUT le contenu de %s va être effacé. Taper « oui » pour continuer : ' "$DISQUE"
	read -r ok
	[ "$ok" = oui ] || { echo "Annulé."; exit 1; }
fi
[ -b "$DISQUE" ] || { echo "Disque introuvable : $DISQUE" >&2; exit 1; }
if [ "$CHIFFREMENT" = oui ] && [ ${#PHRASE_CHIFFREMENT} -lt 8 ]; then
	echo "CHIFFREMENT=oui : PHRASE_CHIFFREMENT doit faire au moins 8 caractères" >&2
	exit 1
fi
echo "$UTILISATEUR" | grep -Eq '^[A-Za-z_][A-Za-z0-9._-]{0,31}$' || { echo "Identifiant invalide : $UTILISATEUR (lettres, chiffres, . - ou _, sans espace, 32 caractères max.)" >&2; exit 1; }
NOM_COMPLET=$(printf '%s' "$NOM_COMPLET" | tr -d ':,\n')   # interdits dans /etc/passwd

# /dev/sda -> /dev/sda1 ; /dev/nvme0n1 -> /dev/nvme0n1p1
part() { case "$DISQUE" in *[0-9]) echo "${DISQUE}p$1" ;; *) echo "$DISQUE$1" ;; esac; }

MNT=/mnt/plume-cible
CLE=""
nettoyer() {
	[ -n "$CLE" ] && rm -f "$CLE"
	umount -R "$MNT" 2>/dev/null || true
	cryptsetup close plume-cible 2>/dev/null || true
}
trap nettoyer EXIT

log "Wifi"
if [ -n "$WIFI_NOM" ]; then
	DEV=$(iwctl device list 2>/dev/null | awk '/station/ {print $1; exit}')
	iwctl --passphrase "$WIFI_MOT_DE_PASSE" station "${DEV:-wlan0}" connect "$WIFI_NOM" || true
	sleep 5
fi

log "Partitions sur $DISQUE (BIOS + UEFI)"
for p in "$DISQUE"?*; do umount "$p" 2>/dev/null || true; done
wipefs -aq "$DISQUE"
# sgdisk (et pas sfdisk) : GRUB ne reconnaît pas la partition BIOS faite par sfdisk
sgdisk -Z "$DISQUE" >/dev/null 2>&1 || true
if [ "$CHIFFREMENT" = oui ]; then
	sgdisk -n1:0:+1M -t1:ef02 -c1:bios -n2:0:+512M -t2:ef00 -c2:efi \
		-n3:0:+1G -t3:8300 -c3:boot -n4:0:0 -t4:8300 -c4:plume "$DISQUE" >/dev/null
else
	sgdisk -n1:0:+1M -t1:ef02 -c1:bios -n2:0:+512M -t2:ef00 -c2:efi \
		-n3:0:0 -t3:8300 -c3:plume "$DISQUE" >/dev/null
fi
partprobe "$DISQUE" 2>/dev/null || blockdev --rereadpt "$DISQUE" 2>/dev/null || true
udevadm settle 2>/dev/null || sleep 2
ESP=$(part 2)
if [ "$CHIFFREMENT" = oui ]; then BOOTDEV=$(part 3) ROOTPART=$(part 4); else BOOTDEV="" ROOTPART=$(part 3); fi

mkfs.vfat -F 32 -n EFI "$ESP" >/dev/null
ROOTDEV=$ROOTPART
if [ "$CHIFFREMENT" = oui ]; then
	log "Chiffrement LUKS2 de la racine"
	CLE=$(mktemp /tmp/plume-cle.XXXXXX)
	chmod 600 "$CLE"
	printf '%s' "$PHRASE_CHIFFREMENT" > "$CLE"
	cryptsetup luksFormat -q --type luks2 --key-file "$CLE" "$ROOTPART"
	cryptsetup open --key-file "$CLE" "$ROOTPART" plume-cible
	ROOTDEV=/dev/mapper/plume-cible
	mkfs.ext4 -q -L boot "$BOOTDEV"
fi
# btrfs : instantanés du système avant chaque mise à jour (plume-retour), compression zstd.
# Sous-volumes : @ (système), @home (tes fichiers, jamais touchés par un retour en arrière),
# @snapshots (instantanés).
mkfs.btrfs -q -f -L plume "$ROOTDEV"
mkdir -p "$MNT"
mount "$ROOTDEV" "$MNT"
for sv in @ @home @snapshots; do btrfs -q subvolume create "$MNT/$sv"; done
umount "$MNT"
OPTS=noatime,compress=zstd:1
mount -o "subvol=@,$OPTS" "$ROOTDEV" "$MNT"
mkdir -p "$MNT/home" "$MNT/.snapshots"
mount -o "subvol=@home,$OPTS" "$ROOTDEV" "$MNT/home"
mount -o "subvol=@snapshots,$OPTS" "$ROOTDEV" "$MNT/.snapshots"
mkdir -p "$MNT/boot"
[ -n "$BOOTDEV" ] && mount "$BOOTDEV" "$MNT/boot"
mkdir -p "$MNT/boot/efi"
mount "$ESP" "$MNT/boot/efi"

log "Copie du système (quelques minutes)"
tar -C / --one-file-system --xattrs -cf - \
	--exclude=./proc --exclude=./sys --exclude=./dev --exclude=./run --exclude=./tmp \
	--exclude=./mnt --exclude=./media --exclude=./home --exclude=./var/cache/xbps \
	--exclude=./boot/efi --exclude=./var/lib/iwd --exclude=./.snapshots . 2>/dev/null |
	tar -C "$MNT" --xattrs --xattrs-include='*' -xpf -
ls "$MNT"/boot/vmlinuz-* >/dev/null 2>&1 || { echo "ERREUR : noyau absent de la copie" >&2; exit 1; }
for d in proc sys dev run tmp mnt media; do mkdir -p "$MNT/$d"; done
chmod 1777 "$MNT/tmp"

log "Réglages du système installé"
mount -t proc proc "$MNT/proc"
mount --rbind /sys "$MNT/sys"
mount --rbind /dev "$MNT/dev"
mount -t tmpfs tmpfs "$MNT/run"
cp -L /etc/resolv.conf "$MNT/etc/resolv.conf" 2>/dev/null || true
IN() { chroot "$MNT" "$@"; }

# ce qui ne sert qu'au live
LIVE_USER=$(sed -n 's/^USERNAME=//p' /etc/default/live.conf 2>/dev/null || echo plume)
IN userdel -r "$LIVE_USER" 2>/dev/null || IN userdel "$LIVE_USER" 2>/dev/null || true
rm -f "$MNT/etc/sudoers.d/99-void-live" "$MNT/etc/default/live.conf" \
	"$MNT/etc/polkit-1/rules.d/void-live.rules" "$MNT/etc/runit/core-services/04-plume-live.sh"
rm -rf "$MNT/etc/plume/install.sh" "$MNT/etc/plume/install-auto.conf" \
	"$MNT/etc/plume/installer-graphique.sh" "$MNT/usr/share/applications/plume-installer.desktop"
sed -i '/Installation sur disque/d' "$MNT/etc/motd"
sed -i 's/-a [^ ]* //' "$MNT/etc/sv/agetty-tty1/conf" 2>/dev/null || true

echo "$MACHINE" > "$MNT/etc/hostname"
sed -i "s/^#*KEYMAP=.*/KEYMAP=$CLAVIER/" "$MNT/etc/rc.conf"
grep -q '^KEYMAP=' "$MNT/etc/rc.conf" || echo "KEYMAP=$CLAVIER" >> "$MNT/etc/rc.conf"
[ -f "$MNT/usr/share/zoneinfo/$FUSEAU" ] && ln -sf "/usr/share/zoneinfo/$FUSEAU" "$MNT/etc/localtime"
sed -i "s|^#*TIMEZONE=.*|TIMEZONE=$FUSEAU|" "$MNT/etc/rc.conf"

# utilisateur (mots de passe : seulement dans /etc/shadow du système installé)
# --badnames : majuscules et points permis (comme Debian), refusés par défaut par useradd de Void
IN useradd --badnames -m -s /bin/bash -c "$NOM_COMPLET" -G wheel,audio,video,input,_seatd,network,bluetooth,storage,lpadmin,scanner "$UTILISATEUR"
printf '%s:%s\n' "$UTILISATEUR" "$MOT_DE_PASSE" | IN chpasswd -c SHA512
if [ -n "$MOT_DE_PASSE_ROOT" ]; then
	printf 'root:%s\n' "$MOT_DE_PASSE_ROOT" | IN chpasswd -c SHA512
else
	IN passwd -l root >/dev/null # pas de connexion root : on passe par sudo
fi

# wifi déjà connecté dans le live : gardé
if ls /var/lib/iwd/*.psk /var/lib/iwd/*.open >/dev/null 2>&1; then
	mkdir -p "$MNT/var/lib/iwd"
	cp -a /var/lib/iwd/. "$MNT/var/lib/iwd/"
fi

if [ "$APPLIS" = non ]; then
	log "Retrait des applications (APPLIS=non)"
	IN xbps-remove -Ry firefox firefox-i18n-fr mpv imv zathura zathura-pdf-mupdf pcmanfm >/dev/null 2>&1 || true
fi

uuid() { blkid -s UUID -o value "$1"; }
{
	echo "# Plume — systèmes de fichiers"
	echo "UUID=$(uuid "$ROOTDEV")  /             btrfs  subvol=@,$OPTS  0 0"
	echo "UUID=$(uuid "$ROOTDEV")  /home         btrfs  subvol=@home,$OPTS  0 0"
	echo "UUID=$(uuid "$ROOTDEV")  /.snapshots   btrfs  subvol=@snapshots,$OPTS  0 0"
	[ -n "$BOOTDEV" ] && echo "UUID=$(uuid "$BOOTDEV")  /boot      ext4  defaults,noatime  0 2"
	echo "UUID=$(uuid "$ESP")  /boot/efi  vfat  defaults,umask=0077  0 2"
	echo "tmpfs  /tmp  tmpfs  defaults,nosuid,nodev  0 0"
} > "$MNT/etc/fstab"

LSM="lsm=landlock,lockdown,yama,loadpin,safesetid,integrity,apparmor"
CMD="loglevel=4 $LSM${NOYAU_EXTRA:+ $NOYAU_EXTRA}"
# console série demandée : connexion possible dessus
case "$NOYAU_EXTRA" in *console=ttyS0*) ln -sf /etc/sv/agetty-ttyS0 "$MNT/etc/runit/runsvdir/default/" ;; esac
if [ "$CHIFFREMENT" = oui ]; then
	LUKS=$(uuid "$ROOTPART")
	echo "plume UUID=$LUKS none luks" > "$MNT/etc/crypttab"
	mkdir -p "$MNT/etc/dracut.conf.d"
	printf 'add_dracutmodules+=" crypt "\ninstall_items+=" /etc/crypttab "\n' > "$MNT/etc/dracut.conf.d/10-plume-crypt.conf"
	CMD="$CMD rd.luks.uuid=$LUKS"
fi
# GRUB : racine trouvée par son UUID, ligne de démarrage Plume
sed -i "s|^#*GRUB_CMDLINE_LINUX_DEFAULT=.*|GRUB_CMDLINE_LINUX_DEFAULT=\"$CMD\"|" "$MNT/etc/default/grub"
sed -i 's|^#*GRUB_DISTRIBUTOR=.*|GRUB_DISTRIBUTOR="Plume Linux"|' "$MNT/etc/default/grub"

log "Initramfs (dracut) pour ce PC"
rm -f "$MNT"/etc/dracut.conf.d/*live*
KVER=$(ls "$MNT/lib/modules" | sort -V | tail -1)
IN dracut -q --force --hostonly "/boot/initramfs-$KVER.img" "$KVER"

log "Chargeur de démarrage (GRUB, BIOS + UEFI)"
IN grub-install --target=i386-pc "$DISQUE" >/dev/null
# UEFI : entrée « Plume Linux » si le firmware le permet, et chemin de secours standard
IN grub-install --target=x86_64-efi --efi-directory=/boot/efi --bootloader-id=Plume --removable >/dev/null
[ -d /sys/firmware/efi ] && IN grub-install --target=x86_64-efi --efi-directory=/boot/efi --bootloader-id=Plume >/dev/null 2>&1 || true
IN grub-mkconfig -o /boot/grub/grub.cfg >/dev/null 2>&1
grep -q "initramfs-$KVER" "$MNT/boot/grub/grub.cfg" || { echo "ERREUR : configuration de GRUB incomplète" >&2; exit 1; }

sync
echo
echo "Installation terminée."
[ "$CHIFFREMENT" = oui ] && echo "Disque chiffré : ta phrase secrète sera demandée à chaque démarrage."
echo "Au démarrage : écran de connexion HUD → $UTILISATEUR + ton mot de passe."
# Secure Boot : Plume n'a pas de chargeur signé par Microsoft ; il faut tes propres clés
if [ -d /sys/firmware/efi ]; then
	cat <<'SB'

Secure Boot (démarrage sécurisé) : il est désactivé, sinon cette clé USB n'aurait pas démarré.
Pour le réactiver avec TES propres clés, une fois Plume installé :
  1. dans le BIOS/UEFI (touche F2, Suppr ou F10 au démarrage), section Secure Boot :
     « Reset to Setup Mode » ou « Clear Secure Boot keys » (les clés Microsoft seront remises) ;
  2. redémarre sur Plume, puis :  sudo plume-secureboot activer
  3. de retour dans le BIOS : active Secure Boot.
  État à tout moment :  plume-secureboot
SB
fi
if [ "$REDEMARRER" = oui ]; then
	echo "Redémarrage dans 10 s (retire la clé USB)…"
	sleep 10
	nettoyer
	trap - EXIT
	reboot
fi
