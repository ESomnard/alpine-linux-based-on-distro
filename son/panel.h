/*
 * plume-son — panneau son de Plume, au style HUD (remplace pavucontrol).
 * Sorties (haut-parleurs, casque…) et entrées (micros) : appareil par défaut,
 * volume, muet ; volume de chaque application. Parle à PipeWire par wpctl.
 *   plume-son        ouvre le panneau (clic sur SON dans la barre)
 */
#ifndef PLUME_PANEL_H
#define PLUME_PANEL_H

#define PANEL_ID    "plume-son"
#define PANEL_TITLE "Son — Plume"
#define PANEL_W 560
#define PANEL_H 600

#include "panel-base.h"

#define MAX_NODES 64
enum { N_SINK, N_SOURCE, N_STREAM };

struct node {
	int id, kind;
	char name[128];
	double vol;
	bool muted, def;
	double hold_until; /* volume changé ici : on ignore les relectures un instant */
};

enum { A_ROW = 1, A_SLIDER, A_MUTE };

struct app {
	PANEL_BASE_FIELDS
	bool no_wpctl, loaded;
	struct node nodes[MAX_NODES];
	int nnodes;
	int drag;          /* curseur tenu (index), -1 : aucun */
	struct rect drag_r;
};

extern struct app A;

#endif
