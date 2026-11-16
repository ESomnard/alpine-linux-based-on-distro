/*
 * plume-shell — barre du haut + dock de Plume Linux, dans le style HUD de plume-hud.
 *
 *   barre : ◆ PLUME (lanceur) · bureaux · fenêtre active · clavier · CPU (graphe) · RAM ·
 *           réseau · son · batterie · heure · ⏻
 *   dock  : applications (glyphes HUD), point lumineux sous les applis ouvertes,
 *           agrandissement au survol, info-bulle, clic = lancer ou afficher
 *
 * Lancé par sway via `swaybar_command` (une barre + un dock par écran).
 */
#ifndef PLUME_SHELL_H
#define PLUME_SHELL_H

#define _GNU_SOURCE
#include <cairo.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wayland-client.h>

#define BAR_H 32
#define DOCK_TILE 48
#define DOCK_GAP 10
#define DOCK_PAD 14
#define DOCK_PANEL_H (DOCK_TILE + 2 * DOCK_PAD + 6)
#define DOCK_TIP_H 60
#define DOCK_MARGIN 10
#define DOCK_SURF_H (DOCK_PANEL_H + DOCK_TIP_H + DOCK_MARGIN)
#define MAX_WS 16
#define MAX_APPS 16
#define MAX_RUNNING 64
#define CPU_HIST 24

/* ------------------------------ JSON -------------------------------- */
enum jtype { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ };
struct json {
	enum jtype t;
	char *key;
	char *s;
	double n;
	bool b;
	struct json *child, *next;
};
struct json *json_parse(const char *s);
void json_free(struct json *j);
struct json *json_get(struct json *obj, const char *key);
const char *json_str(struct json *obj, const char *key);
bool json_bool(struct json *obj, const char *key);
double json_num(struct json *obj, const char *key);

/* ------------------------------ état -------------------------------- */
struct rect { double x, y, w, h; };

struct buffer {
	struct wl_buffer *wl;
	void *data;
	size_t size;
	cairo_surface_t *cs;
	int w, h;
	bool busy;
};

enum surf_kind { SURF_BAR, SURF_DOCK };

struct surf {
	enum surf_kind kind;
	struct output *out;
	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer;
	int width, height;
	bool configured, dirty;
	struct buffer buffers[2];
};

struct output {
	struct wl_list link;
	struct wl_output *wl_output;
	uint32_t global_name;
	int32_t scale;
	char name[32];
	struct surf bar, dock;
	/* zones cliquables (coordonnées logiques de chaque surface) */
	struct rect hit_logo, hit_ws[MAX_WS], hit_kb, hit_net, hit_vol, hit_power;
	struct rect hit_app[MAX_APPS];
	struct rect dock_panel;
};

struct workspace {
	int num;
	char name[32];
	char output[32];
	bool focused, visible, urgent;
};

struct app {
	char glyph[16];
	char label[48];
	char cmd[256];
	char app_id[64];
	bool power;             /* icône ⏻ dessinée en vectoriel */
	bool separator_before;
	double hover;           /* 0..1 animé */
};

struct shell {
	struct wl_display *display;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct wl_seat *seat;
	struct wl_pointer *pointer;
	struct zwlr_layer_shell_v1 *layer_shell;
	struct wl_list outputs;

	struct surf *ptr_surf;
	double px, py;
	int hover_app;          /* -1 = aucun */

	int ipc_ev, ipc_cmd;    /* sockets sway : événements / commandes */

	struct workspace ws[MAX_WS];
	int nws;
	char title[256];
	char focused_app[64];
	char running[MAX_RUNNING][64];
	int nrunning;
	char kb[24];

	struct app apps[MAX_APPS];
	int napps;

	/* système */
	int cpu, ram, bat, vol;
	bool charging, online, wifi, muted;
	int cpu_hist[CPU_HIST];
	unsigned long long prev_idle, prev_total;

	bool running_loop;
};

extern struct shell S;

/* ipc.c */
int ipc_connect(void);
bool ipc_send(int fd, uint32_t type, const char *payload);
char *ipc_recv(int fd, uint32_t *type);
char *ipc_query(uint32_t type, const char *payload);
void ipc_command(const char *fmt, ...);
void ipc_refresh_workspaces(void);
void ipc_refresh_tree(void);
void ipc_refresh_inputs(void);

/* sys.c */
void sys_update(void);
void apps_load(void);
bool app_is_running(const struct app *a);

/* draw.c */
void draw_bar(struct output *o, cairo_t *cr, int w, int h);
void draw_dock(struct output *o, cairo_t *cr, int w, int h);
void dock_layout(struct output *o, int w, int h, double *panel_x, double *panel_w);

/* main.c */
void mark_dirty(enum surf_kind kind);
double now_s(void);

#endif
