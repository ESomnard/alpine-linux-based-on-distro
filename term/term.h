/*
 * plume-term — terminal HUD de Plume Linux (Wayland natif, sans Electron).
 *
 * Le décodage des séquences de contrôle (couleurs, curseur, écran
 * alternatif…) est confié à libvterm ; tout le reste est ici : fenêtre,
 * pseudo-terminaux, onglets, historique, sélection, presse-papiers, dessin.
 */
#ifndef PLUME_TERM_H
#define PLUME_TERM_H

#define _GNU_SOURCE
#include <cairo.h>
#include <pango/pangocairo.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <vterm.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#define MAX_TABS 9
#define SCROLLBACK 5000          /* lignes gardées par onglet */
#define CONT_CELL 0xFFFFFFFFu    /* deuxième moitié d'un caractère large */

/* ------------------------------ cellules ----------------------------- */

enum {
	A_BOLD = 1 << 0,
	A_ITALIC = 1 << 1,
	A_UNDER = 1 << 2,
	A_REVERSE = 1 << 3,
	A_STRIKE = 1 << 4,
	A_FG_DEF = 1 << 5,       /* couleur par défaut du thème */
	A_BG_DEF = 1 << 6,
	A_CONCEAL = 1 << 7,
};

/* cellule compacte (historique et dessin) : 16 octets */
struct cell {
	uint32_t ch;             /* 0 = vide, CONT_CELL = suite d'un large */
	uint32_t fg, bg;         /* 0xRRGGBB */
	uint8_t attrs;
	uint8_t width;
	uint8_t pad[2];
};

struct sbline {
	int cols;
	bool cont;               /* ligne coupée (suite de la précédente) */
	struct cell *cells;
};

/* ------------------------------- onglet ------------------------------ */

struct tab {
	bool used;
	int fd;                  /* maître du pseudo-terminal */
	pid_t pid;
	VTerm *vt;
	VTermScreen *screen;
	VTermState *state;
	int rows, cols;

	struct sbline sb[SCROLLBACK]; /* tampon circulaire */
	int sb_head, sb_count;
	int view;                /* lignes remontées dans l'historique (0 = en bas) */

	VTermPos cursor;
	bool cursor_visible;
	int cursor_shape;
	bool altscreen;
	int mouse_mode;
	char title[128];         /* titre fixé par le programme (OSC 0/2) */
	char label[64];          /* titre affiché sur l'onglet */
	double bell_t;
	bool dirty;
};

/* ----------------------------- sélection ----------------------------- */

/* position « virtuelle » : row < 0 = historique, 0..rows-1 = écran */
struct vpos {
	int row, col;
};

struct selection {
	bool active, dragging;
	struct vpos a, b;        /* ancre, extrémité */
	int mode;                /* 0 caractère, 1 mot, 2 ligne */
};

/* ------------------------------- tampons ----------------------------- */

struct buffer {
	struct wl_buffer *wl;
	void *data;
	size_t size;
	cairo_surface_t *cs;
	int w, h;
	bool busy;
};

struct rect {
	double x, y, w, h;
};

#include "hud.h"

/* -------------------------------- appli ------------------------------ */

struct app {
	struct wl_display *display;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct wl_seat *seat;
	struct wl_keyboard *keyboard;
	struct wl_pointer *pointer;
	struct xdg_wm_base *wm_base;
	struct wl_data_device_manager *ddm;
	struct wl_data_device *data_device;
	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *toplevel;
	struct wl_callback *frame_cb;
	struct buffer buffers[2];
	int width, height;
	bool configured, running, dirty, focused;

	/* clavier */
	struct xkb_context *xkb_ctx;
	struct xkb_keymap *keymap;
	struct xkb_state *xkb_state;
	int32_t repeat_rate, repeat_delay;
	uint32_t repeat_key;
	int repeat_fd;
	uint32_t last_serial;

	/* souris */
	struct wl_cursor_theme *cursor_theme;
	struct wl_surface *cursor_surface;
	uint32_t pointer_serial;
	const char *cursor_name;
	double px, py;
	bool btn_left;
	double last_click_t;
	int click_count;
	struct vpos last_click_pos;
	double axis_acc;

	/* presse-papiers */
	struct wl_data_source *source;
	char *clip;              /* texte que nous offrons */
	struct wl_data_offer *offer;          /* sélection proposée par les autres */
	bool offer_text;
	struct wl_data_offer *pending_offer;  /* offre en cours de description */
	bool pending_text;
	int paste_fd;
	char *paste_buf;
	size_t paste_len, paste_cap;

	/* police */
	double font_px;
	PangoFontDescription *font[4]; /* normal, gras, italique, gras italique */
	double cw, ch, ascent;   /* taille d'une cellule */

	/* mise en page (remplie au dessin) */
	struct rect r_grid, r_tabs[MAX_TABS], r_tab_close[MAX_TABS], r_new;
	double pad;

	/* onglets */
	struct tab tabs[MAX_TABS];
	int active;
	struct selection sel;

	bool hud;                /* mode poste de pilotage (panneaux autour) */

	/* options */
	char **cmd;              /* -e commande… */
	const char *workdir;
	double resized_t;        /* affiche « 120×40 » après un redimensionnement */
	double start;
};

extern struct app A;

/* le terminal a-t-il le clavier ? (en mode HUD, l'explorateur peut l'avoir) */
static inline bool term_focused(void)
{
	return A.focused && !(A.hud && H.focus);
}

/* main.c */
double now_s(void);
void request_redraw(void);
void clipboard_copy(const char *text);
void clipboard_paste(void);
bool open_tab_cmd(char **cmd, const char *dir);
void spawn_argv(char *const argv[]);
void send_to_terminal(uint32_t c, VTermModifier mod);
void send_key_to_terminal(VTermKey k, VTermModifier mod);

/* tab.c */
bool tab_open(int slot, int rows, int cols, char **cmd, const char *dir);
void tab_close(struct tab *t);
void tab_resize(struct tab *t, int rows, int cols);
void tab_read(struct tab *t);
void tab_write(struct tab *t, const char *s, size_t len);
void tab_refresh_label(struct tab *t);
bool tab_cell(struct tab *t, int vrow, int col, struct cell *out);
bool tab_line_cont(struct tab *t, int vrow);
char *tab_text(struct tab *t, struct vpos a, struct vpos b);
void tab_scroll_view(struct tab *t, int delta);
int tab_count(void);
struct tab *tab_active(void);

/* render.c */
void font_setup(double px);
void layout_grid(int W, int H, int *rows, int *cols);
void render_frame(cairo_t *cr, int W, int H);

#endif
