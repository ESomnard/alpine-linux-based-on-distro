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
#ifdef PLUME_VOID
#define DOCK_TIP_H 92   /* place au-dessus du dock : icônes agrandies (loupe) + info-bulle */
#define DOCK_MAG 0.55   /* loupe : +55 % pour l'icône sous la souris, moins pour ses voisines */
#else
#define DOCK_TIP_H 60
#endif
#define DOCK_MARGIN 10
#define DOCK_SURF_H (DOCK_PANEL_H + DOCK_TIP_H + DOCK_MARGIN)
#define MAX_WS 16
#ifdef PLUME_VOID
#define MAX_APPS 28 /* épinglées + ouvertes (zone du milieu, comme sur Mac) */
#else
#define MAX_APPS 16
#endif
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
	struct rect hit_logo, hit_ws[MAX_WS], hit_kb, hit_net, hit_vol, hit_power, hit_maj, hit_bt;
	struct rect hit_app[MAX_APPS];
	struct rect dock_panel;
#ifdef PLUME_VOID
	struct rect hit_clock;  /* heure et date : notifications (comme sur Mac) */
	double panel_w_drawn;   /* largeur du dock au dernier dessin (loupe : zone cliquable à refaire) */
#endif
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
#ifdef PLUME_VOID
	bool extra;             /* appli ouverte mais pas épinglée (zone du milieu) */
	bool trash;             /* la corbeille (au bout du dock) */
	long mini_con;          /* fenêtre réduite (pastille jaune) : son numéro sway, sinon 0 */
	double bounce_t;        /* rebond pendant le lancement : heure du clic (0 = pas de rebond) */
#endif
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
#ifdef PLUME_VOID
	struct app cfg[MAX_APPS]; /* le dock tel qu'écrit dans dock.conf (épinglées + ⏻) */
	int ncfg;
	/* loupe façon Mac */
	double mag;               /* 0..1, animé quand la souris entre / sort du dock */
	double mag_x;             /* position de la souris le long du dock */
	struct output *mag_out;
	/* fenêtres réduites (dans le scratchpad de sway) */
	struct { long id; char app_id[64]; char title[96]; } mini[8];
	int nmini;
	int ntrash;               /* éléments dans la corbeille */
	/* glisser une icône du dock : réordonner, ou la tirer dehors pour la retirer */
	int press_app;            /* -1 : aucun bouton enfoncé sur une appli */
	double press_x, press_y;
	int drag_app;             /* -1 : pas de glisser */
	bool drag_out;
	struct output *drag_outp;
	/* fichiers glissés sur le dock (ouvrir avec une appli, jeter à la corbeille) */
	struct wl_data_device_manager *ddm;
	struct wl_data_device *dd;
	struct wl_data_offer *offer, *drop_offer;
	bool offer_uri;
	int drop_app, drop_fd;
	char *drop_buf;
	size_t drop_len;
#endif

	/* système */
	int cpu, ram, bat, vol;
	int maj;            /* mises à jour disponibles (plume-maj) */
	char maj_version[16]; /* nouvelle version d'Alpine publiée, ex. « 3.25 » */
	bool charging, online, wifi, muted, bt;
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

#ifdef PLUME_VOID
/* menu.c : menu du clic droit sur une appli du dock (comme sur Mac) */
#define MENU_MAX 16
#define MENU_WIN_MAX 8
#define MENU_EXTRA 380 /* hauteur ajoutée au dock quand le menu est ouvert */
enum menu_act { M_FOCUS, M_OPEN, M_NEW, M_HIDE, M_SHOW, M_REMOVE, M_KEEP, M_QUIT, M_EMPTY_TRASH };
struct menu_item {
	char label[160];
	enum menu_act act;
	long arg;               /* M_FOCUS : con_id sway */
	bool check, sep_before;
	struct rect r;          /* zone cliquable (calculée au dessin) */
};
struct menu {
	bool open;
	struct output *out;
	int app, n, nwin, hover;
	struct menu_item it[MENU_MAX];
	long hidden[MENU_MAX];  /* fenêtres réduites (scratchpad) */
	int nhidden;
};
extern struct menu M;
void menu_open(struct output *o, int app);
void menu_close(void);
void menu_click(double x, double y);
void menu_motion(double x, double y);
void draw_menu(struct output *o, cairo_t *cr, int W, int H);
/* sys.c : dock = épinglées | ouvertes non épinglées | ⏻ (refait à chaque changement de fenêtres) */
void dock_compose(void);
void desktop_info(const char *id, char *name, char *cmd, char *icon);
/* menu.c : réécrire le dock (glisser une icône pour la déplacer ou la retirer) */
void dock_move(const char *label, const char *before_label);
void dock_remove_label(const char *label);
/* draw.c : où irait une icône glissée (index de l'appli devant laquelle l'insérer, -1 : en dernier) */
int dock_insert_index(struct output *o);
/* main.c : zones cliquables du dock à refaire (le nombre d'applis a changé) */
void dock_relayout(void);
#endif

/* main.c */
void mark_dirty(enum surf_kind kind);
void spawn(const char *cmd);
double now_s(void);

#endif
