/*
 * Souris virtuelle pour les tests (protocole wlr-virtual-pointer).
 * Contrairement à wlrctl, elle attend que les applis se soient raccordées
 * à la nouvelle souris avant de cliquer.
 *
 *   vpointer W H click X Y [left|right|middle]
 *   vpointer W H drag X1 Y1 X2 Y2
 *   vpointer W H move X Y
 */
#define _POSIX_C_SOURCE 200809L
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-client.h>

#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"

static struct wl_seat *seat;
static struct zwlr_virtual_pointer_manager_v1 *mgr;

static void global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t ver)
{
	(void)d; (void)ver;
	if (!strcmp(iface, wl_seat_interface.name) && !seat)
		seat = wl_registry_bind(r, name, &wl_seat_interface, 1);
	else if (!strcmp(iface, zwlr_virtual_pointer_manager_v1_interface.name))
		mgr = wl_registry_bind(r, name, &zwlr_virtual_pointer_manager_v1_interface, 1);
}
static void global_remove(void *d, struct wl_registry *r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg = { global, global_remove };

static void pause_ms(long ms)
{
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
	nanosleep(&ts, NULL);
}

static uint32_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int main(int argc, char **argv)
{
	if (argc < 6) {
		fprintf(stderr, "usage : vpointer W H click X Y [bouton] | drag X1 Y1 X2 Y2 | move X Y\n");
		return 2;
	}
	struct wl_display *dpy = wl_display_connect(NULL);
	if (!dpy)
		return 1;
	struct wl_registry *r = wl_display_get_registry(dpy);
	wl_registry_add_listener(r, &reg, NULL);
	wl_display_roundtrip(dpy);
	if (!mgr) {
		fprintf(stderr, "vpointer : protocole absent\n");
		return 1;
	}
	uint32_t W = atoi(argv[1]), H = atoi(argv[2]);
	struct zwlr_virtual_pointer_v1 *p = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(mgr, seat);
	wl_display_roundtrip(dpy);
	pause_ms(400); /* le temps que les applis récupèrent la souris */
#define MOVE(x, y) do { zwlr_virtual_pointer_v1_motion_absolute(p, now_ms(), (x), (y), W, H); \
	zwlr_virtual_pointer_v1_frame(p); wl_display_roundtrip(dpy); pause_ms(120); } while (0)
#define BTN(b, st) do { zwlr_virtual_pointer_v1_button(p, now_ms(), (b), (st)); \
	zwlr_virtual_pointer_v1_frame(p); wl_display_roundtrip(dpy); pause_ms(120); } while (0)
	const char *cmd = argv[3];
	if (!strcmp(cmd, "move")) {
		MOVE(atoi(argv[4]), atoi(argv[5]));
	} else if (!strcmp(cmd, "click")) {
		uint32_t b = BTN_LEFT;
		if (argc > 6 && !strcmp(argv[6], "right")) b = BTN_RIGHT;
		if (argc > 6 && !strcmp(argv[6], "middle")) b = BTN_MIDDLE;
		MOVE(atoi(argv[4]), atoi(argv[5]));
		BTN(b, WL_POINTER_BUTTON_STATE_PRESSED);
		BTN(b, WL_POINTER_BUTTON_STATE_RELEASED);
	} else if (!strcmp(cmd, "drag") && argc >= 8) {
		int x1 = atoi(argv[4]), y1 = atoi(argv[5]), x2 = atoi(argv[6]), y2 = atoi(argv[7]);
		MOVE(x1, y1);
		BTN(BTN_LEFT, WL_POINTER_BUTTON_STATE_PRESSED);
		for (int i = 1; i <= 8; i++)
			MOVE(x1 + (x2 - x1) * i / 8, y1 + (y2 - y1) * i / 8);
		BTN(BTN_LEFT, WL_POINTER_BUTTON_STATE_RELEASED);
	}
	pause_ms(200);
	zwlr_virtual_pointer_v1_destroy(p);
	wl_display_roundtrip(dpy);
	wl_display_disconnect(dpy);
	return 0;
}
