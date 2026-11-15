# Profil mkimage "plume" : on part du profil standard (seul profil officiel qui
# démarre sur du vrai matériel : noyau lts + firmwares) et on y ajoute notre overlay.
# Le système INSTALLÉ, lui, est allégé ensuite (voir /etc/plume/slim-firmware.sh).
profile_plume() {
	profile_standard
	profile_abbrev="plume"
	title="Plume Linux"
	desc="Distro ultra-légère basée sur Alpine, prête pour un usage quotidien."
	arch="x86_64 aarch64"
	hostname="plume"
	apkovl="genapkovl-plume.sh"
	# Outils utiles dès le live (wifi, partitionnement)
	apks="$apks iwd wpa_supplicant e2fsprogs dosfstools parted"
}
