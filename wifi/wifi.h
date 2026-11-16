/*
 * plume-wifi — panneau wifi de Plume, au style HUD (remplace iwgtk : 0 dépendance
 * en plus, il parle à iwd par iwctl).
 *
 * Réseaux visibles avec leur signal, connexion (mot de passe demandé si
 * besoin), déconnexion, oubli d'un réseau connu, wifi activé / désactivé.
 *
 *   plume-wifi            ouvre le panneau (clic sur le réseau dans la barre)
 */
#ifndef PLUME_WIFI_H
#define PLUME_WIFI_H

#define _GNU_SOURCE
#include <cairo.h>
#include <pango/pangocairo.h>
#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#define MAX_NETS 64
#define MAX_HITS 200

struct rect {
	double x, y, w, h;
};

struct net {
	char name[128];
	char sec[16];  /* psk, open, 8021x, wep */
	int dbm;
	bool connected, known;
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

enum { A_NET = 1, A_CONNECT, A_DISCONNECT, A_FORGET, A_POWER, A_SCAN, A_SHOWPASS, A_FIELD };

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

	/* état du wifi */
	bool no_iwctl;           /* iwd / iwctl absent */
	bool have_dev, powered;
	char dev[32];
	char state[32];          /* connected, disconnected, connecting… */
	char cur[128];           /* réseau connecté */
	char ip[64];
	struct net nets[MAX_NETS];
	int nnets;
	bool scanning;           /* recherche en cours */
	bool busy;               /* connexion / déconnexion en cours */
	bool loaded;             /* première liste reçue */

	/* interface */
	int sel;                 /* réseau déplié (-1 : aucun) */
	bool ask_pass, show_pass;
	char pass[128];
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

/* iwd.c */
int iwd_fd(void);             /* tube du iwctl en cours (-1 : aucun) */
void iwd_read(void);          /* à appeler quand iwd_fd() est lisible */
void iwd_tick(void);          /* lance la commande suivante, actualise de temps en temps */
double iwd_next_timeout(void);
void iwd_refresh(bool scan);
void iwd_connect(const char *ssid, const char *pass);
void iwd_disconnect(void);
void iwd_forget(const char *ssid);
void iwd_power(bool on);

/* ui.c */
void ui_draw(cairo_t *cr, int W, int H);
void ui_click(double x, double y);
void ui_scroll(double dy);
void ui_key(xkb_keysym_t sym, const char *utf8, bool ctrl);

#endif
