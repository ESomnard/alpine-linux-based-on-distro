/*
 * plume-term — mode HUD (plume-term --hud, ou Ctrl+Maj+H) :
 * le terminal au centre, entouré de panneaux à la façon d'un poste de pilotage.
 *
 *   gauche : SYSTÈME (machine, CPU et cœurs, RAM, swap, température, processus)
 *   droite : RÉSEAU (interface, IP, passerelle, wifi, connexions, débit) et STOCKAGE
 *   bas    : FICHIERS (explorateur : raccourcis, fil d'Ariane, recherche ; suit le
 *            dossier du terminal) et CLAVIER (s'allume, cliquable)
 */
#ifndef PLUME_TERM_HUD_H
#define PLUME_TERM_HUD_H

#include <cairo.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>
#include <xkbcommon/xkbcommon.h>

#define HIST 90
#define MAX_CORES 64
#define MAX_PROCS 8
#define MAX_KEYS 80
#define MAX_FROWS 48
#define MAX_PLACES 10
#define MAX_CRUMBS 24
#define MAX_MOUNTS 4

/* ------------------------------ mesures ------------------------------ */

struct proc_top {
	char name[32];
	double cpu;
	double mem_mb;
};

struct perf {
	int cpu;
	int ncores;
	int core[MAX_CORES];
	double cpu_hist[HIST], ram_hist[HIST], rx_hist[HIST], tx_hist[HIST];
	double ram_used_gb, ram_total_gb;
	double swap_used_mb, swap_total_mb;
	double disk_used_gb, disk_total_gb;
	double rx_rate, tx_rate;
	double temp;
	double load1;
	int nprocs;
	long uptime;
	struct proc_top top[MAX_PROCS];
	int ntop;
	unsigned long long prev_total[MAX_CORES + 1], prev_idle[MAX_CORES + 1];
	unsigned long long prev_rx, prev_tx;
	double prev_net_t;
};

struct mount_use {
	char path[64];
	double used_gb, total_gb;
};

struct sysinfo_plume {
	char os[64], kernel[64], cpu_model[64];
	int battery;              /* % ou -1 */
	bool charging;
	/* réseau */
	char iface[32], ipv4[48], gateway[48];
	bool online;
	bool wireless;
	int wifi_quality;         /* % ou -1 */
	int tcp_established;
	unsigned long long rx_total, tx_total;
	struct mount_use mounts[MAX_MOUNTS];
	int nmounts;
};

/* ------------------------------ fichiers ----------------------------- */

struct fentry {
	char *name;
	bool dir, exec, hidden;
	off_t size;
	time_t mtime;
};

struct place {
	char glyph[8], label[32], path[1024];
};

/* ------------------------------ clavier ------------------------------ */

struct key {
	uint32_t code;            /* code evdev */
	double x, y, w;           /* en unités de touche */
	char label[16], shift_label[16];
	bool special;
	double heat;              /* 1 = vient d'être pressée, décroît */
	bool down;
};

/* ------------------------------- état -------------------------------- */

struct hud {
	struct perf perf;
	struct sysinfo_plume sys;

	/* fichiers : explorateur qui suit le dossier du terminal */
	char cwd[4096];          /* dossier affiché */
	char term_cwd[4096];     /* dernier dossier vu du terminal */
	struct fentry *ent;
	int nent, cap;
	int *view, nview;        /* entrées filtrées (recherche, cachés) */
	int sel, scroll;
	bool snap;               /* ramener la sélection dans la vue */
	char query[256];
	bool show_hidden;
	bool focus;              /* le clavier va à l'explorateur (sinon au terminal) */
	struct place places[MAX_PLACES];
	int nplaces;
	struct rect r_files, r_list, r_search, r_hidden, r_cdhere;
	struct rect r_places[MAX_PLACES], r_frows[MAX_FROWS], r_crumbs[MAX_CRUMBS];
	char crumb_path[MAX_CRUMBS][1024];
	int frow_idx[MAX_FROWS], nfrows, ncrumbs;
	double last_click_t;
	int last_click_idx;
	char status[160];
	double status_t;
	double intro_t;          /* animation d'ouverture */

	/* clavier à l'écran */
	struct key keys[MAX_KEYS];
	int nkeys;
	struct rect r_keys[MAX_KEYS], r_kbd;
	char layout_name[48];
	bool sticky_shift, sticky_ctrl;
	int keystrokes;
	char last_key[16];

	/* cache des panneaux (redessinés une fois par seconde) */
	cairo_surface_t *cache;
	int cache_w, cache_h;
	bool cache_dirty;
};

extern struct hud H;

/* sysinfo.c */
void perf_init(struct perf *p);
void perf_update(struct perf *p);
void sys_update(struct sysinfo_plume *s);

/* hud.c */
void hud_init(void);
void hud_tick(void);
struct rect hud_term_area(int W, int H);
void hud_draw_back(cairo_t *cr, int W, int H);
void hud_draw_front(cairo_t *cr, int W, int H);
bool hud_click(double x, double y, uint32_t button);
bool hud_scroll(double x, double y, int dir);
bool hud_hover_changed(double ox, double oy, double x, double y);
void hud_keymap_changed(void);
void hud_key_light(uint32_t code, bool down);
void hud_decay(double dt);
void hud_key(xkb_keysym_t sym, const char *text, bool ctrl);
void hud_focus_files(bool on);
bool hud_animating(void);

#endif
