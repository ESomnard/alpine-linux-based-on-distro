/*
 * Logithèque Plume — installer et désinstaller des applications, à la manière
 * de GNOME Logiciels, au style HUD de Plume. Wayland natif, sans GTK.
 *
 * Sources : paquets Alpine (apk, avec le mot de passe administrateur via sudo)
 *           et Flathub (flatpak, installé pour l'utilisateur, sans mot de passe).
 */
#ifndef PLUME_LOGITHEQUE_H
#define PLUME_LOGITHEQUE_H

#define _GNU_SOURCE
#include <cairo.h>
#include <pango/pangocairo.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#define MAX_CATALOG 160
#define MAX_RESULTS 60
#define MAX_HITS 512
#define MAX_PROCS 8
#define MAX_JOBS 16

struct rect {
	double x, y, w, h;
};

/* ------------------------------ données ------------------------------ */

/* application du catalogue */
struct entry {
	char name[64], cat[32], apk[64], flatpak[96], term_cmd[48], summary[160];
};

/* paquet Alpine disponible (index complet, trié par nom) */
struct pkg {
	char *name, *version, *desc;
};

/* résultat de recherche hors catalogue */
enum kind { K_CATALOG, K_APK, K_FLATPAK };

struct result {
	enum kind kind;
	char id[96], name[96], version[48], summary[200];
};

/* élément affichable (fiche détaillée, boutons) */
struct item {
	enum kind kind;
	int idx;                 /* catalogue : index ; sinon index dans le tableau de résultats */
	struct result r;         /* copie pour K_APK / K_FLATPAK */
};

/* ------------------------------ tâches ------------------------------- */

enum job_kind {
	J_APK_ADD, J_APK_DEL, J_FP_INSTALL, J_FP_REMOVE, J_FP_REMOVE_SYS,
	J_ENABLE_FLATPAK, J_ADD_FLATHUB, J_FP_APPSTREAM,
	J_APK_UPDATE, J_APK_UPGRADE, J_FP_UPDATE,
};

struct job {
	enum job_kind kind;
	char arg[128];           /* paquet ou identifiant */
	char title[160];
	bool root;
};

/* processus en arrière-plan (tâches visibles et requêtes silencieuses) */
struct proc;
typedef void (*proc_done_fn)(struct proc *p, int status);

struct proc {
	bool used;
	pid_t pid;
	int fd;                  /* sortie standard + erreurs */
	char *out;
	size_t len, cap;
	proc_done_fn done;
	int tag;
	bool is_job;
};

/* --------------------------- interface ------------------------------- */

enum tab { T_EXPLORE, T_INSTALLED, T_UPDATES };

enum action {
	A_NONE, A_TAB, A_CATEGORY, A_DETAIL, A_BACK, A_INSTALL, A_REMOVE, A_LAUNCH,
	A_SOURCE, A_ENABLE_FLATHUB, A_CHECK_UPDATES, A_UPDATE_ALL, A_SEARCH,
	A_PW_OK, A_PW_CANCEL, A_CLEAR_SEARCH, A_PW_FIELD,
};

struct hit {
	struct rect r;
	enum action act;
	int arg;
	struct item item;
};

struct update_row {
	enum kind kind;
	char name[96], from[48], to[48];
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
	/* Wayland */
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
	bool configured, running, dirty, focused;
	struct wl_cursor_theme *cursor_theme;
	struct wl_surface *cursor_surface;
	uint32_t pointer_serial;
	const char *cursor_name;
	double px, py, axis_acc;

	/* clavier */
	struct xkb_context *xkb_ctx;
	struct xkb_keymap *keymap;
	struct xkb_state *xkb_state;
	int32_t repeat_rate, repeat_delay;
	uint32_t repeat_key;
	int repeat_fd;

	/* données */
	struct entry catalog[MAX_CATALOG];
	int ncatalog;
	struct pkg *pkgs;        /* index Alpine */
	int npkgs;
	bool index_ready;
	char **installed;        /* paquets Alpine installés (triés) */
	int ninstalled;
	char **world;            /* paquets demandés explicitement */
	int nworld;
	struct result fp_installed[MAX_RESULTS * 2];
	int nfp_installed;
	bool flatpak_present, flathub_ready;

	/* recherche */
	char query[128];
	char fp_query[128];      /* recherche Flathub en cours / faite */
	double query_t;
	struct result apk_results[MAX_RESULTS];
	int napk_results;
	struct result fp_results[MAX_RESULTS];
	int nfp_results;
	bool fp_searching;

	/* mises à jour */
	struct update_row updates[256];
	int nupdates;
	bool updates_checked;

	/* interface */
	enum tab tab;
	char category[32];       /* "" = toutes */
	bool detail_open;
	struct item detail;
	int detail_source;       /* 0 Alpine, 1 Flathub (quand les deux existent) */
	double scroll, content_h;
	struct hit hits[MAX_HITS];
	int nhits;
	char toast[200];
	bool toast_err;
	double toast_t;

	/* mot de passe administrateur */
	bool pw_open;
	char pw[128];
	char pw_cached[128];
	double pw_cached_t;
	bool pw_error;

	/* tâches (une à la fois, les autres attendent) */
	struct job jobs[MAX_JOBS];
	int njobs;
	bool job_running;
	struct job current;
	char job_line[200];
	double job_progress;     /* 0..1, ou -1 si inconnu */
	double job_start;

	struct proc procs[MAX_PROCS];
	double start;
};

extern struct app A;

/* main.c */
double now_s(void);
void request_redraw(void);
void set_toast(bool err, const char *fmt, ...);
void spawn_detached(const char *cmdline);

/* proc.c */
struct proc *proc_run(char *const argv[], const char *stdin_data, proc_done_fn done, int tag, bool is_job);
void proc_poll_fds(struct pollfd *fds, int *nfds, int base);
void proc_handle(struct pollfd *fds, int nfds);
void proc_reap(void);

/* backend.c */
void backend_init(void);
void load_installed(void);
void refresh_flatpak(void);
void search_update(void);
void tick_search(void);
const struct pkg *pkg_find(const char *name);
bool apk_installed(const char *name);
bool apk_in_world(const char *name);
bool fp_is_installed(const char *id, bool *user);
void queue_job(enum job_kind kind, const char *arg);
void jobs_pump(void);
void password_submitted(void);
void password_cancelled(void);
void check_updates(void);
void update_all(void);
void enable_flathub(void);
void launch_item(const struct item *it, int source);
void install_item(const struct item *it, int source);
void remove_item(const struct item *it, int source);
bool item_has_apk(const struct item *it, const char **name);
bool item_has_flatpak(const struct item *it, const char **id);
bool item_installed(const struct item *it, int source);
void item_title(const struct item *it, char *name, size_t ncap, char *summary, size_t scap);

/* ui.c */
void ui_draw(cairo_t *cr, int W, int H);
void ui_click(double x, double y);
void ui_scroll(double dy);
void ui_key(xkb_keysym_t sym, const char *text, bool ctrl);

#endif
