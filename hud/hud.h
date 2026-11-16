/*
 * plume-hud — écran de connexion (greeter) et de verrouillage HUD de Plume Linux.
 *
 * Réécriture native (C + Wayland + cairo/pango) du projet « HUD Lockscreen »
 * (Electron) d'ESomnard, en gardant ses fonctionnalités :
 *   - 5 panneaux : opérateur/système, horloge + déverrouillage, énergie/réseau,
 *     calendrier, journal d'événements ;
 *   - CPU / RAM en direct, uptime, « verrouillé depuis » ;
 *   - blocage après N essais pendant N secondes ;
 *   - animations (apparition des panneaux, anneaux, voyant, tremblement, sortie) ;
 *   - tous les écrans, mode test.
 *
 * Modes :
 *   --lock     écran de verrouillage sécurisé (protocole ext-session-lock-v1)
 *   --greeter  écran de connexion pour greetd (lancé dans cage)
 *   --dev      fenêtre de test, Échap pour quitter
 */
#ifndef PLUME_HUD_H
#define PLUME_HUD_H

#define _GNU_SOURCE
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <cairo.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#define HUD_MAX_ATTEMPTS 5
#define HUD_LOCKOUT_SECONDS 30
#define HUD_LOG_MAX 8
#define HUD_FPS 20

enum hud_mode { MODE_LOCK, MODE_GREETER, MODE_DEV };
enum msg_kind { MSG_NORMAL = 0, MSG_ERR = 1, MSG_OK = 2 };

struct rect { double x, y, w, h; };

struct buffer {
	struct wl_buffer *wl;
	void *data;
	size_t size;
	cairo_surface_t *cs;
	int w, h;
	bool busy;
};

/* Une surface affichée : un écran (mode verrou) ou la fenêtre (greeter / test). */
struct output {
	struct wl_list link;
	struct wl_output *wl_output;
	uint32_t global_name;
	int32_t scale;
	struct wl_surface *surface;
	struct ext_session_lock_surface_v1 *lock_surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;
	int width, height;          /* taille logique */
	bool configured;
	struct buffer buffers[2];
	struct wl_callback *frame_cb; /* image envoyée, pas encore affichée */
	cairo_surface_t *bg_cache;  /* fond + cadres des panneaux */
	int bg_w, bg_h, bg_scale;
	/* zones cliquables (coordonnées logiques), remplies au dessin */
	struct rect field_rect[2];  /* 0 = utilisateur (greeter), 1 = mot de passe */
	struct rect button_rect;
};

struct log_entry {
	char ts[9];
	char text[160];
	int kind;
	double born;
};

struct sysinfo_hud {
	char host[64], os[96], arch[72];
	int cores;
	int cpu, ram;               /* % */
	double cpu_disp, ram_disp, bat_disp;  /* valeurs animées */
	long uptime;
	int bat;                    /* % ou -1 si pas de batterie */
	bool charging;
	bool online;
	bool first;
	unsigned long long prev_idle, prev_total;
};

struct hud {
	enum hud_mode mode;
	bool daemonize;

	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct wl_seat *seat;
	struct wl_keyboard *keyboard;
	struct wl_pointer *pointer;
	struct xdg_wm_base *wm_base;
	struct ext_session_lock_manager_v1 *lock_manager;
	struct ext_session_lock_v1 *lock;
	bool locked;
	struct wl_list outputs;

	struct xkb_context *xkb_ctx;
	struct xkb_keymap *keymap;
	struct xkb_state *xkb_state;
	int32_t repeat_rate, repeat_delay;
	uint32_t repeat_key;
	xkb_keysym_t repeat_sym;
	char repeat_utf8[8];
	int repeat_fd;

	struct wl_cursor_theme *cursor_theme;
	struct wl_surface *cursor_surface;
	struct output *pointer_output;
	double px, py;

	/* interface */
	char user[64];
	char pw[256];
	int focus;                  /* 0 = utilisateur, 1 = mot de passe */
	char msg[160];
	int msg_kind;
	struct log_entry log[HUD_LOG_MAX];
	int log_n;
	int failed;
	double locked_until;        /* blocage de sécurité (horloge monotone) */
	double start;               /* démarrage (animation d'apparition) */
	long locked_at;             /* heure murale du verrouillage */
	double shake_start;
	double unlock_start;
	bool unlocking;
	bool authenticating;
	pid_t auth_pid;
	int auth_fd;

	struct sysinfo_hud sys;
	double last_sys;

	bool running;
	int exit_code;
};

/* main.c */
double now_s(void);
void hud_log(struct hud *h, int kind, const char *fmt, ...);
void hud_set_msg(struct hud *h, int kind, const char *fmt, ...);
void hud_auth_done(struct hud *h, int result, const char *desc);

/* sys.c */
void sys_init(struct hud *h);
void sys_update(struct hud *h);
void sys_animate(struct hud *h, double dt);
void first_human_user(char *out, size_t cap);

/* draw.c */
void draw_frame(struct hud *h, struct output *o, cairo_t *cr, int w, int h_px);

/* auth.c */
int auth_start(struct hud *h);
void auth_finish(struct hud *h);

#endif
