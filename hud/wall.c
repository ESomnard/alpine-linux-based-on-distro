/*
 * plume-wall — génère le fond d'écran HUD de Plume (PNG).
 *   plume-wall fichier.png [largeur hauteur]
 * Même palette que plume-hud : bleu nuit, grille et anneaux cyan, équerres aux coins.
 */
#define _GNU_SOURCE
#include <math.h>
#include <pango/pangocairo.h>
#include <stdio.h>
#include <stdlib.h>

#define FONT "JetBrains Mono, DejaVu Sans Mono"

static void rgba(cairo_t *cr, unsigned c, double a)
{
	cairo_set_source_rgba(cr, ((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0, (c & 255) / 255.0, a);
}

static void label(cairo_t *cr, double x, double y, double px, double ls, unsigned col, double a, int center,
		  const char *s)
{
	PangoLayout *l = pango_cairo_create_layout(cr);
	PangoFontDescription *fd = pango_font_description_from_string(FONT);
	pango_font_description_set_absolute_size(fd, px * PANGO_SCALE);
	pango_layout_set_font_description(l, fd);
	pango_font_description_free(fd);
	PangoAttrList *al = pango_attr_list_new();
	pango_attr_list_insert(al, pango_attr_letter_spacing_new((int)(ls * px * PANGO_SCALE)));
	pango_layout_set_attributes(l, al);
	pango_attr_list_unref(al);
	pango_layout_set_text(l, s, -1);
	int w, h;
	pango_layout_get_pixel_size(l, &w, &h);
	cairo_move_to(cr, center ? x - w / 2.0 + ls * px / 2 : x, y - h / 2.0);
	rgba(cr, col, a);
	pango_cairo_show_layout(cr, l);
	g_object_unref(l);
}

static void bracket(cairo_t *cr, double x, double y, double dx, double dy, double L)
{
	cairo_move_to(cr, x, y + dy * L);
	cairo_line_to(cr, x, y);
	cairo_line_to(cr, x + dx * L, y);
	for (int i = 3; i >= 1; i--) {
		rgba(cr, 0x4fb8e0, 0.05 * (4 - i));
		cairo_set_line_width(cr, 2 + i * 3);
		cairo_stroke_preserve(cr);
	}
	rgba(cr, 0x9fe4ff, 0.55);
	cairo_set_line_width(cr, 2);
	cairo_stroke(cr);
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage : %s fichier.png [largeur hauteur]\n", argv[0]);
		return 2;
	}
	int W = argc > 3 ? atoi(argv[2]) : 2560, H = argc > 3 ? atoi(argv[3]) : 1440;
	double u = H / 100.0; /* 1 vh */
	cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, W, H);
	cairo_t *cr = cairo_create(s);

	/* fond + halo central */
	rgba(cr, 0x02060b, 1);
	cairo_paint(cr);
	cairo_save(cr);
	cairo_translate(cr, W * 0.5, H * 0.46);
	cairo_scale(cr, W * 0.65, H * 0.75);
	cairo_pattern_t *g = cairo_pattern_create_radial(0, 0, 0, 0, 0, 1);
	cairo_pattern_add_color_stop_rgba(g, 0, 30 / 255.0, 90 / 255.0, 130 / 255.0, 0.20);
	cairo_pattern_add_color_stop_rgba(g, 1, 30 / 255.0, 90 / 255.0, 130 / 255.0, 0);
	cairo_set_source(cr, g);
	cairo_paint(cr);
	cairo_pattern_destroy(g);
	cairo_restore(cr);

	/* grille fine + grille majeure */
	double step = round(6.4 * u);
	cairo_set_line_width(cr, 1);
	for (int major = 0; major < 2; major++) {
		double st = major ? step * 4 : step;
		rgba(cr, 0x4fb8e0, major ? 0.045 : 0.018);
		for (double x = fmod(W / 2.0, st); x < W; x += st) {
			cairo_move_to(cr, floor(x) + 0.5, 0);
			cairo_line_to(cr, floor(x) + 0.5, H);
		}
		for (double y = fmod(H / 2.0, st); y < H; y += st) {
			cairo_move_to(cr, 0, floor(y) + 0.5);
			cairo_line_to(cr, W, floor(y) + 0.5);
		}
		cairo_stroke(cr);
	}

	/* anneaux HUD au centre */
	double cx = W / 2.0, cy = H * 0.46;
	double dash[] = { 3, 7 };
	cairo_set_dash(cr, dash, 2, 0);
	rgba(cr, 0x4fb8e0, 0.10);
	cairo_arc(cr, cx, cy, 34 * u, 0, 2 * M_PI);
	cairo_stroke(cr);
	cairo_set_dash(cr, NULL, 0, 0);
	cairo_set_line_width(cr, 2);
	rgba(cr, 0x4fb8e0, 0.16);
	cairo_arc(cr, cx, cy, 27 * u, -0.80 * M_PI, -0.20 * M_PI);
	cairo_stroke(cr);
	rgba(cr, 0x4fb8e0, 0.08);
	cairo_arc(cr, cx, cy, 27 * u, 0.20 * M_PI, 0.80 * M_PI);
	cairo_stroke(cr);
	/* graduations */
	cairo_set_line_width(cr, 1);
	for (int i = 0; i < 120; i++) {
		double a = i * 2 * M_PI / 120, r1 = 30.5 * u, r2 = r1 + (i % 10 == 0 ? 1.6 * u : 0.6 * u);
		rgba(cr, 0x4fb8e0, i % 10 == 0 ? 0.22 : 0.10);
		cairo_move_to(cr, cx + cos(a) * r1, cy + sin(a) * r1);
		cairo_line_to(cr, cx + cos(a) * r2, cy + sin(a) * r2);
		cairo_stroke(cr);
	}
	/* réticule */
	rgba(cr, 0x4fb8e0, 0.12);
	cairo_move_to(cr, cx - 40 * u, cy + 0.5);
	cairo_line_to(cr, cx - 36 * u, cy + 0.5);
	cairo_move_to(cr, cx + 36 * u, cy + 0.5);
	cairo_line_to(cr, cx + 40 * u, cy + 0.5);
	cairo_stroke(cr);

	/* logo en filigrane */
	label(cr, cx, cy - 1.2 * u, 7.5 * u, 0.55, 0xcdeefc, 0.10, 1, "PLUME");
	label(cr, cx, cy + 5.2 * u, 1.4 * u, 0.6, 0x4fb8e0, 0.30, 1, "◆  ALPINE LINUX  ·  HUD  ◆");

	/* lignes de balayage */
	cairo_set_source_rgba(cr, 1, 1, 1, 0.010);
	for (int y = 0; y < H; y += 3)
		cairo_rectangle(cr, 0, y, W, 1);
	cairo_fill(cr);

	/* équerres aux 4 coins de l'écran */
	double m = 2.2 * u, L = 5 * u;
	bracket(cr, m, m + 3 * u, 1, 1, L);
	bracket(cr, W - m, m + 3 * u, -1, 1, L);
	bracket(cr, m, H - m, 1, -1, L);
	bracket(cr, W - m, H - m, -1, -1, L);

	/* coordonnées discrètes, façon interface système */
	char buf[64];
	snprintf(buf, sizeof buf, "SYS://PLUME  %dx%d", W, H);
	label(cr, m + 1.2 * u, H - m - 1.2 * u, 1.1 * u, 0.25, 0x5f8ea5, 0.45, 0, buf);

	cairo_destroy(cr);
	cairo_status_t st = cairo_surface_write_to_png(s, argv[1]);
	cairo_surface_destroy(s);
	if (st != CAIRO_STATUS_SUCCESS) {
		fprintf(stderr, "plume-wall : écriture impossible (%s)\n", cairo_status_to_string(st));
		return 1;
	}
	return 0;
}
