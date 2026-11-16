/* plume-fonds — fonds Plume générés (palette du HUD), créés une fois à l'installation. */
#include "fonds.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define C_BG   0x02060b
#define C_LINE 0x4fb8e0
#define C_CYAN 0x6fe3ff
#define C_OK   0x4fe0a4
#define C_PURP 0xb48cff
#define C_PINK 0xff8fb1

/* hasard reproductible : les fonds sont identiques sur toutes les machines */
static uint32_t seed = 12345;
static double rnd(void)
{
	seed = seed * 1664525u + 1013904223u;
	return (seed >> 8) / 16777216.0;
}

static void rgba(cairo_t *cr, uint32_t c, double a)
{
	cairo_set_source_rgba(cr, ((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0, (c & 255) / 255.0, a);
}

static void base(cairo_t *cr, int W, int H, uint32_t top, uint32_t bottom)
{
	cairo_pattern_t *g = cairo_pattern_create_linear(0, 0, 0, H);
	cairo_pattern_add_color_stop_rgb(g, 0, ((top >> 16) & 255) / 255.0, ((top >> 8) & 255) / 255.0, (top & 255) / 255.0);
	cairo_pattern_add_color_stop_rgb(g, 1, ((bottom >> 16) & 255) / 255.0, ((bottom >> 8) & 255) / 255.0, (bottom & 255) / 255.0);
	cairo_set_source(cr, g);
	cairo_paint(cr);
	cairo_pattern_destroy(g);
}

static void glow(cairo_t *cr, double x, double y, double r, uint32_t c, double a)
{
	cairo_pattern_t *g = cairo_pattern_create_radial(x, y, 0, x, y, r);
	cairo_pattern_add_color_stop_rgba(g, 0, ((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0, (c & 255) / 255.0, a);
	cairo_pattern_add_color_stop_rgba(g, 1, ((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0, (c & 255) / 255.0, 0);
	cairo_set_source(cr, g);
	cairo_arc(cr, x, y, r, 0, 2 * M_PI);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
}

static void stars(cairo_t *cr, int W, int H, int n, double ymax)
{
	for (int i = 0; i < n; i++) {
		double x = rnd() * W, y = rnd() * ymax, r = 0.6 + rnd() * 1.4;
		rgba(cr, rnd() < 0.2 ? C_CYAN : 0xe9f9ff, 0.25 + rnd() * 0.6);
		cairo_arc(cr, x, y, r * W / 2560.0 * 1.6, 0, 2 * M_PI);
		cairo_fill(cr);
	}
}

/* lignes de balayage très discrètes, comme le HUD */
static void scanlines(cairo_t *cr, int W, int H)
{
	cairo_set_source_rgba(cr, 1, 1, 1, 0.012);
	for (int y = 0; y < H; y += 3)
		cairo_rectangle(cr, 0, y, W, 1);
	cairo_fill(cr);
}

/* ------------------------------ Horizon ------------------------------ */

static void horizon(cairo_t *cr, int W, int H)
{
	base(cr, W, H, 0x01040a, 0x061a2c);
	double hy = H * 0.6, cx = W / 2.0;
	stars(cr, W, H, 260, hy * 0.95);
	glow(cr, cx, hy, W * 0.45, C_LINE, 0.22);
	glow(cr, cx, hy, W * 0.12, C_CYAN, 0.35);
	/* sol */
	cairo_rectangle(cr, 0, hy, W, H - hy);
	cairo_pattern_t *g = cairo_pattern_create_linear(0, hy, 0, H);
	cairo_pattern_add_color_stop_rgba(g, 0, 0.02, 0.09, 0.15, 1);
	cairo_pattern_add_color_stop_rgba(g, 1, 0.0, 0.02, 0.04, 1);
	cairo_set_source(cr, g);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
	double s = W / 2560.0;
	/* lignes qui fuient vers l'horizon */
	for (int i = -30; i <= 30; i++) {
		double xb = cx + i * 160 * s;
		cairo_move_to(cr, cx + i * 6 * s, hy);
		cairo_line_to(cr, cx + (xb - cx) * 3.2, H);
	}
	rgba(cr, C_LINE, 0.35);
	cairo_set_line_width(cr, 1.4 * s);
	cairo_stroke(cr);
	/* lignes horizontales, de plus en plus serrées vers l'horizon */
	for (int i = 1; i < 26; i++) {
		double t = pow(i / 26.0, 2.2);
		double y = hy + (H - hy) * t;
		rgba(cr, C_LINE, 0.12 + 0.35 * t);
		cairo_rectangle(cr, 0, y, W, 1.4 * s);
		cairo_fill(cr);
	}
	rgba(cr, C_CYAN, 0.9);
	cairo_rectangle(cr, 0, hy - 1 * s, W, 2 * s);
	cairo_fill(cr);
	scanlines(cr, W, H);
}

/* ------------------------------- Aurore ------------------------------ */

static void aurore(cairo_t *cr, int W, int H)
{
	base(cr, W, H, 0x01030a, 0x030b14);
	stars(cr, W, H, 340, H);
	uint32_t cols[] = { C_OK, C_CYAN, C_PURP, C_LINE };
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	for (int band = 0; band < 4; band++) {
		double y0 = H * (0.28 + 0.1 * band), amp = H * (0.06 + 0.03 * rnd()), ph = rnd() * 6.28;
		double fr = 1.5 + rnd() * 1.5;
		/* voile flou : beaucoup de traits larges et très transparents */
		for (int k = 0; k < 18; k++) {
			double off = (k - 9) * H * 0.006;
			cairo_new_path(cr);
			for (int x = -40; x <= W + 40; x += 20) {
				double y = y0 + off + amp * sin(x / (double)W * fr * 6.28 + ph) +
					   amp * 0.4 * sin(x / (double)W * 13 + ph * 2);
				if (x == -40) cairo_move_to(cr, x, y);
				else cairo_line_to(cr, x, y);
			}
			rgba(cr, cols[band], 0.035);
			cairo_set_line_width(cr, H * (0.05 + 0.006 * abs(k - 9)));
			cairo_stroke(cr);
		}
	}
	glow(cr, W * 0.5, H * 1.1, W * 0.6, C_LINE, 0.12);
	scanlines(cr, W, H);
}

/* ---------------------------- Constellation -------------------------- */

static void constellation(cairo_t *cr, int W, int H)
{
	base(cr, W, H, 0x02060b, 0x050f1c);
	glow(cr, W * 0.7, H * 0.3, W * 0.5, C_PURP, 0.10);
	glow(cr, W * 0.25, H * 0.75, W * 0.45, C_LINE, 0.10);
	stars(cr, W, H, 200, H);
	enum { N = 150 };
	double px[N], py[N];
	for (int i = 0; i < N; i++) {
		px[i] = rnd() * W;
		py[i] = rnd() * H;
	}
	double maxd = W * 0.085;
	cairo_set_line_width(cr, 1.2 * W / 2560.0);
	for (int i = 0; i < N; i++)
		for (int j = i + 1; j < N; j++) {
			double dx = px[i] - px[j], dy = py[i] - py[j], d = sqrt(dx * dx + dy * dy);
			if (d > maxd)
				continue;
			rgba(cr, C_LINE, 0.35 * (1 - d / maxd));
			cairo_move_to(cr, px[i], py[i]);
			cairo_line_to(cr, px[j], py[j]);
			cairo_stroke(cr);
		}
	for (int i = 0; i < N; i++) {
		double r = (1.5 + rnd() * 2.5) * W / 2560.0;
		glow(cr, px[i], py[i], r * 6, C_CYAN, 0.25);
		rgba(cr, 0xe9f9ff, 0.9);
		cairo_arc(cr, px[i], py[i], r, 0, 2 * M_PI);
		cairo_fill(cr);
	}
	scanlines(cr, W, H);
}

/* ------------------------------- Circuit ----------------------------- */

static void circuit(cairo_t *cr, int W, int H)
{
	base(cr, W, H, 0x02070c, 0x03101a);
	double s = W / 2560.0, step = 32 * s;
	/* fine grille */
	rgba(cr, C_LINE, 0.04);
	for (double x = 0; x < W; x += step)
		cairo_rectangle(cr, x, 0, 1, H);
	for (double y = 0; y < H; y += step)
		cairo_rectangle(cr, 0, y, W, 1);
	cairo_fill(cr);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	for (int t = 0; t < 140; t++) {
		double x = floor(rnd() * W / step) * step, y = floor(rnd() * H / step) * step;
		int dir = (int)(rnd() * 8);
		static const int dx[] = { 1, 1, 0, -1, -1, -1, 0, 1 }, dy[] = { 0, 1, 1, 1, 0, -1, -1, -1 };
		uint32_t col = rnd() < 0.15 ? C_OK : rnd() < 0.2 ? C_PURP : C_LINE;
		cairo_new_path(cr);
		cairo_move_to(cr, x, y);
		int len = 3 + (int)(rnd() * 14);
		for (int k = 0; k < len; k++) {
			if (rnd() < 0.25)
				dir = (dir + (rnd() < 0.5 ? 1 : 7)) % 8; /* virage à 45° */
			x += dx[dir] * step;
			y += dy[dir] * step;
			cairo_line_to(cr, x, y);
		}
		cairo_path_t *p = cairo_copy_path(cr);
		rgba(cr, col, 0.08);
		cairo_set_line_width(cr, 7 * s);
		cairo_stroke(cr);
		cairo_append_path(cr, p);
		cairo_path_destroy(p);
		rgba(cr, col, 0.55);
		cairo_set_line_width(cr, 1.6 * s);
		cairo_stroke(cr);
		/* pastille au bout */
		cairo_arc(cr, x, y, 5 * s, 0, 2 * M_PI);
		rgba(cr, col, 0.9);
		cairo_set_line_width(cr, 1.6 * s);
		cairo_stroke(cr);
	}
	glow(cr, W / 2.0, H / 2.0, W * 0.55, C_LINE, 0.08);
	scanlines(cr, W, H);
}

/* ----------------------------- Topographie --------------------------- */

static double field(double x, double y)
{
	/* relief doux : quelques bosses et ondulations */
	static double cx[7], cy[7], cr_[7], ca[7];
	static bool init;
	if (!init) {
		for (int i = 0; i < 7; i++) {
			cx[i] = rnd();
			cy[i] = rnd();
			cr_[i] = 0.12 + rnd() * 0.22;
			ca[i] = (rnd() < 0.5 ? -1 : 1) * (0.6 + rnd());
		}
		init = true;
	}
	double v = 0.25 * sin(x * 7.0 + y * 3.0) + 0.18 * sin(y * 9.0 - x * 2.0);
	for (int i = 0; i < 7; i++) {
		double dx = (x - cx[i]) / cr_[i], dy = (y - cy[i]) / cr_[i];
		v += ca[i] * exp(-(dx * dx + dy * dy));
	}
	return v;
}

static void topographie(cairo_t *cr, int W, int H)
{
	base(cr, W, H, 0x02060b, 0x04101b);
	double s = W / 2560.0, cell = 10 * s;
	int nx = (int)(W / cell) + 2, ny = (int)(H / cell) + 2;
	double *v = malloc(sizeof(double) * nx * ny);
	if (!v)
		return;
	for (int j = 0; j < ny; j++)
		for (int i = 0; i < nx; i++)
			v[j * nx + i] = field(i * cell / W, j * cell / (double)W);
	cairo_set_line_width(cr, 1.3 * s);
	for (int lvl = -14; lvl <= 14; lvl++) {
		double iso = lvl * 0.12;
		bool major = lvl % 5 == 0;
		rgba(cr, major ? C_CYAN : C_LINE, major ? 0.45 : 0.20);
		/* carrés qui avancent : un segment par case traversée */
		for (int j = 0; j < ny - 1; j++)
			for (int i = 0; i < nx - 1; i++) {
				double a = v[j * nx + i], b = v[j * nx + i + 1], c = v[(j + 1) * nx + i + 1], d = v[(j + 1) * nx + i];
				double x0 = i * cell, y0 = j * cell;
				double pts[4][2];
				int np = 0;
				if ((a < iso) != (b < iso)) { double t = (iso - a) / (b - a); pts[np][0] = x0 + t * cell; pts[np++][1] = y0; }
				if ((b < iso) != (c < iso)) { double t = (iso - b) / (c - b); pts[np][0] = x0 + cell; pts[np++][1] = y0 + t * cell; }
				if ((c < iso) != (d < iso)) { double t = (iso - d) / (c - d); pts[np][0] = x0 + t * cell; pts[np++][1] = y0 + cell; }
				if ((d < iso) != (a < iso)) { double t = (iso - a) / (d - a); pts[np][0] = x0; pts[np++][1] = y0 + t * cell; }
				for (int k = 0; k + 1 < np; k += 2) {
					cairo_move_to(cr, pts[k][0], pts[k][1]);
					cairo_line_to(cr, pts[k + 1][0], pts[k + 1][1]);
				}
			}
		cairo_stroke(cr);
	}
	free(v);
	glow(cr, W * 0.3, H * 0.35, W * 0.4, C_LINE, 0.08);
	scanlines(cr, W, H);
}

/* ------------------------------- Nébuleuse --------------------------- */

static void nebuleuse(cairo_t *cr, int W, int H)
{
	base(cr, W, H, 0x020309, 0x05040f);
	uint32_t cols[] = { C_PURP, C_LINE, C_PINK, C_CYAN };
	for (int i = 0; i < 40; i++) {
		double x = W * (0.2 + 0.6 * rnd()), y = H * (0.2 + 0.6 * rnd()), r = W * (0.05 + 0.18 * rnd());
		glow(cr, x, y, r, cols[i % 4], 0.05 + 0.06 * rnd());
	}
	stars(cr, W, H, 600, H);
	for (int i = 0; i < 12; i++) {
		double x = rnd() * W, y = rnd() * H;
		glow(cr, x, y, 18 * W / 2560.0, 0xe9f9ff, 0.5);
	}
	scanlines(cr, W, H);
}

/* ------------------------------- export ------------------------------ */

/* crée le dossier et ses parents (mkdir -p) */
static void mkdir_p(const char *dir)
{
	char p[PATH_MAX];
	snprintf(p, sizeof p, "%s", dir);
	for (char *s = p + 1; *s; s++)
		if (*s == '/') {
			*s = '\0';
			mkdir(p, 0755);
			*s = '/';
		}
	mkdir(p, 0755);
}

int generate_presets(const char *dir, int W, int H)
{
	struct { const char *file; void (*fn)(cairo_t *, int, int); } list[] = {
		{ "horizon.png", horizon }, { "aurore.png", aurore }, { "constellation.png", constellation },
		{ "circuit.png", circuit }, { "topographie.png", topographie }, { "nebuleuse.png", nebuleuse },
	};
	mkdir_p(dir);
	int ok = 0;
	for (size_t i = 0; i < sizeof list / sizeof list[0]; i++) {
		seed = 12345 + (uint32_t)i * 7919;
		cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, W, H);
		cairo_t *cr = cairo_create(s);
		list[i].fn(cr, W, H);
		cairo_destroy(cr);
		char path[PATH_MAX];
		snprintf(path, sizeof path, "%s/%s", dir, list[i].file);
		if (cairo_surface_write_to_png(s, path) == CAIRO_STATUS_SUCCESS) {
			ok++;
			printf("  %s\n", path);
		} else {
			fprintf(stderr, "plume-fonds : impossible d'écrire %s\n", path);
		}
		cairo_surface_destroy(s);
	}
	return ok == (int)(sizeof list / sizeof list[0]) ? 0 : 1;
}
