/* Panneau Plume — partie commune (fenêtre, entrées, dessin HUD, commandes).
 * Chaque panneau a son panel.h qui définit PANEL_* puis inclut ce fichier. */
#ifndef PLUME_PANEL_BASE_H
#define PLUME_PANEL_BASE_H

#define _GNU_SOURCE
#include <cairo.h>
#include <pango/pangocairo.h>
#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#define MAX_HITS 300

struct rect {
	double x, y, w, h;
};

struct hit {
	struct rect r;
	int act, arg;
};

struct buffer {
	struct wl_buffer *wl;
	void *data;
	size_t size;
	cairo_surface_t *cs;
	int w, h;
	bool busy;
};

/* champs communs, en tête de struct app */
#define PANEL_BASE_FIELDS                                                       \
	struct wl_display *display;                                             \
	struct wl_compositor *compositor;                                       \
	struct wl_shm *shm;                                                     \
	struct wl_seat *seat;                                                   \
	struct wl_keyboard *keyboard;                                           \
	struct wl_pointer *pointer;                                             \
	struct xdg_wm_base *wm_base;                                            \
	struct wl_surface *surface;                                             \
	struct xdg_surface *xdg_surface;                                        \
	struct xdg_toplevel *toplevel;                                          \
	struct wl_callback *frame_cb;                                           \
	struct buffer buffers[2];                                               \
	int width, height;                                                      \
	bool configured, running, dirty, pressed;                               \
	struct xkb_context *xkb_ctx;                                            \
	struct xkb_keymap *keymap;                                              \
	struct xkb_state *xkb_state;                                            \
	double px, py;                                                          \
	bool busy, scanning;                                                    \
	int sel;                                                                \
	double scroll, content_h;                                               \
	struct hit hits[MAX_HITS];                                              \
	int nhits;                                                              \
	char toast[200];                                                        \
	bool toast_err;                                                         \
	double toast_t;

/* main.c */
double now_s(void);
void set_toast(bool err, const char *fmt, ...);

/* run.c : commandes système en arrière-plan */
enum { RUN_NONE, RUN_REPLACE, RUN_SKIP };
void run_push(int job, const char *tag, int dedupe, ...); /* argv…, NULL */
void run_start(void);
void run_read(void);
int run_fd(void);
bool run_idle(void);
void run_done(int job, const char *tag, int rc, char *out); /* fourni par le panneau */

/* panneau : état */
void back_refresh(bool full);
void back_tick(void);
int back_fd(void);
void back_read(void);
double back_next_timeout(void);

/* panneau : interface */
void ui_draw(cairo_t *cr, int W, int H);
void ui_click(double x, double y);
void ui_drag(double x, double y);
void ui_release(void);
void ui_scroll(double dy);
void ui_key(xkb_keysym_t sym, const char *utf8, bool ctrl);

/* kit.c : dessin HUD */
#define C_BG      0x02060b
#define C_PANEL   0x07121c
#define C_TEXT    0xcdeefc
#define C_MUTED   0x5f8ea5
#define C_OK      0x4fe0a4
#define C_ERR     0xff5470
#define C_BRIGHT  0xe9f9ff
#define HEADER_H 64.0
#define STATUS_H 40.0
#define MARGIN   24.0
double clampd(double v, double lo, double hi);
void rgba(cairo_t *cr, uint32_t c, double a);
void rrect(cairo_t *cr, double x, double y, double w, double h, double r);
void glow_stroke(cairo_t *cr, uint32_t col, double lw, double k);
double txt(cairo_t *cr, double px, int weight, double ls, uint32_t col, double a,
	   double x, double cy, int align, double maxw, const char *s);
bool hover(struct rect r);
void add_hit(struct rect r, int act, int arg, bool clipped);
void kit_clip(double top, double bot);   /* zone qui défile (pour les clics) */
void button(cairo_t *cr, struct rect r, const char *label, bool on, bool danger, int act, int arg, bool clipped);
void tag(cairo_t *cr, double *x, double cy, const char *s, uint32_t col);
void spinner(cairo_t *cr, double cx, double cy, double r);
void message(cairo_t *cr, double W, double y, const char *t1, const char *t2);
void header(cairo_t *cr, double W, const char *title, const char *sub);
void statusbar(cairo_t *cr, double W, double H, const char *hint);
int hit_at(double x, double y, int *arg); /* action sous (x, y), 0 sinon */

#endif
