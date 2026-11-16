/*
 * plume-lanceur — grille de toutes les applications installées, à la manière
 * de GNOME (« Afficher les applications »), au style HUD de Plume.
 *
 * Plein écran par-dessus le bureau (wlr-layer-shell) : recherche en haut,
 * icônes en pages, Entrée ou clic pour lancer, Échap pour fermer.
 * Relancer la commande ferme la grille (Super+A deux fois).
 */
#ifndef PLUME_LANCEUR_H
#define PLUME_LANCEUR_H

#define _GNU_SOURCE
#include <cairo.h>
#include <pango/pangocairo.h>
#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#define MAX_APPS 1024

struct rect {
	double x, y, w, h;
};

struct app_entry {
	char id[160];            /* nom du fichier .desktop */
	char name[96];
	char generic[96];
	char comment[200];
	char keywords[200];
	char exec[512];
	char icon[160];
	bool terminal;
	int uses;                /* nombre de lancements (pour classer la recherche) */
	cairo_surface_t *icon_surf;
	bool icon_tried;
};

struct buffer {
	struct wl_buffer *wl;
	void *data;
	size_t size;
	cairo_surface_t *cs;
	int w, h;
	bool busy;
};

struct lanceur {
	struct wl_display *display;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct wl_seat *seat;
	struct wl_keyboard *keyboard;
	struct wl_pointer *pointer;
	struct zwlr_layer_shell_v1 *layer_shell;
	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer;
	struct wl_callback *frame_cb;
	struct buffer buffers[2];
	int width, height;
	bool configured, running, dirty;
	double px, py, axis_acc;

	struct xkb_context *xkb_ctx;
	struct xkb_keymap *keymap;
	struct xkb_state *xkb_state;
	int32_t repeat_rate, repeat_delay;
	uint32_t repeat_key;
	int repeat_fd;

	struct app_entry apps[MAX_APPS];
	int napps;
	int view[MAX_APPS];      /* applis affichées (filtrées, triées) */
	int nview;
	char query[128];
	int sel;                 /* index dans view */
	int page, per_page, npages, cols;
	double start;

	/* zones cliquables (remplies au dessin) */
	struct rect r_cells[MAX_APPS];
	int cell_idx[MAX_APPS];
	int ncells;
	struct rect r_dots[64];
	int ndots;
};

extern struct lanceur L;

/* main.c */
double now_s(void);

/* apps.c */
void apps_load(void);
void apps_filter(void);
void app_launch(struct app_entry *a);
cairo_surface_t *app_icon(struct app_entry *a, int size);

/* draw.c */
void draw(cairo_t *cr, int W, int H);

#endif
