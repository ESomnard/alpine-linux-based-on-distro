/* plume-term — mode HUD : panneaux SYSTÈME, RÉSEAU, FICHIERS, CLAVIER autour du terminal. */
#include "term.h"

#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

struct hud H;
static bool static_dirty; /* couche fixe à refaire (disposition du clavier changée) */

#define C_BG      0x02060b
#define C_LINE    0x4fb8e0
#define C_LINEDIM 0x1d5a78
#define C_TEXT    0xcdeefc
#define C_MUTED   0x5f8ea5
#define C_OK      0x4fe0a4
#define C_ERR     0xff5470
#define C_WARN    0xe6c86e
#define C_PURP    0xb48cff
#define C_BRIGHT  0xe9f9ff
#define C_CORNER  0x9fe4ff
#define FONT      "JetBrains Mono, DejaVu Sans Mono, WenQuanYi Zen Hei"

static const char *JOURS[] = { "DIMANCHE", "LUNDI", "MARDI", "MERCREDI", "JEUDI", "VENDREDI", "SAMEDI" };
static const char *MOIS[] = { "JANV", "FÉVR", "MARS", "AVR", "MAI", "JUIN", "JUIL", "AOÛT", "SEPT", "OCT", "NOV", "DÉC" };

/* ------------------------------ outils ------------------------------- */

static double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

static void rgba(cairo_t *cr, uint32_t c, double a)
{
	cairo_set_source_rgba(cr, ((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0, (c & 255) / 255.0, a);
}

static void rrect(cairo_t *cr, double x, double y, double w, double h, double r)
{
	if (r > w / 2) r = w / 2;
	if (r > h / 2) r = h / 2;
	cairo_new_sub_path(cr);
	cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
	cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
	cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
	cairo_arc(cr, x + r, y + r, r, M_PI, 1.5 * M_PI);
	cairo_close_path(cr);
}

static void glow_stroke(cairo_t *cr, uint32_t col, double lw, double k)
{
	static const double wd[] = { 8, 4.5, 2 };
	for (int i = 0; i < 3; i++) {
		rgba(cr, col, 0.07 * k * (i + 1));
		cairo_set_line_width(cr, lw + wd[i]);
		cairo_stroke_preserve(cr);
	}
	rgba(cr, col, 1);
	cairo_set_line_width(cr, lw);
	cairo_stroke(cr);
}

static double txt(cairo_t *cr, double px, int weight, double ls, uint32_t col, double a,
		  double x, double cy, int align, double maxw, const char *s)
{
	PangoLayout *l = pango_cairo_create_layout(cr);
	PangoFontDescription *fd = pango_font_description_from_string(FONT);
	pango_font_description_set_absolute_size(fd, px * PANGO_SCALE);
	pango_font_description_set_weight(fd, weight);
	pango_layout_set_font_description(l, fd);
	pango_font_description_free(fd);
	if (ls) {
		PangoAttrList *al = pango_attr_list_new();
		pango_attr_list_insert(al, pango_attr_letter_spacing_new((int)(ls * px * PANGO_SCALE)));
		pango_layout_set_attributes(l, al);
		pango_attr_list_unref(al);
	}
	pango_layout_set_text(l, s, -1);
	if (maxw > 0) {
		pango_layout_set_width(l, (int)(maxw * PANGO_SCALE));
		pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_END);
	}
	int w, h;
	pango_layout_get_pixel_size(l, &w, &h);
	double tx = align == 0 ? x : align == 1 ? x - w / 2.0 : x - w;
	cairo_move_to(cr, tx, cy - h / 2.0);
	rgba(cr, col, a);
	pango_cairo_show_layout(cr, l);
	g_object_unref(l);
	return w;
}

static void panel(cairo_t *cr, struct rect r)
{
	rrect(cr, r.x, r.y, r.w, r.h, 14);
	cairo_pattern_t *g = cairo_pattern_create_linear(0, r.y, 0, r.y + r.h);
	cairo_pattern_add_color_stop_rgba(g, 0, 8 / 255.0, 20 / 255.0, 30 / 255.0, 0.55);
	cairo_pattern_add_color_stop_rgba(g, 1, 3 / 255.0, 8 / 255.0, 14 / 255.0, 1);
	cairo_set_source(cr, g);
	cairo_fill_preserve(cr);
	cairo_pattern_destroy(g);
	glow_stroke(cr, C_LINE, 1.5, 1);
	const double L = 30, rad = 9;
	double bx = r.x + 4, by = r.y + 4;
	cairo_move_to(cr, bx, by + L);
	cairo_line_to(cr, bx, by + rad);
	cairo_arc(cr, bx + rad, by + rad, rad, M_PI, 1.5 * M_PI);
	cairo_line_to(cr, bx + L, by);
	glow_stroke(cr, C_CORNER, 2, 0.8);
	double ex = r.x + r.w - 4, ey = r.y + r.h - 4;
	cairo_move_to(cr, ex, ey - L);
	cairo_line_to(cr, ex, ey - rad);
	cairo_arc(cr, ex - rad, ey - rad, rad, 0, 0.5 * M_PI);
	cairo_line_to(cr, ex - L, ey);
	glow_stroke(cr, C_CORNER, 2, 0.8);
}

/* titre « NOM ─────── droite » ; renvoie la hauteur consommée */
static double title(cairo_t *cr, double x, double y, double w, double vh, const char *t, const char *right)
{
	double px = clampd(1.25 * vh, 10, 15);
	double tw = txt(cr, px, PANGO_WEIGHT_SEMIBOLD, 0.3, C_LINE, 1, x, y + px * 0.6, 0, 0, t);
	double rw = right ? txt(cr, px * 0.9, PANGO_WEIGHT_NORMAL, 0.1, C_MUTED, 1, x + w, y + px * 0.6, 2,
				w - tw - 2 * px, right) : 0;
	double lx = x + tw + 0.6 * px, lx2 = x + w - (rw ? rw + 0.6 * px : 0);
	if (lx2 > lx) {
		cairo_pattern_t *g = cairo_pattern_create_linear(lx, 0, lx2, 0);
		cairo_pattern_add_color_stop_rgba(g, 0, 0x1d / 255.0, 0x5a / 255.0, 0x78 / 255.0, 1);
		cairo_pattern_add_color_stop_rgba(g, 1, 0x1d / 255.0, 0x5a / 255.0, 0x78 / 255.0, right ? 0.6 : 0);
		cairo_set_source(cr, g);
		cairo_rectangle(cr, lx, y + px * 0.6, lx2 - lx, 1);
		cairo_fill(cr);
		cairo_pattern_destroy(g);
	}
	return px * 1.2 + 1.2 * vh;
}

static void human(double bytes, char *out, size_t cap)
{
	const char *u[] = { "o", "Ko", "Mo", "Go", "To" };
	int i = 0;
	while (bytes >= 1024 && i < 4) {
		bytes /= 1024;
		i++;
	}
	snprintf(out, cap, i ? "%.1f %s" : "%.0f %s", bytes, u[i]);
}

static void graph(cairo_t *cr, struct rect r, const double *v, int n, double max, uint32_t col, bool fill)
{
	if (max <= 0)
		max = 1;
	cairo_save(cr);
	cairo_rectangle(cr, r.x, r.y, r.w, r.h);
	cairo_clip(cr);
	rgba(cr, C_LINEDIM, 0.35);
	cairo_set_line_width(cr, 1);
	for (int i = 1; i < 4; i++) {
		double gy = floor(r.y + r.h * i / 4) + 0.5;
		cairo_move_to(cr, r.x, gy);
		cairo_line_to(cr, r.x + r.w, gy);
	}
	cairo_stroke(cr);
	cairo_new_path(cr);
	for (int i = 0; i < n; i++) {
		double x = r.x + r.w * i / (n - 1), y = r.y + r.h - r.h * clampd(v[i] / max, 0, 1);
		if (i == 0) cairo_move_to(cr, x, y);
		else cairo_line_to(cr, x, y);
	}
	if (fill) {
		cairo_path_t *line = cairo_copy_path(cr);
		cairo_line_to(cr, r.x + r.w, r.y + r.h);
		cairo_line_to(cr, r.x, r.y + r.h);
		cairo_close_path(cr);
		cairo_pattern_t *g = cairo_pattern_create_linear(0, r.y, 0, r.y + r.h);
		cairo_pattern_add_color_stop_rgba(g, 0, ((col >> 16) & 255) / 255.0, ((col >> 8) & 255) / 255.0, (col & 255) / 255.0, 0.35);
		cairo_pattern_add_color_stop_rgba(g, 1, ((col >> 16) & 255) / 255.0, ((col >> 8) & 255) / 255.0, (col & 255) / 255.0, 0.02);
		cairo_set_source(cr, g);
		cairo_fill(cr);
		cairo_pattern_destroy(g);
		cairo_append_path(cr, line);
		cairo_path_destroy(line);
	}
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	glow_stroke(cr, col, 1.5, 0.7);
	cairo_restore(cr);
	rgba(cr, C_LINEDIM, 1);
	cairo_set_line_width(cr, 1);
	cairo_rectangle(cr, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1);
	cairo_stroke(cr);
}

static void bar(cairo_t *cr, double x, double y, double w, double h, double frac, uint32_t col)
{
	rrect(cr, x, y, w, h, h / 2);
	rgba(cr, C_LINEDIM, 1);
	cairo_set_line_width(cr, 1);
	cairo_stroke(cr);
	double fw = (w - 2) * clampd(frac, 0, 1);
	if (fw > 1) {
		rrect(cr, x + 1, y + 1, fw, h - 2, (h - 2) / 2);
		cairo_pattern_t *g = cairo_pattern_create_linear(x, 0, x + w, 0);
		cairo_pattern_add_color_stop_rgb(g, 0, 0x1d / 255.0, 0x5a / 255.0, 0x78 / 255.0);
		cairo_pattern_add_color_stop_rgb(g, 1, ((col >> 16) & 255) / 255.0, ((col >> 8) & 255) / 255.0, (col & 255) / 255.0);
		cairo_set_source(cr, g);
		cairo_fill(cr);
		cairo_pattern_destroy(g);
	}
}

/* ligne « ÉTIQUETTE ........ valeur » ; renvoie la hauteur */
static double kv(cairo_t *cr, double x, double y, double w, double fs, const char *k, const char *v, uint32_t vc)
{
	double kw = txt(cr, fs * 0.85, PANGO_WEIGHT_SEMIBOLD, 0.2, C_LINEDIM, 1, x, y + fs / 2, 0, 0, k);
	txt(cr, fs * 0.9, PANGO_WEIGHT_NORMAL, 0, vc, 1, x + w, y + fs / 2, 2, w - kw - fs, v);
	return fs * 1.55;
}

/* ----------------------------- disposition --------------------------- */

struct layout {
	struct rect head, left, right, term, files, kbd;
	double vh, m;
};

static struct layout lay(int W, int H_)
{
	struct layout L;
	double vh = H_ / 100.0, m = round(1.1 * vh);
	L.vh = vh;
	L.m = m;
	L.head = (struct rect){ m * 1.5, 0, W - 3 * m, round(5.5 * vh) };
	double top = L.head.h + m * 0.6;
	double sw = round(clampd(W * 0.2, 220, 440));
	L.left = (struct rect){ m, top, sw, H_ - top - m };
	L.right = (struct rect){ W - m - sw, top, sw, H_ - top - m };
	double cx = m + sw + m, cw = W - 2 * sw - 4 * m;
	double bh = round(H_ * 0.29);
	L.term = (struct rect){ cx, top, cw, H_ - top - m - bh - m };
	double fw = round(cw * 0.55);
	L.files = (struct rect){ cx, H_ - m - bh, fw, bh };
	L.kbd = (struct rect){ cx + fw + m, H_ - m - bh, cw - fw - m, bh };
	return L;
}

struct rect hud_term_area(int W, int H_)
{
	/* le cadre du terminal (render.c) est dessiné 6 px à l'intérieur */
	struct rect r = lay(W, H_).term;
	return (struct rect){ r.x - 6, r.y - 6, r.w + 12, r.h + 12 };
}

/* ------------------------------ fichiers ----------------------------- */

#define EXT_IMG "png jpg jpeg gif webp bmp svg heic heif avif jxl tif tiff ico"
#define EXT_VID "mp4 mkv webm avi mov m4v mpg mpeg flv wmv"
#define EXT_AUD "mp3 flac ogg opus wav m4a aac wma"
#define EXT_PDF "pdf epub djvu cbz"
#define EXT_ARC "zip tar gz tgz xz zst bz2 7z rar apk iso"
#define EXT_TXT "txt md c h cpp hpp sh py js ts json conf ini cfg log csv html css xml yml yaml toml rs go java lua rb php sql"

static const char *ext_of(const char *name)
{
	const char *d = strrchr(name, '.');
	return d && d != name ? d + 1 : "";
}

static bool ext_in(const char *ext, const char *list)
{
	char buf[256], e[40];
	snprintf(buf, sizeof buf, " %s ", list);
	snprintf(e, sizeof e, " %s ", ext);
	for (char *p = e; *p; p++)
		*p = tolower((unsigned char)*p);
	return *ext && strstr(buf, e) != NULL;
}

static const char *glyph_of(const struct fentry *e, uint32_t *color)
{
	const char *x = ext_of(e->name);
	if (e->dir) { *color = C_LINE; return "▸"; }
	if (ext_in(x, EXT_IMG)) { *color = C_OK; return "▣"; }
	if (ext_in(x, EXT_VID)) { *color = C_PURP; return "▶"; }
	if (ext_in(x, EXT_AUD)) { *color = C_PURP; return "♪"; }
	if (ext_in(x, EXT_PDF)) { *color = C_ERR; return "◧"; }
	if (ext_in(x, EXT_ARC)) { *color = C_WARN; return "▤"; }
	if (ext_in(x, EXT_TXT)) { *color = C_TEXT; return "≡"; }
	if (e->exec) { *color = C_OK; return "⚙"; }
	*color = C_MUTED;
	return "·";
}

static int cmp_fe(const void *a, const void *b)
{
	const struct fentry *x = a, *y = b;
	if (x->dir != y->dir)
		return x->dir ? -1 : 1;
	return strcasecmp(x->name, y->name);
}

static void files_status(const char *fmt, const char *arg)
{
	snprintf(H.status, sizeof H.status, fmt, arg);
	H.status_t = now_s();
	H.cache_dirty = true;
}

static bool query_is_path(void)
{
	return H.query[0] == '/' || H.query[0] == '~';
}

/* recherche : sous-chaîne sans tenir compte des majuscules */
static bool matches(const char *name, const char *q)
{
	size_t n = strlen(name), m = strlen(q);
	if (!m)
		return true;
	for (size_t i = 0; i + m <= n; i++)
		if (!strncasecmp(name + i, q, m))
			return true;
	return false;
}

static void refilter(void)
{
	H.nview = 0;
	bool pathq = query_is_path();
	for (int i = 0; i < H.nent; i++) {
		struct fentry *e = &H.ent[i];
		if (e->hidden && !H.show_hidden && H.query[0] != '.')
			continue;
		if (!pathq && !matches(e->name, H.query))
			continue;
		H.view[H.nview++] = i;
	}
	if (H.sel >= H.nview)
		H.sel = H.nview ? H.nview - 1 : 0;
	if (H.sel < 0)
		H.sel = 0;
	H.snap = true;
	H.cache_dirty = true;
}

static void load_dir(const char *path)
{
	for (int i = 0; i < H.nent; i++)
		free(H.ent[i].name);
	H.nent = 0;
	snprintf(H.cwd, sizeof H.cwd, "%s", path);
	DIR *d = opendir(path);
	if (d) {
		struct dirent *de;
		while ((de = readdir(d)) && H.nent < 4096) {
			if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
				continue;
			if (H.nent == H.cap) {
				int nc = H.cap ? H.cap * 2 : 128;
				struct fentry *ne = realloc(H.ent, sizeof *ne * nc);
				int *nv = realloc(H.view, sizeof *nv * nc);
				if (nv)
					H.view = nv;
				if (!ne || !nv) {
					if (ne)
						H.ent = ne;
					break;
				}
				H.ent = ne;
				H.cap = nc;
			}
			char p[PATH_MAX];
			snprintf(p, sizeof p, "%s/%s", strcmp(path, "/") ? path : "", de->d_name);
			struct stat st;
			if (stat(p, &st) != 0 && lstat(p, &st) != 0)
				continue;
			struct fentry *e = &H.ent[H.nent++];
			e->name = strdup(de->d_name);
			e->dir = S_ISDIR(st.st_mode);
			e->exec = !e->dir && (st.st_mode & S_IXUSR);
			e->hidden = de->d_name[0] == '.';
			e->size = st.st_size;
			e->mtime = st.st_mtime;
		}
		closedir(d);
		qsort(H.ent, H.nent, sizeof H.ent[0], cmp_fe);
	}
	refilter();
}

/* aller dans un dossier (explorateur seulement, le terminal ne bouge pas) */
static bool files_chdir(const char *path)
{
	char real[PATH_MAX];
	struct stat st;
	if (!realpath(path, real)) {
		files_status("Introuvable : %s", path);
		return false;
	}
	if (stat(real, &st) != 0 || !S_ISDIR(st.st_mode)) {
		files_status("Pas un dossier : %s", path);
		return false;
	}
	if (access(real, R_OK | X_OK) != 0) {
		files_status("Accès refusé : %s", real);
		return false;
	}
	H.query[0] = '\0';
	H.sel = 0;
	H.scroll = 0;
	load_dir(real);
	return true;
}

static void files_go_up(void)
{
	if (!strcmp(H.cwd, "/"))
		return;
	char parent[4096], from[256];
	snprintf(parent, sizeof parent, "%s", H.cwd);
	char *sl = strrchr(parent, '/');
	snprintf(from, sizeof from, "%s", sl ? sl + 1 : "");
	if (sl == parent)
		sl[1] = '\0';
	else if (sl)
		*sl = '\0';
	if (files_chdir(parent))
		for (int i = 0; i < H.nview; i++)
			if (!strcmp(H.ent[H.view[i]].name, from))
				H.sel = i; /* on resélectionne le dossier d'où l'on vient */
}

/* « ~/Documents », « /etc » tapés dans la recherche */
static bool files_try_path(const char *q)
{
	char path[PATH_MAX];
	if (q[0] == '~') {
		const char *home = getenv("HOME");
		snprintf(path, sizeof path, "%s%s", home ? home : "", q + 1);
	} else if (q[0] == '/') {
		snprintf(path, sizeof path, "%s", q);
	} else {
		return false;
	}
	files_chdir(path);
	return true;
}

/* dossier du programme au premier plan dans l'onglet actif */
static bool active_cwd(char *out, size_t cap)
{
	struct tab *t = tab_active();
	if (!t)
		return false;
	pid_t pg = tcgetpgrp(t->fd);
	char p[64];
	snprintf(p, sizeof p, "/proc/%d/cwd", (int)(pg > 0 ? pg : t->pid));
	ssize_t n = readlink(p, out, cap - 1);
	if (n <= 0)
		return false;
	out[n] = '\0';
	return true;
}

/* quand le terminal change de dossier, l'explorateur le suit */
static void files_follow(void)
{
	char cwd[4096];
	if (active_cwd(cwd, sizeof cwd) && strcmp(cwd, H.term_cwd)) {
		snprintf(H.term_cwd, sizeof H.term_cwd, "%s", cwd);
		if (strcmp(cwd, H.cwd))
			files_chdir(cwd);
	}
}

/* le shell attend-il une commande ? (aucun programme au premier plan) */
static bool shell_idle(struct tab *t)
{
	return t && tcgetpgrp(t->fd) == t->pid;
}

/* « cd ici » : le terminal va dans le dossier de l'explorateur */
static void type_cd(const char *path)
{
	struct tab *t = tab_active();
	if (!shell_idle(t)) {
		files_status("%s", "Un programme tourne dans le terminal : quitte-le d'abord");
		return;
	}
	/* Ctrl+U efface la ligne en cours, puis cd -- 'chemin' */
	char cmd[PATH_MAX * 4 + 32];
	size_t n = 0;
	n += snprintf(cmd + n, sizeof cmd - n, "\x15" "cd -- '");
	for (const char *p = path; *p && n < sizeof cmd - 8; p++) {
		if (*p == '\'')
			n += snprintf(cmd + n, sizeof cmd - n, "'\\''");
		else
			cmd[n++] = *p;
	}
	n += snprintf(cmd + n, sizeof cmd - n, "'\r");
	tab_write(t, cmd, n);
	t->view = 0;
	snprintf(H.term_cwd, sizeof H.term_cwd, "%s", path);
	hud_focus_files(false);
}

static bool have(const char *prog)
{
	const char *path = getenv("PATH");
	if (!path)
		path = "/usr/local/bin:/usr/bin:/bin";
	char buf[PATH_MAX];
	const char *p = path;
	while (*p) {
		size_t n = strcspn(p, ":");
		snprintf(buf, sizeof buf, "%.*s/%s", (int)n, p, prog);
		if (access(buf, X_OK) == 0)
			return true;
		p += n + (p[n] == ':');
	}
	return false;
}

static bool looks_text(const char *path, off_t size)
{
	if (size > 4 * 1024 * 1024)
		return false;
	FILE *fp = fopen(path, "rb");
	if (!fp)
		return false;
	unsigned char buf[4096];
	size_t n = fread(buf, 1, sizeof buf, fp);
	fclose(fp);
	return memchr(buf, 0, n) == NULL;
}

static void open_entry(struct fentry *e)
{
	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/%s", strcmp(H.cwd, "/") ? H.cwd : "", e->name);
	if (e->dir) {
		files_chdir(path);
		return;
	}
	const char *x = ext_of(e->name);
	if (ext_in(x, EXT_PDF)) {
		char *argv[] = { "zathura", path, NULL };
		spawn_argv(argv);
	} else if (ext_in(x, EXT_IMG)) {
		char *argv[] = { "imv", path, NULL };
		spawn_argv(argv);
	} else if (ext_in(x, EXT_VID) || ext_in(x, EXT_AUD)) {
		char *argv[] = { "mpv", "--player-operation-mode=pseudo-gui", path, NULL };
		spawn_argv(argv);
	} else if (ext_in(x, EXT_TXT) || looks_text(path, e->size)) {
		/* texte : ouvert dans un nouvel onglet */
		char *argv[] = { have("micro") ? "micro" : "vi", path, NULL };
		if (!open_tab_cmd(argv, H.cwd))
			files_status("%s", "Plus de place pour un onglet");
		else
			hud_focus_files(false);
		return;
	} else {
		files_status("Pas d'application pour « %s »", e->name);
		return;
	}
	files_status("Ouverture de « %s »", e->name);
}

static void activate(int vi)
{
	if (vi >= 0 && vi < H.nview)
		open_entry(&H.ent[H.view[vi]]);
}

static void move_sel(int d)
{
	H.sel += d;
	if (H.sel >= H.nview) H.sel = H.nview - 1;
	if (H.sel < 0) H.sel = 0;
	H.snap = true;
	H.cache_dirty = true;
}

static void add_place(const char *glyph, const char *label, const char *path)
{
	struct stat st;
	if (H.nplaces >= MAX_PLACES || stat(path, &st) != 0 || !S_ISDIR(st.st_mode))
		return;
	struct place *p = &H.places[H.nplaces++];
	snprintf(p->glyph, sizeof p->glyph, "%s", glyph);
	snprintf(p->label, sizeof p->label, "%s", label);
	snprintf(p->path, sizeof p->path, "%s", path);
}

static void places_init(void)
{
	const char *home = getenv("HOME");
	if (!home)
		home = "/";
	add_place("⌂", "Perso", home);
	static const char *dirs[][2] = {
		{ "▭", "Bureau" }, { "▤", "Documents" }, { "↓", "Téléchargements" },
		{ "▣", "Images" }, { "♪", "Musique" }, { "▶", "Vidéos" },
	};
	char p[1024];
	for (size_t i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
		snprintf(p, sizeof p, "%s/%s", home, dirs[i][1]);
		add_place(dirs[i][0], dirs[i][1], p);
	}
	add_place("/", "Racine", "/");
}

void hud_focus_files(bool on)
{
	if (H.focus == on)
		return;
	H.focus = on;
	H.cache_dirty = true;
	A.dirty = true;
}

/* touches quand l'explorateur a le clavier */
void hud_key(xkb_keysym_t sym, const char *text, bool ctrl)
{
	H.cache_dirty = true;
	A.dirty = true;
	if (ctrl) {
		if (sym == XKB_KEY_h || sym == XKB_KEY_H) {
			H.show_hidden = !H.show_hidden;
			refilter();
		} else if (sym == XKB_KEY_u || sym == XKB_KEY_U) {
			H.query[0] = '\0';
			refilter();
		}
		return;
	}
	switch (sym) {
	case XKB_KEY_Escape:
		if (H.query[0]) {
			H.query[0] = '\0';
			H.sel = 0;
			refilter();
		} else {
			hud_focus_files(false); /* retour au terminal */
		}
		return;
	case XKB_KEY_Up: case XKB_KEY_KP_Up: move_sel(-1); return;
	case XKB_KEY_Down: case XKB_KEY_KP_Down: move_sel(1); return;
	case XKB_KEY_Prior: move_sel(-8); return;
	case XKB_KEY_Next: move_sel(8); return;
	case XKB_KEY_Home: move_sel(-100000); return;
	case XKB_KEY_End: move_sel(100000); return;
	case XKB_KEY_Return: case XKB_KEY_KP_Enter:
		if (H.query[0] && query_is_path())
			files_try_path(H.query);
		else
			activate(H.sel);
		return;
	case XKB_KEY_Right: case XKB_KEY_Tab:
		if (H.nview > 0 && H.ent[H.view[H.sel]].dir)
			activate(H.sel);
		return;
	case XKB_KEY_Left:
		if (!H.query[0])
			files_go_up();
		return;
	case XKB_KEY_BackSpace:
		if (H.query[0]) {
			size_t n = strlen(H.query);
			while (n > 0 && ((unsigned char)H.query[--n] & 0xC0) == 0x80)
				;
			H.query[n] = '\0';
			H.sel = 0;
			refilter();
		} else {
			files_go_up();
		}
		return;
	default:
		break;
	}
	if (text && (unsigned char)text[0] >= 0x20 && text[0] != 0x7f) {
		size_t n = strlen(H.query), m = strlen(text);
		if (n + m < sizeof H.query) {
			memcpy(H.query + n, text, m + 1);
			H.sel = 0;
			refilter();
		}
	}
}

/* ------------------------------ clavier ------------------------------ */

struct kdef {
	uint32_t code;
	double w;
	const char *fixed;
};

#define KROWS 5
static const struct kdef KR0[] = {
	{ KEY_GRAVE, 1, NULL }, { KEY_1, 1, NULL }, { KEY_2, 1, NULL }, { KEY_3, 1, NULL }, { KEY_4, 1, NULL },
	{ KEY_5, 1, NULL }, { KEY_6, 1, NULL }, { KEY_7, 1, NULL }, { KEY_8, 1, NULL }, { KEY_9, 1, NULL },
	{ KEY_0, 1, NULL }, { KEY_MINUS, 1, NULL }, { KEY_EQUAL, 1, NULL }, { KEY_BACKSPACE, 2, "⌫" },
};
static const struct kdef KR1[] = {
	{ KEY_TAB, 1.5, "⇥" }, { KEY_Q, 1, NULL }, { KEY_W, 1, NULL }, { KEY_E, 1, NULL }, { KEY_R, 1, NULL },
	{ KEY_T, 1, NULL }, { KEY_Y, 1, NULL }, { KEY_U, 1, NULL }, { KEY_I, 1, NULL }, { KEY_O, 1, NULL },
	{ KEY_P, 1, NULL }, { KEY_LEFTBRACE, 1, NULL }, { KEY_RIGHTBRACE, 1, NULL }, { KEY_ENTER, 1.5, "⏎" },
};
static const struct kdef KR2[] = {
	{ KEY_CAPSLOCK, 1.75, "VERR" }, { KEY_A, 1, NULL }, { KEY_S, 1, NULL }, { KEY_D, 1, NULL },
	{ KEY_F, 1, NULL }, { KEY_G, 1, NULL }, { KEY_H, 1, NULL }, { KEY_J, 1, NULL }, { KEY_K, 1, NULL },
	{ KEY_L, 1, NULL }, { KEY_SEMICOLON, 1, NULL }, { KEY_APOSTROPHE, 1, NULL }, { KEY_BACKSLASH, 1, NULL },
	{ KEY_ENTER, 1.25, "" },
};
static const struct kdef KR3[] = {
	{ KEY_LEFTSHIFT, 1.25, "⇧" }, { KEY_102ND, 1, NULL }, { KEY_Z, 1, NULL }, { KEY_X, 1, NULL },
	{ KEY_C, 1, NULL }, { KEY_V, 1, NULL }, { KEY_B, 1, NULL }, { KEY_N, 1, NULL }, { KEY_M, 1, NULL },
	{ KEY_COMMA, 1, NULL }, { KEY_DOT, 1, NULL }, { KEY_SLASH, 1, NULL }, { KEY_RIGHTSHIFT, 2.75, "⇧ MAJ" },
};
static const struct kdef KR4[] = {
	{ KEY_LEFTCTRL, 1.5, "CTRL" }, { KEY_ESC, 1.25, "ÉCH" }, { KEY_LEFTALT, 1.25, "ALT" },
	{ KEY_SPACE, 5.5, "ESPACE" }, { KEY_RIGHTALT, 1.5, "ALT GR" }, { KEY_LEFT, 1, "←" },
	{ KEY_UP, 1, "↑" }, { KEY_DOWN, 1, "↓" }, { KEY_RIGHT, 1, "→" },
};
static const struct { const struct kdef *k; int n; } KROWS_DEF[KROWS] = {
	{ KR0, sizeof KR0 / sizeof KR0[0] }, { KR1, sizeof KR1 / sizeof KR1[0] },
	{ KR2, sizeof KR2 / sizeof KR2[0] }, { KR3, sizeof KR3 / sizeof KR3[0] },
	{ KR4, sizeof KR4 / sizeof KR4[0] },
};

static void sym_label(xkb_keysym_t sym, char *out, size_t cap)
{
	out[0] = '\0';
	if (sym == XKB_KEY_NoSymbol)
		return;
	char buf[16];
	int n = xkb_keysym_to_utf8(xkb_keysym_to_upper(sym), buf, sizeof buf);
	if (n > 1 && (unsigned char)buf[0] >= 0x20) {
		snprintf(out, cap, "%s", buf);
		return;
	}
	switch (sym) {
	case XKB_KEY_dead_circumflex: snprintf(out, cap, "^"); break;
	case XKB_KEY_dead_diaeresis: snprintf(out, cap, "¨"); break;
	case XKB_KEY_dead_grave: snprintf(out, cap, "`"); break;
	case XKB_KEY_dead_acute: snprintf(out, cap, "´"); break;
	case XKB_KEY_dead_tilde: snprintf(out, cap, "~"); break;
	default: break;
	}
}

/* disposition à afficher : celle du compositeur si elle est complète
 * (un clavier virtuel peut n'en fournir qu'une partie), sinon fr,us */
static struct xkb_keymap *display_keymap(bool *fallback)
{
	struct xkb_keymap *km = A.keymap;
	if (km) {
		struct xkb_state *probe = xkb_state_new(km);
		if (xkb_state_key_get_one_sym(probe, KEY_A + 8) == XKB_KEY_NoSymbol)
			km = NULL;
		xkb_state_unref(probe);
	}
	*fallback = !km;
	if (!km) {
		static struct xkb_keymap *fb;
		if (!fb && A.xkb_ctx) {
			const char *layout = getenv("XKB_DEFAULT_LAYOUT");
			struct xkb_rule_names names = { .layout = layout && *layout ? layout : "fr,us" };
			fb = xkb_keymap_new_from_names(A.xkb_ctx, &names, 0);
		}
		km = fb;
	}
	return km;
}

static xkb_layout_index_t display_group(bool fallback)
{
	return A.xkb_state && !fallback ? xkb_state_serialize_layout(A.xkb_state, XKB_STATE_LAYOUT_EFFECTIVE) : 0;
}

static const char *friendly(const char *name)
{
	if (!name || !*name)
		return "";
	if (!strncmp(name, "French", 6))
		return "AZERTY";
	if (!strncmp(name, "English", 7))
		return "QWERTY";
	return name;
}

static void keyboard_build(void)
{
	bool fallback;
	struct xkb_keymap *km = display_keymap(&fallback);
	struct xkb_state *plain = NULL, *shift = NULL;
	if (km) {
		plain = xkb_state_new(km);
		shift = xkb_state_new(km);
		xkb_mod_index_t si = xkb_keymap_mod_get_index(km, XKB_MOD_NAME_SHIFT);
		xkb_layout_index_t g = display_group(fallback);
		xkb_state_update_mask(plain, 0, 0, 0, 0, 0, g);
		xkb_state_update_mask(shift, si != XKB_MOD_INVALID ? 1u << si : 0, 0, 0, 0, 0, g);
		snprintf(H.layout_name, sizeof H.layout_name, "%s", friendly(xkb_keymap_layout_get_name(km, g)));
	}
	H.nkeys = 0;
	for (int r = 0; r < KROWS; r++) {
		double x = 0;
		for (int i = 0; i < KROWS_DEF[r].n && H.nkeys < MAX_KEYS; i++) {
			const struct kdef *kd = &KROWS_DEF[r].k[i];
			struct key *k = &H.keys[H.nkeys++];
			double heat = k->heat;
			bool down = k->down;
			memset(k, 0, sizeof *k);
			k->heat = heat;
			k->down = down;
			k->code = kd->code;
			k->x = x;
			k->y = r;
			k->w = kd->w;
			if (kd->fixed) {
				k->special = true;
				snprintf(k->label, sizeof k->label, "%s", kd->fixed);
			} else if (plain) {
				sym_label(xkb_state_key_get_one_sym(plain, kd->code + 8), k->label, sizeof k->label);
				char sl[16];
				sym_label(xkb_state_key_get_one_sym(shift, kd->code + 8), sl, sizeof sl);
				if (strcmp(sl, k->label))
					snprintf(k->shift_label, sizeof k->shift_label, "%s", sl);
			}
			x += kd->w;
		}
	}
	if (plain) xkb_state_unref(plain);
	if (shift) xkb_state_unref(shift);
}

void hud_keymap_changed(void)
{
	keyboard_build();
	static_dirty = true; /* les inscriptions changent (AZERTY ⇄ QWERTY) */
}

void hud_key_light(uint32_t code, bool down)
{
	if (down)
		H.keystrokes++;
	for (int i = 0; i < H.nkeys; i++)
		if (H.keys[i].code == code) {
			H.keys[i].down = down;
			if (down) {
				H.keys[i].heat = 1;
				if (H.keys[i].label[0])
					snprintf(H.last_key, sizeof H.last_key, "%s", H.keys[i].label);
			}
		}
}

void hud_decay(double dt)
{
	for (int i = 0; i < H.nkeys; i++) {
		struct key *k = &H.keys[i];
		if (k->down)
			k->heat = 1;
		else if (k->heat > 0)
			k->heat = fmax(0, k->heat - dt / 0.9);
	}
}

bool hud_animating(void)
{
	if (now_s() - H.intro_t < 1.1)
		return true;
	for (int i = 0; i < H.nkeys; i++)
		if (H.keys[i].heat > 0)
			return true;
	return false;
}

/* touche cliquée : envoyée au terminal (Maj et Ctrl « collants ») */
static void click_key(struct key *k)
{
	k->heat = 1;
	H.keystrokes++;
	VTermModifier mod = H.sticky_ctrl ? VTERM_MOD_CTRL : VTERM_MOD_NONE;
	switch (k->code) {
	case KEY_LEFTSHIFT:
	case KEY_RIGHTSHIFT: H.sticky_shift = !H.sticky_shift; H.cache_dirty = true; return;
	case KEY_LEFTCTRL: H.sticky_ctrl = !H.sticky_ctrl; H.cache_dirty = true; return;
	case KEY_LEFTALT: case KEY_RIGHTALT: case KEY_CAPSLOCK: return;
	case KEY_BACKSPACE: send_key_to_terminal(VTERM_KEY_BACKSPACE, mod); break;
	case KEY_ENTER: send_key_to_terminal(VTERM_KEY_ENTER, mod); break;
	case KEY_TAB: send_key_to_terminal(VTERM_KEY_TAB, mod); break;
	case KEY_ESC: send_key_to_terminal(VTERM_KEY_ESCAPE, mod); break;
	case KEY_LEFT: send_key_to_terminal(VTERM_KEY_LEFT, mod); break;
	case KEY_RIGHT: send_key_to_terminal(VTERM_KEY_RIGHT, mod); break;
	case KEY_UP: send_key_to_terminal(VTERM_KEY_UP, mod); break;
	case KEY_DOWN: send_key_to_terminal(VTERM_KEY_DOWN, mod); break;
	case KEY_SPACE: send_to_terminal(' ', mod); break;
	default: {
		bool fallback;
		struct xkb_keymap *km = display_keymap(&fallback);
		if (!km)
			break;
		struct xkb_state *st = xkb_state_new(km);
		xkb_mod_index_t si = xkb_keymap_mod_get_index(km, XKB_MOD_NAME_SHIFT);
		xkb_state_update_mask(st, H.sticky_shift && si != XKB_MOD_INVALID ? 1u << si : 0, 0, 0, 0, 0,
				      display_group(fallback));
		xkb_keysym_t sym = xkb_state_key_get_one_sym(st, k->code + 8);
		xkb_state_unref(st);
		uint32_t c = xkb_keysym_to_utf32(sym);
		if (c)
			send_to_terminal(c, mod);
		break;
	}
	}
	H.sticky_shift = false;
	H.sticky_ctrl = false;
	H.cache_dirty = true;
}

/* ------------------------------ panneaux ----------------------------- */

static void header(cairo_t *cr, struct rect r, double vh)
{
	double cy = r.y + r.h / 2;
	double x = r.x + 4;
	x += txt(cr, 2 * vh, PANGO_WEIGHT_NORMAL, 0, C_LINE, 1, x, cy, 0, 0, "◆") + 10;
	x += txt(cr, 1.7 * vh, PANGO_WEIGHT_BOLD, 0.35, C_BRIGHT, 1, x, cy, 0, 0, "PLUME") + 14;
	txt(cr, 1.3 * vh, PANGO_WEIGHT_NORMAL, 0.3, C_MUTED, 1, x, cy, 0, 0, "//  TERMINAL HUD");
	const char *user = getenv("USER");
	char host[64] = "", who[160];
	gethostname(host, sizeof host - 1);
	snprintf(who, sizeof who, "%s@%s", user ? user : "?", host);
	txt(cr, 1.3 * vh, PANGO_WEIGHT_NORMAL, 0.15, C_MUTED, 1, r.x + r.w / 2, cy, 1, 0, who);
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	char hm[16], day[48];
	snprintf(hm, sizeof hm, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
	snprintf(day, sizeof day, "%s %d %s", JOURS[tm.tm_wday], tm.tm_mday, MOIS[tm.tm_mon]);
	double rx = r.x + r.w - 4;
	rx -= txt(cr, 2 * vh, PANGO_WEIGHT_BOLD, 0.05, C_BRIGHT, 1, rx, cy, 2, 0, hm) + 14;
	txt(cr, 1.2 * vh, PANGO_WEIGHT_NORMAL, 0.25, C_MUTED, 1, rx, cy, 2, 0, day);
	cairo_pattern_t *g = cairo_pattern_create_linear(r.x, 0, r.x + r.w, 0);
	cairo_pattern_add_color_stop_rgba(g, 0, 79 / 255.0, 184 / 255.0, 224 / 255.0, 0.0);
	cairo_pattern_add_color_stop_rgba(g, 0.5, 79 / 255.0, 184 / 255.0, 224 / 255.0, 0.6);
	cairo_pattern_add_color_stop_rgba(g, 1, 79 / 255.0, 184 / 255.0, 224 / 255.0, 0.0);
	cairo_set_source(cr, g);
	cairo_rectangle(cr, r.x, r.y + r.h - 1, r.w, 1);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
}

static void system_panel(cairo_t *cr, struct rect r, double vh)
{
	struct perf *p = &H.perf;
	struct sysinfo_plume *s = &H.sys;
	double pad = 1.5 * vh, x = r.x + pad, w = r.w - 2 * pad, y = r.y + pad;
	double bottom = r.y + r.h - pad;
	cairo_save(cr);
	rrect(cr, r.x, r.y, r.w, r.h, 14);
	cairo_clip(cr);
	double fs = clampd(1.25 * vh, 10, 15), big = 2.5 * vh;
	char buf[128];

	y += title(cr, x, y, w, vh, "SYSTÈME", NULL);
	y += kv(cr, x, y, w, fs, "OS", s->os[0] ? s->os : "Linux", C_TEXT);
	y += kv(cr, x, y, w, fs, "NOYAU", s->kernel, C_TEXT);
	long up = p->uptime;
	if (up >= 86400)
		snprintf(buf, sizeof buf, "%ldj %02ldh%02ld", up / 86400, (up % 86400) / 3600, (up % 3600) / 60);
	else
		snprintf(buf, sizeof buf, "%ldh%02ld", up / 3600, (up % 3600) / 60);
	y += kv(cr, x, y, w, fs, "EN MARCHE", buf, C_TEXT);
	if (s->battery >= 0) {
		snprintf(buf, sizeof buf, "%d%%%s", s->battery, s->charging ? " ⚡" : "");
		y += kv(cr, x, y, w, fs, "BATTERIE", buf, s->battery < 15 && !s->charging ? C_ERR : C_OK);
	}
	y += 0.8 * vh;

	/* CPU */
	y += title(cr, x, y, w, vh, "CPU", s->cpu_model[0] ? s->cpu_model : NULL);
	snprintf(buf, sizeof buf, "%d%%", p->cpu);
	if (p->temp >= 0) {
		char tb[32];
		snprintf(tb, sizeof tb, "%.0f°C · charge %.2f", p->temp, p->load1);
		txt(cr, fs * 0.85, PANGO_WEIGHT_NORMAL, 0.05, p->temp > 85 ? C_ERR : C_MUTED, 1, x, y + big / 2, 0, w * 0.6, tb);
	} else {
		char tb[32];
		snprintf(tb, sizeof tb, "charge %.2f", p->load1);
		txt(cr, fs * 0.85, PANGO_WEIGHT_NORMAL, 0.05, C_MUTED, 1, x, y + big / 2, 0, w * 0.6, tb);
	}
	txt(cr, big, PANGO_WEIGHT_BOLD, 0, p->cpu > 85 ? C_ERR : C_BRIGHT, 1, x + w, y + big / 2, 2, 0, buf);
	y += big + 0.4 * vh;
	double avail = bottom - y;
	double gh = fmax(30, avail * 0.15);
	graph(cr, (struct rect){ x, y, w, gh }, p->cpu_hist, HIST, 100, C_LINE, true);
	y += gh + 0.5 * vh;
	int nc = p->ncores > 32 ? 32 : p->ncores;
	if (nc > 0) {
		double cw = w / nc, ch = 2.2 * vh;
		for (int i = 0; i < nc; i++) {
			double v = p->core[i] / 100.0;
			rgba(cr, C_LINEDIM, 0.6);
			cairo_rectangle(cr, x + i * cw + 1, y, cw - 2, ch);
			cairo_fill(cr);
			rgba(cr, v > 0.85 ? C_ERR : C_LINE, 0.4 + 0.6 * v);
			cairo_rectangle(cr, x + i * cw + 1, y + ch * (1 - v), cw - 2, ch * v);
			cairo_fill(cr);
		}
		y += ch + 1.2 * vh;
	}

	/* mémoire */
	snprintf(buf, sizeof buf, "%.1f / %.1f Go", p->ram_used_gb, p->ram_total_gb);
	y += title(cr, x, y, w, vh, "MÉMOIRE", buf);
	gh = fmax(24, avail * 0.10);
	graph(cr, (struct rect){ x, y, w, gh }, p->ram_hist, HIST, 100, C_OK, true);
	y += gh + 0.7 * vh;
	if (p->swap_total_mb > 0) {
		snprintf(buf, sizeof buf, "%.0f / %.0f Mo", p->swap_used_mb, p->swap_total_mb);
		y += kv(cr, x, y, w, fs, "SWAP ZRAM", buf, C_TEXT) - fs * 0.3;
		bar(cr, x, y, w, 6, p->swap_used_mb / p->swap_total_mb, C_WARN);
		y += 6 + 1.2 * vh;
	}

	/* processus */
	snprintf(buf, sizeof buf, "%d au total", p->nprocs);
	y += title(cr, x, y, w, vh, "PROCESSUS", buf);
	double rh = fs * 1.5;
	for (int i = 0; i < p->ntop && y + rh <= bottom; i++, y += rh) {
		struct proc_top *t = &p->top[i];
		txt(cr, fs * 0.9, PANGO_WEIGHT_NORMAL, 0, i == 0 ? C_BRIGHT : C_TEXT, 1, x, y + rh / 2, 0, w * 0.5, t->name);
		snprintf(buf, sizeof buf, "%.1f%%", t->cpu);
		txt(cr, fs * 0.9, PANGO_WEIGHT_NORMAL, 0, t->cpu > 50 ? C_ERR : C_LINE, 1, x + w * 0.72, y + rh / 2, 2, 0, buf);
		snprintf(buf, sizeof buf, "%.0f Mo", t->mem_mb);
		txt(cr, fs * 0.9, PANGO_WEIGHT_NORMAL, 0, C_MUTED, 1, x + w, y + rh / 2, 2, 0, buf);
	}
	cairo_restore(cr);
}

static void network_panel(cairo_t *cr, struct rect r, double vh)
{
	struct perf *p = &H.perf;
	struct sysinfo_plume *s = &H.sys;
	double pad = 1.5 * vh, x = r.x + pad, w = r.w - 2 * pad, y = r.y + pad;
	double bottom = r.y + r.h - pad;
	cairo_save(cr);
	rrect(cr, r.x, r.y, r.w, r.h, 14);
	cairo_clip(cr);
	double fs = clampd(1.25 * vh, 10, 15);
	char buf[128];

	y += title(cr, x, y, w, vh, "RÉSEAU", s->online ? "● EN LIGNE" : "○ HORS LIGNE");
	/* pastille d'état */
	y += kv(cr, x, y, w, fs, "INTERFACE", s->iface[0] ? s->iface : "aucune", C_TEXT);
	y += kv(cr, x, y, w, fs, "ADRESSE IPV4", s->ipv4[0] ? s->ipv4 : "—", s->ipv4[0] ? C_BRIGHT : C_MUTED);
	y += kv(cr, x, y, w, fs, "PASSERELLE", s->gateway[0] ? s->gateway : "—", C_TEXT);
	if (s->wireless) {
		if (s->wifi_quality >= 0)
			snprintf(buf, sizeof buf, "%d%%", s->wifi_quality);
		else
			snprintf(buf, sizeof buf, "—");
		y += kv(cr, x, y, w, fs, "SIGNAL WIFI", buf, s->wifi_quality >= 0 && s->wifi_quality < 30 ? C_WARN : C_OK);
	}
	snprintf(buf, sizeof buf, "%d", s->tcp_established);
	y += kv(cr, x, y, w, fs, "CONNEXIONS TCP", buf, C_TEXT);
	y += 0.8 * vh;

	/* débit */
	char rx[24], tx[24];
	human(p->rx_rate, rx, sizeof rx);
	human(p->tx_rate, tx, sizeof tx);
	y += title(cr, x, y, w, vh, "DÉBIT", NULL);
	snprintf(buf, sizeof buf, "↓ %s/s", rx);
	txt(cr, fs, PANGO_WEIGHT_SEMIBOLD, 0, C_LINE, 1, x, y + fs / 2, 0, 0, buf);
	snprintf(buf, sizeof buf, "↑ %s/s", tx);
	txt(cr, fs, PANGO_WEIGHT_SEMIBOLD, 0, C_OK, 1, x + w, y + fs / 2, 2, 0, buf);
	y += fs + 0.6 * vh;
	double mx = 1024;
	for (int i = 0; i < HIST; i++) {
		if (p->rx_hist[i] > mx) mx = p->rx_hist[i];
		if (p->tx_hist[i] > mx) mx = p->tx_hist[i];
	}
	double gh = fmax(40, (bottom - y) * 0.28);
	graph(cr, (struct rect){ x, y, w, gh }, p->tx_hist, HIST, mx, C_OK, false);
	graph(cr, (struct rect){ x, y, w, gh }, p->rx_hist, HIST, mx, C_LINE, true);
	y += gh + 0.6 * vh;
	human((double)s->rx_total, rx, sizeof rx);
	human((double)s->tx_total, tx, sizeof tx);
	snprintf(buf, sizeof buf, "reçu %s · envoyé %s", rx, tx);
	txt(cr, fs * 0.85, PANGO_WEIGHT_NORMAL, 0.05, C_MUTED, 1, x, y + fs / 2, 0, w, buf);
	y += fs + 1.4 * vh;

	/* stockage */
	y += title(cr, x, y, w, vh, "STOCKAGE", NULL);
	for (int i = 0; i < s->nmounts && y + fs * 2.6 <= bottom; i++) {
		struct mount_use *m = &s->mounts[i];
		snprintf(buf, sizeof buf, "%.1f / %.0f Go", m->used_gb, m->total_gb);
		double frac = m->total_gb > 0 ? m->used_gb / m->total_gb : 0;
		y += kv(cr, x, y, w, fs, m->path, buf, frac > 0.9 ? C_ERR : C_TEXT) - fs * 0.3;
		bar(cr, x, y, w, 6, frac, frac > 0.9 ? C_ERR : C_LINE);
		y += 6 + 1.1 * vh;
	}
	y += 0.4 * vh;

	/* terminal */
	struct tab *t = tab_active();
	if (t && y + fs * 5 <= bottom) {
		y += title(cr, x, y, w, vh, "TERMINAL", NULL);
		snprintf(buf, sizeof buf, "%d / %d", tab_count(), MAX_TABS);
		y += kv(cr, x, y, w, fs, "ONGLETS", buf, C_TEXT);
		snprintf(buf, sizeof buf, "%d × %d", t->cols, t->rows);
		y += kv(cr, x, y, w, fs, "TAILLE", buf, C_TEXT);
		snprintf(buf, sizeof buf, "%d lignes", t->sb_count);
		if (y + fs * 1.5 <= bottom)
			y += kv(cr, x, y, w, fs, "HISTORIQUE", buf, C_TEXT);
	}
	cairo_restore(cr);
}

static bool hovering(struct rect r)
{
	return r.w > 0 && A.px >= r.x && A.px <= r.x + r.w && A.py >= r.y && A.py <= r.y + r.h;
}

static int utf8_len(const char *s)
{
	int n = 0;
	for (; *s; s++)
		n += ((unsigned char)*s & 0xC0) != 0x80;
	return n;
}

/* fil d'Ariane cliquable : « ~ › Documents › cours » ; les premiers
 * dossiers sont remplacés par « … » s'il n'y a pas la place */
static void crumbs(cairo_t *cr, double x, double cy, double maxw, double fs)
{
	H.ncrumbs = 0;
	const char *home = getenv("HOME");
	size_t hl = home ? strlen(home) : 0;
	bool in_home = hl && !strncmp(H.cwd, home, hl) && (H.cwd[hl] == '/' || H.cwd[hl] == '\0');
	const char *rest = in_home ? H.cwd + hl : H.cwd;
	char root_path[1024];
	snprintf(root_path, sizeof root_path, "%s", in_home ? home : "/");
	/* segments */
	char seg[MAX_CRUMBS][256], acc[MAX_CRUMBS][1024];
	int ns = 0;
	char cur[1024];
	snprintf(cur, sizeof cur, "%s", in_home ? home : "");
	for (const char *s = rest; *s && ns < MAX_CRUMBS - 1;) {
		while (*s == '/')
			s++;
		if (!*s)
			break;
		size_t n = strcspn(s, "/");
		snprintf(seg[ns], sizeof seg[0], "%.*s", (int)n, s);
		size_t cl = strlen(cur);
		snprintf(cur + cl, sizeof cur - cl, "/%s", seg[ns]);
		snprintf(acc[ns], sizeof acc[0], "%s", cur);
		ns++;
		s += n;
	}
	double cwch = fs * 0.62, sep = fs * 1.6;
	double need = cwch * 1.5;
	int first = ns;
	for (int i = ns - 1; i >= 0; i--) {
		double wseg = sep + cwch * utf8_len(seg[i]);
		if (need + wseg + (i > 0 ? sep + cwch : 0) > maxw && i < ns - 1)
			break;
		need += wseg;
		first = i;
	}
	double cx = x;
	double w = txt(cr, fs, PANGO_WEIGHT_SEMIBOLD, 0.03, C_LINE, 1, cx, cy, 0, 0, in_home ? "~" : "/");
	H.r_crumbs[H.ncrumbs] = (struct rect){ cx - 4, cy - fs, w + 8, fs * 2 };
	snprintf(H.crumb_path[H.ncrumbs++], sizeof H.crumb_path[0], "%s", root_path);
	cx += w;
	if (first > 0) {
		cx += txt(cr, fs, PANGO_WEIGHT_NORMAL, 0, C_LINEDIM, 1, cx + fs * 0.4, cy, 0, 0, "›") + fs * 0.8;
		cx += txt(cr, fs, PANGO_WEIGHT_NORMAL, 0, C_MUTED, 1, cx, cy, 0, 0, "…");
	}
	for (int i = first; i < ns && H.ncrumbs < MAX_CRUMBS; i++) {
		cx += txt(cr, fs, PANGO_WEIGHT_NORMAL, 0, C_LINEDIM, 1, cx + fs * 0.4, cy, 0, 0, "›") + fs * 0.8;
		bool last = i == ns - 1;
		double room = x + maxw - cx;
		if (room < fs * 2)
			break;
		struct rect rc = { cx - 4, cy - fs, 0, fs * 2 };
		bool hov = A.px >= rc.x && A.px <= cx + cwch * utf8_len(seg[i]) + 4 && A.py >= rc.y && A.py <= rc.y + rc.h;
		w = txt(cr, fs, last ? PANGO_WEIGHT_SEMIBOLD : PANGO_WEIGHT_NORMAL, 0.02,
			last ? C_BRIGHT : hov ? C_LINE : C_TEXT, 1, cx, cy, 0, room, seg[i]);
		rc.w = w + 8;
		H.r_crumbs[H.ncrumbs] = rc;
		snprintf(H.crumb_path[H.ncrumbs++], sizeof H.crumb_path[0], "%s", acc[i]);
		cx += w;
	}
}

/* petit bouton texte ; renvoie sa largeur */
static double button(cairo_t *cr, double x, double y, double h, double fs, const char *label, bool on,
		     struct rect *out)
{
	double w = fs * 0.62 * utf8_len(label) + fs * 1.4;
	struct rect r = { x - w, y, w, h };
	*out = r;
	bool hov = hovering(r);
	rrect(cr, r.x, r.y, r.w, r.h, 6);
	rgba(cr, C_LINE, on ? 0.18 : hov ? 0.08 : 0);
	cairo_fill_preserve(cr);
	rgba(cr, on || hov ? C_LINE : C_LINEDIM, 1);
	cairo_set_line_width(cr, 1);
	cairo_stroke(cr);
	txt(cr, fs * 0.9, PANGO_WEIGHT_NORMAL, 0.03, on ? C_BRIGHT : hov ? C_TEXT : C_MUTED, 1, r.x + w / 2,
	    r.y + h / 2, 1, 0, label);
	return w;
}

static void files_panel(cairo_t *cr, struct rect r, double vh)
{
	double pad = 1.5 * vh, x = r.x + pad, w = r.w - 2 * pad, y = r.y + pad;
	double bottom = r.y + r.h - pad;
	cairo_save(cr);
	rrect(cr, r.x, r.y, r.w, r.h, 14);
	cairo_clip(cr);
	H.r_files = r;
	double fs = clampd(1.2 * vh, 10, 14);

	/* titre + fil d'Ariane */
	double tpx = clampd(1.25 * vh, 10, 15);
	double tw = txt(cr, tpx, PANGO_WEIGHT_SEMIBOLD, 0.3, H.focus ? C_BRIGHT : C_LINE, 1, x, y + tpx * 0.6, 0, 0, "FICHIERS");
	crumbs(cr, x + tw + tpx, y + tpx * 0.6, w - tw - tpx, fs);
	y += tpx * 1.2 + 0.9 * vh;

	/* recherche + boutons */
	double sh = round(fs * 2.2);
	double bx = x + w;
	bx -= button(cr, bx, y, sh, fs, "cd ici", false, &H.r_cdhere) + 4;
	bx -= button(cr, bx, y, sh, fs, H.show_hidden ? "● cachés" : "○ cachés", H.show_hidden, &H.r_hidden) + 6;
	struct rect sr = { x, y, bx - x, sh };
	H.r_search = sr;
	rrect(cr, sr.x, sr.y, sr.w, sr.h, 7);
	cairo_set_source_rgba(cr, 2 / 255.0, 10 / 255.0, 18 / 255.0, 0.9);
	cairo_fill_preserve(cr);
	glow_stroke(cr, H.focus ? C_LINE : C_LINEDIM, 1.1, H.focus ? 0.8 : 0.2);
	double qx = sr.x + fs * 0.7;
	qx += txt(cr, fs * 1.1, PANGO_WEIGHT_NORMAL, 0, C_LINE, 1, qx, sr.y + sh / 2, 0, 0, "⌕") + fs * 0.5;
	double qw = 0, qmax = sr.x + sr.w - qx - fs * 0.6;
	if (H.query[0])
		qw = txt(cr, fs, PANGO_WEIGHT_NORMAL, 0.02, C_BRIGHT, 1, qx, sr.y + sh / 2, 0, qmax, H.query);
	else
		txt(cr, fs * 0.9, PANGO_WEIGHT_NORMAL, 0.02, C_MUTED, 0.8, qx, sr.y + sh / 2, 0, qmax,
		    H.focus ? "filtrer, ou ~/chemin puis ⏎" : "clic ici pour chercher");
	if (H.focus) {
		rgba(cr, C_LINE, 0.9);
		cairo_rectangle(cr, qx + qw + 2, sr.y + sh * 0.25, 1.5, sh * 0.5);
		cairo_fill(cr);
	}
	y += sh + 0.8 * vh;

	/* pied */
	double footer = fs * 1.9;
	double list_bottom = bottom - footer;

	/* raccourcis (colonne de gauche) */
	double pw = round(fmin(w * 0.3, fs * 11)), px = x;
	double prh = fs * 1.75, py = y;
	for (int i = 0; i < H.nplaces; i++) {
		struct place *pl = &H.places[i];
		struct rect pr = { px, py, pw, prh };
		H.r_places[i] = (struct rect){ 0 };
		if (py + prh > list_bottom)
			continue;
		H.r_places[i] = pr;
		bool cur = !strcmp(H.cwd, pl->path), hov = hovering(pr);
		if (cur || hov) {
			rrect(cr, pr.x, pr.y + 1, pr.w, pr.h - 2, 5);
			rgba(cr, C_LINE, cur ? 0.16 : 0.07);
			cairo_fill(cr);
			if (cur) {
				rgba(cr, C_LINE, 1);
				cairo_rectangle(cr, pr.x, pr.y + 4, 2, pr.h - 8);
				cairo_fill(cr);
			}
		}
		txt(cr, fs, PANGO_WEIGHT_NORMAL, 0, cur ? C_BRIGHT : C_LINE, 1, px + fs * 0.9, py + prh / 2, 1, 0, pl->glyph);
		txt(cr, fs * 0.92, cur ? PANGO_WEIGHT_SEMIBOLD : PANGO_WEIGHT_NORMAL, 0.02, cur ? C_BRIGHT : C_TEXT, 1,
		    px + fs * 1.9, py + prh / 2, 0, pw - fs * 2.2, pl->label);
		py += prh;
	}
	rgba(cr, C_LINEDIM, 1);
	cairo_rectangle(cr, px + pw + pad * 0.4, y, 1, list_bottom - y);
	cairo_fill(cr);

	/* liste */
	double lx = px + pw + pad * 0.8, lw = x + w - lx;
	double rh = fs * 1.75;
	H.r_list = (struct rect){ lx, y, lw, list_bottom - y };
	int visible = (int)((list_bottom - y) / rh);
	if (visible < 1)
		visible = 1;
	if (H.snap) {
		if (H.sel < H.scroll)
			H.scroll = H.sel;
		if (H.sel >= H.scroll + visible)
			H.scroll = H.sel - visible + 1;
		H.snap = false;
	}
	if (H.scroll > H.nview - visible)
		H.scroll = H.nview - visible > 0 ? H.nview - visible : 0;
	if (H.scroll < 0)
		H.scroll = 0;
	bool show_date = lw > fs * 34;
	double c_size = show_date ? lx + lw - fs * 9.5 : lx + lw - 6;
	H.nfrows = 0;
	if (H.nview == 0)
		txt(cr, fs * 0.9, PANGO_WEIGHT_NORMAL, 0.03, C_MUTED, 1, lx + lw / 2, y + rh * 1.5, 1, lw - fs,
		    H.query[0] ? (query_is_path() ? "⏎ pour aller à ce chemin" : "Aucun résultat")
			       : access(H.cwd, R_OK) == 0 ? "Dossier vide" : "Accès refusé");
	for (int i = H.scroll; i < H.nview && i < H.scroll + visible && H.nfrows < MAX_FROWS; i++) {
		struct fentry *e = &H.ent[H.view[i]];
		double ry = y + (i - H.scroll) * rh;
		struct rect rc = { lx, ry, lw - 6, rh };
		H.r_frows[H.nfrows] = rc;
		H.frow_idx[H.nfrows++] = i;
		bool sel = i == H.sel, hov = hovering(rc);
		if (sel || hov) {
			rrect(cr, rc.x, rc.y + 1, rc.w, rc.h - 2, 5);
			rgba(cr, C_LINE, sel ? (H.focus ? 0.18 : 0.10) : 0.06);
			cairo_fill(cr);
			if (sel) {
				rgba(cr, C_LINE, H.focus ? 1 : 0.5);
				cairo_rectangle(cr, rc.x, rc.y + 4, 2, rc.h - 8);
				cairo_fill(cr);
			}
		}
		uint32_t gc;
		const char *g = glyph_of(e, &gc);
		txt(cr, fs, PANGO_WEIGHT_NORMAL, 0, gc, e->hidden ? 0.5 : 1, lx + fs * 0.9, ry + rh / 2, 1, 0, g);
		char name[300], sz[24] = "—";
		snprintf(name, sizeof name, "%s%s", e->name, e->dir ? "/" : "");
		if (!e->dir)
			human(e->size, sz, sizeof sz);
		double szw = fs * 0.55 * strlen(sz) + fs;
		txt(cr, fs * 0.95, e->dir ? PANGO_WEIGHT_SEMIBOLD : PANGO_WEIGHT_NORMAL, 0.02,
		    sel ? C_BRIGHT : e->dir ? C_LINE : C_TEXT, e->hidden ? 0.6 : 1, lx + fs * 1.9, ry + rh / 2, 0,
		    c_size - szw - lx - fs * 1.9, name);
		txt(cr, fs * 0.85, PANGO_WEIGHT_NORMAL, 0, C_MUTED, 1, c_size, ry + rh / 2, 2, 0, sz);
		if (show_date) {
			struct tm tm;
			localtime_r(&e->mtime, &tm);
			char dt[24];
			snprintf(dt, sizeof dt, "%02d/%02d/%04d", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900);
			txt(cr, fs * 0.85, PANGO_WEIGHT_NORMAL, 0, C_MUTED, 1, lx + lw - 6, ry + rh / 2, 2, 0, dt);
		}
	}
	/* ascenseur */
	if (H.nview > visible) {
		double lh = list_bottom - y, th = fmax(12, lh * visible / H.nview);
		double ty = y + (lh - th) * H.scroll / (double)(H.nview - visible);
		rgba(cr, C_LINE, 0.6);
		rrect(cr, lx + lw - 3, ty, 3, th, 1.5);
		cairo_fill(cr);
	}

	/* pied */
	double fy = bottom - footer / 2;
	rgba(cr, C_LINEDIM, 1);
	cairo_rectangle(cr, x, list_bottom, w, 1);
	cairo_fill(cr);
	if (H.status[0] && now_s() - H.status_t < 4) {
		txt(cr, fs * 0.85, PANGO_WEIGHT_NORMAL, 0.03, C_WARN, 1, x, fy, 0, w, H.status);
	} else {
		int nd = 0;
		for (int i = 0; i < H.nview; i++)
			nd += H.ent[H.view[i]].dir;
		char info[96];
		snprintf(info, sizeof info, "%d dossier%s · %d fichier%s", nd, nd > 1 ? "s" : "", H.nview - nd,
			 H.nview - nd > 1 ? "s" : "");
		double iw = txt(cr, fs * 0.85, PANGO_WEIGHT_NORMAL, 0.03, C_MUTED, 1, x, fy, 0, 0, info);
		txt(cr, fs * 0.78, PANGO_WEIGHT_NORMAL, 0.03, C_LINEDIM, 1, x + w, fy, 2, w - iw - fs,
		    H.focus ? "↑↓ ⏎ ouvrir · ⌫ parent · Ctrl+H cachés · Échap terminal"
			    : "double-clic : ouvrir · cd ici : le terminal y va");
	}
	cairo_restore(cr);
}

static double kbd_title(cairo_t *cr, struct rect r, double vh)
{
	double pad = 1.5 * vh;
	char right[96];
	snprintf(right, sizeof right, "%s%s%s%s%s · %d frappes", H.layout_name[0] ? H.layout_name : "?",
		 H.sticky_shift ? " · ⇧" : "", H.sticky_ctrl ? " · CTRL" : "", H.last_key[0] ? " · " : "",
		 H.last_key, H.keystrokes);
	return title(cr, r.x + pad, r.y + pad, r.w - 2 * pad, vh, "CLAVIER", right);
}

/* géométrie des touches (dépend seulement de la taille du panneau) */
static void keyboard_geometry(struct rect r, double vh)
{
	double pad = 1.5 * vh, x = r.x + pad, w = r.w - 2 * pad;
	double px = clampd(1.25 * vh, 10, 15);
	double y = r.y + pad + px * 1.2 + 1.2 * vh;
	double ah = r.y + r.h - pad - y;
	double u = fmin(w / 15.0, ah / 5.0);
	double kx = x + (w - u * 15) / 2, ky = y + (ah - u * 5) / 2;
	H.r_kbd = (struct rect){ kx, ky, u * 15, u * 5 };
	double gap = fmax(2, u * 0.1);
	for (int i = 0; i < H.nkeys; i++) {
		struct key *k = &H.keys[i];
		H.r_keys[i] = (struct rect){ kx + k->x * u + gap / 2, ky + k->y * u + gap / 2, k->w * u - gap, u - gap };
	}
}

/* une touche ; « h » = éclat (0 éteinte … 1 vient d'être pressée).
 * opaque : on repeint par-dessus la version éteinte de la couche fixe */
static void draw_key(cairo_t *cr, struct key *k, struct rect kr, double h, bool hov, bool opaque)
{
	double u = H.r_kbd.h / 5;
	double a = 0.035 + 0.40 * h + (hov ? 0.06 : 0);
	rrect(cr, kr.x, kr.y, kr.w, kr.h, u * 0.14);
	if (opaque) {
		/* fond du panneau (≈ 4,10,17) mélangé à la couleur des lignes */
		double br = 4 / 255.0, bg = 10 / 255.0, bb = 17 / 255.0;
		cairo_set_source_rgb(cr, br + (0x4f / 255.0 - br) * a, bg + (0xb8 / 255.0 - bg) * a, bb + (0xe0 / 255.0 - bb) * a);
	} else {
		rgba(cr, C_LINE, a);
	}
	cairo_fill_preserve(cr);
	if (h > 0.05)
		glow_stroke(cr, h > 0.6 ? C_BRIGHT : C_LINE, 1.1, h);
	else {
		rgba(cr, hov ? C_LINE : C_LINEDIM, 1);
		cairo_set_line_width(cr, 1);
		cairo_stroke(cr);
	}
	uint32_t col = h > 0.5 ? C_BG : k->special ? C_MUTED : C_TEXT;
	if (k->label[0]) {
		double fs = k->special ? u * (strlen(k->label) > 4 ? 0.22 : 0.3) : u * 0.38;
		txt(cr, fs, k->special ? PANGO_WEIGHT_SEMIBOLD : PANGO_WEIGHT_NORMAL, k->special ? 0.08 : 0, col, 1,
		    kr.x + kr.w / 2, kr.y + kr.h * (k->shift_label[0] ? 0.62 : 0.5), 1, kr.w - 3, k->label);
	}
	if (k->shift_label[0] && u > 18)
		txt(cr, u * 0.24, PANGO_WEIGHT_NORMAL, 0, h > 0.5 ? C_BG : C_MUTED, 1, kr.x + u * 0.14,
		    kr.y + kr.h * 0.27, 0, 0, k->shift_label);
}

static double key_heat(struct key *k)
{
	double h = k->heat;
	if ((H.sticky_shift && (k->code == KEY_LEFTSHIFT || k->code == KEY_RIGHTSHIFT)) ||
	    (H.sticky_ctrl && k->code == KEY_LEFTCTRL))
		h = fmax(h, 0.7);
	return h;
}

static bool key_hovered(int i)
{
	struct rect kr = H.r_keys[i];
	return A.px >= kr.x && A.px <= kr.x + kr.w && A.py >= kr.y && A.py <= kr.y + kr.h;
}

static void background(cairo_t *cr, int W, int H_)
{
	rgba(cr, C_BG, 1);
	cairo_paint(cr);
	cairo_save(cr);
	cairo_translate(cr, W * 0.5, H_ * 0.4);
	cairo_scale(cr, W * 0.65, H_ * 0.7);
	cairo_pattern_t *g = cairo_pattern_create_radial(0, 0, 0, 0, 0, 1);
	cairo_pattern_add_color_stop_rgba(g, 0, 30 / 255.0, 90 / 255.0, 130 / 255.0, 0.13);
	cairo_pattern_add_color_stop_rgba(g, 1, 30 / 255.0, 90 / 255.0, 130 / 255.0, 0);
	cairo_set_source(cr, g);
	cairo_paint(cr);
	cairo_pattern_destroy(g);
	cairo_restore(cr);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.012);
	for (int y = 0; y < H_; y += 3)
		cairo_rectangle(cr, 0, y, W, 1);
	cairo_fill(cr);
}

/* fond du panneau terminal (le cadre lumineux est dessiné par render.c) */
static void term_backdrop(cairo_t *cr, struct rect r)
{
	rrect(cr, r.x, r.y, r.w, r.h, 12);
	cairo_set_source_rgba(cr, 1 / 255.0, 4 / 255.0, 8 / 255.0, 0.92);
	cairo_fill(cr);
}

/* couche fixe : fond, cadres lumineux (recalculée seulement si la taille change) */
static cairo_surface_t *static_layer;

void hud_draw_back(cairo_t *cr, int W, int H_)
{
	if (!H.cache || H.cache_w != W || H.cache_h != H_ || static_dirty) {
		if (H.cache)
			cairo_surface_destroy(H.cache);
		if (static_layer)
			cairo_surface_destroy(static_layer);
		H.cache = cairo_image_surface_create(CAIRO_FORMAT_RGB24, W, H_);
		static_layer = cairo_image_surface_create(CAIRO_FORMAT_RGB24, W, H_);
		H.cache_w = W;
		H.cache_h = H_;
		static_dirty = false;
		struct layout L = lay(W, H_);
		cairo_t *c = cairo_create(static_layer);
		background(c, W, H_);
		panel(c, L.left);
		panel(c, L.right);
		panel(c, L.files);
		panel(c, L.kbd);
		struct rect ta = hud_term_area(W, H_);
		term_backdrop(c, (struct rect){ ta.x + 6, ta.y + 6, ta.w - 12, ta.h - 12 });
		keyboard_geometry(L.kbd, L.vh);
		for (int i = 0; i < H.nkeys; i++)
			draw_key(c, &H.keys[i], H.r_keys[i], 0, false, false);
		cairo_destroy(c);
		H.cache_dirty = true;
	}
	if (H.cache_dirty) {
		struct layout L = lay(W, H_);
		cairo_t *c = cairo_create(H.cache);
		cairo_set_operator(c, CAIRO_OPERATOR_SOURCE);
		cairo_set_source_surface(c, static_layer, 0, 0);
		cairo_paint(c);
		cairo_set_operator(c, CAIRO_OPERATOR_OVER);
		header(c, L.head, L.vh);
		system_panel(c, L.left, L.vh);
		network_panel(c, L.right, L.vh);
		files_panel(c, L.files, L.vh);
		kbd_title(c, L.kbd, L.vh);
		cairo_destroy(c);
		H.cache_dirty = false;
	}
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_surface(cr, H.cache, 0, 0);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	/* ouverture : chaque panneau s'allume l'un après l'autre */
	double dt = now_s() - H.intro_t;
	if (dt < 1.1) {
		struct layout L = lay(W, H_);
		struct rect rs[4] = { L.left, L.right, L.files, L.kbd };
		for (int i = 0; i < 4; i++) {
			double k = clampd((dt - 0.1 * i) / 0.6, 0, 1);
			if (k >= 1)
				continue;
			rrect(cr, rs[i].x - 4, rs[i].y - 4, rs[i].w + 8, rs[i].h + 8, 16);
			rgba(cr, C_BG, 1 - k);
			cairo_fill(cr);
			if (k > 0) {
				rrect(cr, rs[i].x, rs[i].y, rs[i].w, rs[i].h, 14);
				cairo_set_source_rgba(cr, 0.75, 0.93, 1.0, 0.35 * (1 - k));
				cairo_fill(cr);
			}
		}
	}
}

/* par-dessus : seulement les touches allumées, collantes ou survolées */
void hud_draw_front(cairo_t *cr, int W, int H_)
{
	(void)W; (void)H_;
	for (int i = 0; i < H.nkeys; i++) {
		struct key *k = &H.keys[i];
		double h = key_heat(k);
		bool hov = key_hovered(i);
		if (h > 0.01 || hov)
			draw_key(cr, k, H.r_keys[i], h, hov, true);
	}
}

/* ------------------------------ entrées ------------------------------ */

static bool in_rect(struct rect r, double x, double y)
{
	return r.w > 0 && x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

bool hud_click(double x, double y, uint32_t button)
{
	if (button != BTN_LEFT)
		return false;
	A.dirty = true;
	H.cache_dirty = true;
	if (in_rect(H.r_search, x, y)) {
		hud_focus_files(true);
		return true;
	}
	if (in_rect(H.r_hidden, x, y)) {
		H.show_hidden = !H.show_hidden;
		refilter();
		return true;
	}
	if (in_rect(H.r_cdhere, x, y)) {
		type_cd(H.cwd);
		return true;
	}
	for (int i = 0; i < H.ncrumbs; i++)
		if (in_rect(H.r_crumbs[i], x, y)) {
			files_chdir(H.crumb_path[i]);
			return true;
		}
	for (int i = 0; i < H.nplaces; i++)
		if (in_rect(H.r_places[i], x, y)) {
			files_chdir(H.places[i].path);
			return true;
		}
	for (int i = 0; i < H.nfrows; i++)
		if (in_rect(H.r_frows[i], x, y)) {
			int vi = H.frow_idx[i];
			double tn = now_s();
			hud_focus_files(true);
			if (vi == H.last_click_idx && tn - H.last_click_t < 0.45) {
				H.last_click_idx = -1;
				activate(vi); /* double-clic : ouvrir */
			} else {
				H.sel = vi;
				H.last_click_idx = vi;
				H.last_click_t = tn;
			}
			return true;
		}
	if (in_rect(H.r_files, x, y)) {
		hud_focus_files(true);
		return true;
	}
	for (int i = 0; i < H.nkeys; i++)
		if (in_rect(H.r_keys[i], x, y)) {
			click_key(&H.keys[i]);
			return true;
		}
	return false;
}

bool hud_scroll(double x, double y, int dir)
{
	if (!in_rect(H.r_files, x, y))
		return false;
	H.scroll += dir * 2;
	if (H.scroll < 0)
		H.scroll = 0;
	H.cache_dirty = true;
	return true;
}

/* le survol change-t-il l'affichage des panneaux en cache ? */
bool hud_hover_changed(double ox, double oy, double x, double y)
{
	bool was = in_rect(H.r_files, ox, oy), now = in_rect(H.r_files, x, y);
	if (was || now) {
		H.cache_dirty = true;
		return true;
	}
	return in_rect(H.r_kbd, ox, oy) || in_rect(H.r_kbd, x, y);
}

void hud_tick(void)
{
	perf_update(&H.perf);
	sys_update(&H.sys);
	files_follow();
	H.cache_dirty = true;
}

void hud_init(void)
{
	H.intro_t = now_s();
	static bool done;
	if (done)
		return;
	done = true;
	H.last_click_idx = -1;
	perf_init(&H.perf);
	sys_update(&H.sys);
	keyboard_build();
	places_init();
	files_follow();
	if (!H.cwd[0]) {
		const char *home = getenv("HOME");
		files_chdir(home ? home : "/");
	}
	H.cache_dirty = true;
}
