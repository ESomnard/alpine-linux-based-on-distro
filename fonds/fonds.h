/*
 * plume-fonds — choisir le fond d'écran de Plume, au style HUD.
 *
 * Fonds Plume (générés à l'installation), couleurs unies, ou tes images
 * (Images, Bureau, Téléchargements). Un clic applique tout de suite
 * (swaymsg) ; le choix est gardé dans ~/.config/plume/fond.conf et réappliqué
 * au démarrage de sway par « plume-fonds --appliquer ».
 *
 *   plume-fonds                     ouvre la fenêtre
 *   plume-fonds --appliquer         réapplique le fond enregistré (démarrage)
 *   plume-fonds --generer DOSSIER   crée les fonds Plume (à l'installation)
 */
#ifndef PLUME_FONDS_H
#define PLUME_FONDS_H

#define _GNU_SOURCE
#include <cairo.h>
#include <limits.h>
#include <pango/pangocairo.h>
#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#define MAX_ITEMS 400
#define MAX_HITS 600
#define THUMB_W 256
#define THUMB_H 144

struct rect {
	double x, y, w, h;
};

enum kind { K_PRESET, K_COLOR, K_IMAGE };

struct item {
	enum kind kind;
	char name[96];
	char path[PATH_MAX];
	uint32_t color;
	cairo_surface_t *thumb;
	bool thumb_tried, visible;
	int img_w, img_h;
};

enum mode { M_FILL, M_FIT, M_CENTER, M_TILE };

struct hit {
	struct rect r;
	int act;      /* 1 : élément, 2 : mode */
	int arg;
};

struct buffer {
	struct wl_buffer *wl;
	void *data;
	size_t size;
	cairo_surface_t *cs;
	int w, h;
	bool busy;
};

struct app {
	struct wl_display *display;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct wl_seat *seat;
	struct wl_keyboard *keyboard;
	struct wl_pointer *pointer;
	struct xdg_wm_base *wm_base;
	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *toplevel;
	struct wl_callback *frame_cb;
	struct buffer buffers[2];
	int width, height;
	bool configured, running, dirty;
	struct xkb_context *xkb_ctx;
	struct xkb_keymap *keymap;
	struct xkb_state *xkb_state;
	double px, py;

	struct item items[MAX_ITEMS];
	int nitems;
	int current;              /* élément appliqué (-1 : inconnu) */
	enum mode mode;
	cairo_surface_t *preview; /* aperçu en grand de l'élément appliqué */
	int preview_for;
	double scroll, content_h;
	struct hit hits[MAX_HITS];
	int nhits;
	char toast[200];
	bool toast_err;
	double toast_t;
};

extern struct app A;

/* main.c */
double now_s(void);
void set_toast(bool err, const char *fmt, ...);

/* gen.c */
int generate_presets(const char *dir, int w, int h);

/* images.c */
void items_load(void);
bool load_one_thumb(void);
cairo_surface_t *load_scaled(const char *path, int w, int h, bool cover, int *iw, int *ih);
void apply_item(int idx);
void apply_mode(enum mode m);
int apply_saved(void);

/* ui.c */
void ui_draw(cairo_t *cr, int W, int H);
void ui_click(double x, double y);
void ui_scroll(double dy);

#endif
