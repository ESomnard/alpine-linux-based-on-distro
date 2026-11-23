/*
 * Plume — pastilles de fenêtre dessinées dans l'appli (comme Firefox dessine ses boutons) :
 *   rouge = fermer · jaune = réduire (rangée de côté) · verte = plein écran (agrandir / revenir)
 * Au survol, les signes × − ⤢ apparaissent. Fichier identique dans chaque appli Plume.
 *
 * Actif seulement si compilé avec -DPLUME_PASTILLES (paquets de Plume Void, void/depot-inside.sh) ;
 * sans cette option, tout est vide et l'appli est inchangée (version Alpine).
 *
 *   pf_draw(cr, x, cy, px, py)   dessine les pastilles (x = bord gauche, cy = milieu vertical,
 *                                px/py = position de la souris, pour le survol)
 *   pf_clic(px, py)              0 = pas sur une pastille ; 1 = fermer (à faire par l'appli) ;
 *                                2 = réduite, 3 = plein écran (faits ici, via sway)
 *   pf_entete_clic(x, y)         clic dans un endroit vide de l'en-tête : au 2e clic rapproché
 *                                (double-clic, comme sur Mac), plein écran / taille normale ;
 *                                bouton tenu puis souris bougée : la fenêtre suit (voir ci-dessous)
 *   pf_serie = serial            dans p_button (main.c), avant ui_click
 *   pf_mouvement(px, py)         dans p_motion : 1 = commencer à déplacer la fenêtre
 *                                (xdg_toplevel_move(toplevel, seat, pf_serie))
 *   pf_relache()                 dans p_button, au relâchement
 *   pf_deja_ouvert("app_id")     au lancement : si l'appli est déjà ouverte (même réduite),
 *                                la montre et renvoie 1 (l'appli quitte alors sans fenêtre)
 *   PF_LARGEUR                   place à laisser à gauche de l'en-tête
 */
#ifndef PLUME_FENETRE_H
#define PLUME_FENETRE_H

#include <cairo.h>

#ifdef PLUME_PASTILLES

#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define PF_R 6.5
#define PF_PAS 20.0
#define PF_LARGEUR (2 * PF_R + 2 * PF_PAS + 18)

static double pf_x0 __attribute__((unused)) = -1e9, pf_cy __attribute__((unused)) = -1e9;

/* partagés entre ui.c (clic) et main.c (souris) : « weak » = une seule variable pour tout le programme */
__attribute__((weak)) int pf_appui;        /* bouton tenu dans le vide de l'en-tête */
__attribute__((weak)) double pf_ax, pf_ay; /* où il a été enfoncé */
__attribute__((weak)) uint32_t pf_serie;   /* numéro du dernier clic (pour xdg_toplevel_move) */

static inline void pf_rgb(cairo_t *cr, uint32_t c)
{
	cairo_set_source_rgb(cr, ((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0, (c & 255) / 255.0);
}

static inline int pf_index(double px, double py)
{
	for (int i = 0; i < 3; i++) {
		double dx = px - (pf_x0 + PF_R + i * PF_PAS), dy = py - pf_cy;
		if (dx * dx + dy * dy <= (PF_R + 3) * (PF_R + 3))
			return i + 1;
	}
	return 0;
}

static inline void pf_draw(cairo_t *cr, double x, double cy, double px, double py)
{
	static const uint32_t fond[3] = { 0xff5f57, 0xfebc2e, 0x28c840 };
	static const uint32_t bord[3] = { 0xe0443e, 0xdea123, 0x1aab29 };
	static const uint32_t signe[3] = { 0x4d0000, 0x995700, 0x006500 };
	pf_x0 = x;
	pf_cy = cy;
	bool survol = px >= x - 4 && px <= x + 2 * PF_R + 2 * PF_PAS + 4 && py >= cy - PF_R - 4 && py <= cy + PF_R + 4;
	cairo_save(cr);
	cairo_new_path(cr);
	for (int i = 0; i < 3; i++) {
		double cx = x + PF_R + i * PF_PAS;
		cairo_arc(cr, cx, cy, PF_R, 0, 2 * 3.14159265358979);
		pf_rgb(cr, fond[i]);
		cairo_fill_preserve(cr);
		pf_rgb(cr, bord[i]);
		cairo_set_line_width(cr, 1);
		cairo_stroke(cr);
		if (!survol)
			continue;
		pf_rgb(cr, signe[i]);
		cairo_set_line_width(cr, 1.4);
		cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
		if (i == 0) {          /* × */
			double d = 2.6;
			cairo_move_to(cr, cx - d, cy - d);
			cairo_line_to(cr, cx + d, cy + d);
			cairo_move_to(cr, cx + d, cy - d);
			cairo_line_to(cr, cx - d, cy + d);
			cairo_stroke(cr);
		} else if (i == 1) {   /* − */
			cairo_move_to(cr, cx - 3.3, cy);
			cairo_line_to(cr, cx + 3.3, cy);
			cairo_stroke(cr);
		} else {               /* ⤢ : deux petits triangles vers les coins */
			cairo_move_to(cr, cx - 3.2, cy + 3.2);
			cairo_line_to(cr, cx - 3.2, cy - 1.0);
			cairo_line_to(cr, cx + 1.0, cy + 3.2);
			cairo_close_path(cr);
			cairo_move_to(cr, cx + 3.2, cy - 3.2);
			cairo_line_to(cr, cx + 3.2, cy + 1.0);
			cairo_line_to(cr, cx - 1.0, cy - 3.2);
			cairo_close_path(cr);
			cairo_fill(cr);
		}
	}
	cairo_restore(cr);
}

/* swaymsg ARGS… sans attendre la fin (double fork : pas de processus zombie) ; renvoie son code si attendre */
static inline int pf_swaymsg(bool attendre, const char *cmd)
{
	pid_t p = fork();
	if (p < 0)
		return -1;
	if (p == 0) {
		if (!attendre && fork() != 0)
			_exit(0);
		int nul = open("/dev/null", O_WRONLY);
		if (nul >= 0) {
			dup2(nul, 1);
			dup2(nul, 2);
		}
		execlp("swaymsg", "swaymsg", "-q", "--", cmd, (char *)NULL);
		_exit(127);
	}
	int st = 0;
	if (waitpid(p, &st, 0) < 0)
		return -1;
	return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static inline int pf_clic(double px, double py)
{
	int i = pf_index(px, py);
	char c[96];
	if (i == 2) {
		snprintf(c, sizeof c, "[pid=%d] move scratchpad", (int)getpid());
		pf_swaymsg(false, c);
	} else if (i == 3) {
		snprintf(c, sizeof c, "[pid=%d] fullscreen toggle", (int)getpid());
		pf_swaymsg(false, c);
	}
	return i;
}

static inline void pf_entete_clic(double px, double py)
{
	pf_appui = 1;
	pf_ax = px;
	pf_ay = py;
	static double avant;
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	double t = ts.tv_sec + ts.tv_nsec / 1e9;
	if (t - avant > 0.45) { /* 1er clic : on attend le 2e */
		avant = t;
		return;
	}
	avant = 0;
	char c[96];
	snprintf(c, sizeof c, "[pid=%d] fullscreen toggle", (int)getpid());
	pf_swaymsg(false, c);
}

static inline void pf_relache(void)
{
	pf_appui = 0;
}

/* souris bougée de plus de 5 px, bouton tenu dans l'en-tête : on déplace la fenêtre.
 * Une fenêtre rangée en mosaïque devient d'abord libre (flottante), comme sur Mac. */
static inline int pf_mouvement(double px, double py)
{
	if (!pf_appui || (px - pf_ax) * (px - pf_ax) + (py - pf_ay) * (py - pf_ay) < 25)
		return 0;
	pf_appui = 0;
	char c[96];
	snprintf(c, sizeof c, "[pid=%d tiling] floating enable", (int)getpid());
	pf_swaymsg(true, c);
	return 1;
}

static inline int pf_deja_ouvert(const char *app_id)
{
	char c[128];
	snprintf(c, sizeof c, "[app_id=\"^%s$\"] focus", app_id);
	return pf_swaymsg(true, c) == 0;
}

#else /* version sans pastilles */

#define PF_LARGEUR 0.0
static inline void pf_draw(cairo_t *cr, double x, double cy, double px, double py)
{
	(void)cr; (void)x; (void)cy; (void)px; (void)py;
}
static inline int pf_clic(double px, double py)
{
	(void)px; (void)py;
	return 0;
}
#define pf_serie pf_serie_inutile
static unsigned pf_serie_inutile __attribute__((unused));
static inline void pf_entete_clic(double px, double py)
{
	(void)px; (void)py;
}
static inline void pf_relache(void)
{
}
static inline int pf_mouvement(double px, double py)
{
	(void)px; (void)py;
	return 0;
}
static inline int pf_deja_ouvert(const char *app_id)
{
	(void)app_id;
	return 0;
}

#endif
#endif
