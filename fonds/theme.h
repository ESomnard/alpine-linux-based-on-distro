/*
 * Thème de Plume : couleur d'accent choisie avec plume-theme (« Apparence »).
 * Lue dans ~/.config/plume/theme.conf (accent=, dim=, bright= en RRGGBB) ;
 * cyan HUD par défaut. Fichier identique dans chaque appli Plume.
 */
#ifndef PLUME_THEME_H
#define PLUME_THEME_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t plume_theme_c[3] = { 0x4fb8e0, 0x1d5a78, 0x9fe4ff };
static int plume_theme_ok;

static inline void plume_theme_load(void)
{
	if (plume_theme_ok)
		return;
	plume_theme_ok = 1;
	const char *home = getenv("HOME");
	char p[512];
	snprintf(p, sizeof p, "%s/.config/plume/theme.conf", home ? home : "");
	FILE *f = fopen(p, "r");
	char l[128];
	while (f && fgets(l, sizeof l, f)) {
		static const char *keys[3] = { "accent=", "dim=", "bright=" };
		for (int i = 0; i < 3; i++) {
			size_t n = strlen(keys[i]);
			if (!strncmp(l, keys[i], n))
				plume_theme_c[i] = (uint32_t)strtoul(l + n + (l[n] == '#'), NULL, 16) & 0xffffff;
		}
	}
	if (f)
		fclose(f);
}

/* relire après un changement de thème */
static inline void plume_theme_reset(void)
{
	plume_theme_ok = 0;
}

#define C_LINE    (plume_theme_load(), plume_theme_c[0])
#define C_LINEDIM (plume_theme_load(), plume_theme_c[1])
#define C_CORNER  (plume_theme_load(), plume_theme_c[2])
/* composantes 0..1 pour les dégradés cairo */
#define ACC_RGB ((C_LINE >> 16) & 255) / 255.0, ((C_LINE >> 8) & 255) / 255.0, (C_LINE & 255) / 255.0
#define DIM_RGB ((C_LINEDIM >> 16) & 255) / 255.0, ((C_LINEDIM >> 8) & 255) / 255.0, (C_LINEDIM & 255) / 255.0

#endif
