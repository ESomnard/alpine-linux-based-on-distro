/*
 * plume-bluetooth — panneau Bluetooth de Plume, au style HUD (remplace blueman).
 * Appareils associés (connecter, déconnecter, oublier, batterie) et appareils à
 * proximité (associer en un clic) ; Bluetooth activé / désactivé.
 * Parle à bluez par bluetoothctl.
 *   plume-bluetooth     ouvre le panneau (clic sur BT dans la barre)
 */
#ifndef PLUME_PANEL_H
#define PLUME_PANEL_H

#define PANEL_ID    "plume-bluetooth"
#define PANEL_TITLE "Bluetooth — Plume"
#define PANEL_W 560
#define PANEL_H 600

#include "panel-base.h"

#define MAX_DEVS 64

struct dev {
	char mac[18];
	char name[96];
	char icon[32];
	bool paired, connected, trusted;
	int battery; /* -1 : inconnue */
};

enum { A_DEV = 1, A_CONNECT, A_DISCONNECT, A_PAIR, A_FORGET, A_POWER, A_SCAN };

struct app {
	PANEL_BASE_FIELDS
	bool no_ctl, no_adapter, powered, loaded, blocked;
	char adapter[64];
	struct dev devs[MAX_DEVS];
	int ndevs;
	char busy_mac[18];
};

extern struct app A;

#endif
