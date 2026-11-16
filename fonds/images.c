/* plume-fonds — fonds disponibles, miniatures, application et mémorisation du choix. */
#include "fonds.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *MODE_NAME[] = { "fill", "fit", "center", "tile" };

static const struct { const char *name; uint32_t c; } COLORS[] = {
	{ "Nuit HUD", 0x02060b }, { "Noir", 0x000000 }, { "Bleu nuit", 0x0b1a2e }, { "Ardoise", 0x1e2733 },
	{ "Violet", 0x1d1330 }, { "Forêt", 0x0c1f17 }, { "Bordeaux", 0x2a0f16 }, { "Gris clair", 0x9aa7b4 },
};

static const struct { const char *file, *name; } PRESETS[] = {
	{ "horizon.png", "Horizon" }, { "aurore.png", "Aurore" }, { "constellation.png", "Constellation" },
	{ "circuit.png", "Circuit" }, { "topographie.png", "Topographie" }, { "nebuleuse.png", "Nébuleuse" },
};

/* ----------------------------- chemins ------------------------------- */

/* choix enregistré : ~/.config/plume/fond.conf (« fond=… » et « mode=… »).
 * Il n'est PAS lu par sway directement : une image disparue casserait sa
 * configuration. sway lance « plume-fonds --appliquer », qui vérifie d'abord. */
static void conf_path(char *out, size_t cap)
{
	const char *home = getenv("HOME");
	snprintf(out, cap, "%s/.config/plume/fond.conf", home ? home : "/tmp");
}

#define DEFAULT_WALL "/usr/share/plume/wallpaper.png"

/* lit le choix : val = chemin ou « #rrggbb », mode = fill/fit/… */
static void read_conf(char *val, size_t vcap, char *mode, size_t mcap)
{
	val[0] = mode[0] = '\0';
	char p[PATH_MAX];
	conf_path(p, sizeof p);
	FILE *f = fopen(p, "r");
	char line[PATH_MAX + 32];
	while (f && fgets(line, sizeof line, f)) {
		line[strcspn(line, "\n")] = '\0';
		if (!strncmp(line, "fond=", 5))
			snprintf(val, vcap, "%s", line + 5);
		else if (!strncmp(line, "mode=", 5))
			snprintf(mode, mcap, "%s", line + 5);
	}
	if (f)
		fclose(f);
}

static const char *presets_dir(void)
{
	const char *env = getenv("PLUME_FONDS_DIR");
	if (env && *env)
		return env;
	static const char *dirs[] = { "/usr/local/share/plume/fonds", "/usr/share/plume/fonds" };
	for (size_t i = 0; i < 2; i++)
		if (access(dirs[i], R_OK) == 0)
			return dirs[i];
	return dirs[0];
}

/* ---------------------------- chargement ----------------------------- */

static struct item *add(enum kind k, const char *name, const char *path, uint32_t color)
{
	if (A.nitems >= MAX_ITEMS)
		return NULL;
	struct item *it = &A.items[A.nitems++];
	memset(it, 0, sizeof *it);
	it->kind = k;
	snprintf(it->name, sizeof it->name, "%s", name);
	if (path)
		snprintf(it->path, sizeof it->path, "%s", path);
	it->color = color;
	return it;
}

static bool is_image(const char *name)
{
	const char *d = strrchr(name, '.');
	if (!d)
		return false;
	static const char *ext[] = { ".jpg", ".jpeg", ".png", ".webp", ".bmp", ".tif", ".tiff", ".svg" };
	for (size_t i = 0; i < sizeof ext / sizeof ext[0]; i++)
		if (!strcasecmp(d, ext[i]))
			return true;
	return false;
}

struct found {
	char path[PATH_MAX];
	time_t mtime;
};
static struct found found[MAX_ITEMS];
static int nfound;

static void scan(const char *dir, int depth)
{
	DIR *d = opendir(dir);
	if (!d)
		return;
	struct dirent *e;
	while ((e = readdir(d)) && nfound < MAX_ITEMS) {
		if (e->d_name[0] == '.')
			continue;
		char p[PATH_MAX];
		snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
		struct stat st;
		if (stat(p, &st) != 0)
			continue;
		if (S_ISDIR(st.st_mode)) {
			if (depth > 0)
				scan(p, depth - 1);
		} else if (is_image(e->d_name) && st.st_size > 2048) {
			snprintf(found[nfound].path, sizeof found[0].path, "%s", p);
			found[nfound++].mtime = st.st_mtime;
		}
	}
	closedir(d);
}

static int cmp_found(const void *a, const void *b)
{
	const struct found *x = a, *y = b;
	return x->mtime < y->mtime ? 1 : x->mtime > y->mtime ? -1 : 0;
}

/* fond appliqué : on relit ~/.config/sway/fond.conf */
static void read_current(void)
{
	A.current = -1;
	A.mode = M_FILL;
	char val[PATH_MAX], mode[32];
	read_conf(val, sizeof val, mode, sizeof mode);
	if (!val[0]) /* rien de choisi : le fond d'origine de Plume */
		snprintf(val, sizeof val, DEFAULT_WALL);
	for (int m = 0; m < 4; m++)
		if (!strcmp(mode, MODE_NAME[m]))
			A.mode = (enum mode)m;
	for (int i = 0; i < A.nitems; i++) {
		struct item *it = &A.items[i];
		if (it->kind == K_COLOR) {
			char hex[16];
			snprintf(hex, sizeof hex, "#%06x", it->color);
			if (!strcasecmp(hex, val))
				A.current = i;
		} else if (!strcmp(it->path, val)) {
			A.current = i;
		}
	}
	/* image choisie ailleurs : on l'ajoute à la liste */
	if (A.current < 0 && val[0] == '/' && access(val, R_OK) == 0) {
		const char *b = strrchr(val, '/');
		if (add(K_IMAGE, b ? b + 1 : val, val, 0))
			A.current = A.nitems - 1;
	}
}

void items_load(void)
{
	/* fonds Plume */
	if (access("/usr/share/plume/wallpaper.png", R_OK) == 0)
		add(K_PRESET, "Plume HUD", "/usr/share/plume/wallpaper.png", 0);
	const char *dir = presets_dir();
	for (size_t i = 0; i < sizeof PRESETS / sizeof PRESETS[0]; i++) {
		char p[PATH_MAX];
		snprintf(p, sizeof p, "%s/%s", dir, PRESETS[i].file);
		if (access(p, R_OK) == 0)
			add(K_PRESET, PRESETS[i].name, p, 0);
	}
	/* couleurs */
	for (size_t i = 0; i < sizeof COLORS / sizeof COLORS[0]; i++)
		add(K_COLOR, COLORS[i].name, NULL, COLORS[i].c);
	/* images de l'utilisateur, les plus récentes d'abord */
	const char *home = getenv("HOME");
	if (home) {
		static const char *sub[] = { "Images", "Bureau", "Téléchargements", "Pictures", "Desktop", "Downloads" };
		for (size_t i = 0; i < sizeof sub / sizeof sub[0]; i++) {
			char p[PATH_MAX];
			snprintf(p, sizeof p, "%s/%s", home, sub[i]);
			scan(p, 2);
		}
		qsort(found, nfound, sizeof found[0], cmp_found);
		for (int i = 0; i < nfound; i++) {
			const char *b = strrchr(found[i].path, '/');
			add(K_IMAGE, b ? b + 1 : found[i].path, found[i].path, 0);
		}
	}
	read_current();
}

/* ----------------------------- miniatures ---------------------------- */

/* image redimensionnée : « cover » remplit (et recadre), sinon tient dedans */
cairo_surface_t *load_scaled(const char *path, int w, int h, bool cover, int *iw, int *ih)
{
	int fw = 0, fh = 0;
	if (!gdk_pixbuf_get_file_info(path, &fw, &fh) || fw <= 0 || fh <= 0)
		return NULL;
	if (iw) *iw = fw;
	if (ih) *ih = fh;
	double sc = cover ? fmax((double)w / fw, (double)h / fh) : fmin((double)w / fw, (double)h / fh);
	int lw = (int)(fw * sc + 0.5), lh = (int)(fh * sc + 0.5);
	if (lw < 1) lw = 1;
	if (lh < 1) lh = 1;
	GError *err = NULL;
	GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_scale(path, lw, lh, FALSE, &err);
	if (!pb) {
		if (err)
			g_error_free(err);
		return NULL;
	}
	/* pixbuf (RGB/RGBA) -> surface cairo (ARGB prémultiplié) */
	int pw = gdk_pixbuf_get_width(pb), ph = gdk_pixbuf_get_height(pb);
	int nch = gdk_pixbuf_get_n_channels(pb), stride = gdk_pixbuf_get_rowstride(pb);
	const guchar *src = gdk_pixbuf_get_pixels(pb);
	cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
	unsigned char *dst = cairo_image_surface_get_data(s);
	int dstride = cairo_image_surface_get_stride(s);
	for (int y = 0; y < ph; y++) {
		const guchar *sp = src + y * stride;
		uint32_t *dp = (uint32_t *)(dst + y * dstride);
		for (int x = 0; x < pw; x++, sp += nch) {
			uint32_t a = nch == 4 ? sp[3] : 255;
			uint32_t r = sp[0] * a / 255, g = sp[1] * a / 255, b = sp[2] * a / 255;
			dp[x] = a << 24 | r << 16 | g << 8 | b;
		}
	}
	cairo_surface_mark_dirty(s);
	g_object_unref(pb);
	return s;
}

/* une miniature à la fois (la fenêtre reste fluide) ; renvoie true s'il en reste */
bool load_one_thumb(void)
{
	for (int pass = 0; pass < 2; pass++)
		for (int i = 0; i < A.nitems; i++) {
			struct item *it = &A.items[i];
			if (it->kind == K_COLOR || it->thumb_tried || (pass == 0 && !it->visible))
				continue;
			it->thumb_tried = true;
			it->thumb = load_scaled(it->path, THUMB_W, THUMB_H, true, &it->img_w, &it->img_h);
			return true;
		}
	return false;
}

/* ----------------------------- appliquer ----------------------------- */

static bool run_swaymsg(const char *cmd)
{
	pid_t p = fork();
	if (p == 0) {
		int fd = open("/dev/null", 1);
		if (fd >= 0) {
			dup2(fd, 1);
			dup2(fd, 2);
		}
		execlp("swaymsg", "swaymsg", cmd, (char *)NULL);
		_exit(127);
	}
	int st = 0;
	if (p < 0)
		return false;
	while (waitpid(p, &st, 0) < 0 && errno == EINTR)
		;
	return WIFEXITED(st) && WEXITSTATUS(st) == 0;
}

static void bg_value(const struct item *it, char *out, size_t cap)
{
	if (it->kind == K_COLOR)
		snprintf(out, cap, "#%06x solid_color", it->color);
	else
		snprintf(out, cap, "\"%s\" %s", it->path, MODE_NAME[A.mode]);
}

static void save(const struct item *it)
{
	char p[PATH_MAX], tmp[PATH_MAX + 8];
	conf_path(p, sizeof p);
	snprintf(tmp, sizeof tmp, "%s.tmp", p);
	/* crée ~/.config/plume et ses parents si besoin */
	char dir[PATH_MAX];
	snprintf(dir, sizeof dir, "%s", p);
	for (char *c = dir + 1; *c; c++)
		if (*c == '/') {
			*c = '\0';
			mkdir(dir, 0755);
			*c = '/';
		}
	FILE *f = fopen(tmp, "w");
	if (!f)
		return;
	fprintf(f, "# Plume — fond d'écran choisi avec plume-fonds (appliqué par « plume-fonds --appliquer »)\n");
	if (it->kind == K_COLOR)
		fprintf(f, "fond=#%06x\n", it->color);
	else
		fprintf(f, "fond=%s\nmode=%s\n", it->path, MODE_NAME[A.mode]);
	fclose(f);
	rename(tmp, p);
}

void apply_item(int idx)
{
	if (idx < 0 || idx >= A.nitems)
		return;
	struct item *it = &A.items[idx];
	char val[PATH_MAX + 32], cmd[PATH_MAX + 64];
	bg_value(it, val, sizeof val);
	snprintf(cmd, sizeof cmd, "output * bg %s", val);
	if (!run_swaymsg(cmd)) {
		set_toast(true, "Impossible d'appliquer le fond (sway ne répond pas)");
		return;
	}
	A.current = idx;
	save(it);
	set_toast(false, "Fond d'écran : %s", it->name);
}

void apply_mode(enum mode m)
{
	A.mode = m;
	if (A.current >= 0 && A.items[A.current].kind != K_COLOR)
		apply_item(A.current);
	A.preview_for = -2; /* aperçu à refaire */
}

/* au démarrage de sway : réapplique le fond enregistré, s'il existe encore */
int apply_saved(void)
{
	char val[PATH_MAX], mode[32], cmd[PATH_MAX + 64];
	read_conf(val, sizeof val, mode, sizeof mode);
	bool ok_mode = false;
	for (int m = 0; m < 4; m++)
		ok_mode |= !strcmp(mode, MODE_NAME[m]);
	if (!ok_mode)
		snprintf(mode, sizeof mode, "fill");
	if (val[0] == '#' && strlen(val) == 7)
		snprintf(cmd, sizeof cmd, "output * bg %s solid_color", val);
	else if (val[0] == '/' && access(val, R_OK) == 0)
		snprintf(cmd, sizeof cmd, "output * bg \"%s\" %s", val, mode);
	else if (access(DEFAULT_WALL, R_OK) == 0)
		snprintf(cmd, sizeof cmd, "output * bg %s fill", DEFAULT_WALL); /* image disparue : fond Plume */
	else
		snprintf(cmd, sizeof cmd, "output * bg #02060b solid_color");
	return run_swaymsg(cmd) ? 0 : 1;
}
