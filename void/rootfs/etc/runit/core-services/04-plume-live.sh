# Plume (Void) — réglages du système LIVE (clé USB) uniquement.
# L'utilisateur live (« plume ») est créé au démarrage par l'initramfs de Void :
# on lui donne les groupes du bureau (écran, son, wifi, bluetooth).
if [ -r /etc/default/live.conf ]; then
	. /etc/default/live.conf
	echo plume-live > /etc/hostname
	for g in _seatd input network bluetooth storage lpadmin scanner; do
		usermod -aG "$g" "${USERNAME:-plume}" 2>/dev/null || true
	done
fi
