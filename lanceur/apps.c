/* plume-lanceur — applications installées (.desktop), icônes, recherche, lancement. */
#include "lanceur.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <librsvg/rsvg.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* ----------------------------- outils -------------------------------- */

static char *trim(char *s)
{
	while (*s == ' ' || *s == '\t')
		s++;
	char *e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r'))
		*--e = '\0';
	return s;
}

static bool in_path(const char *prog)
{
	if (strchr(prog, '/'))
		return access(prog, X_OK) == 0;
	const char *path = getenv("PATH");
	char buf[PATH_MAX];
	for (const char *p = path ? path : "/usr/local/bin:/usr/bin:/bin"; *p;) {
		size_t n = strcspn(p, ":");
		snprintf(buf, sizeof buf, "%.*s/%s", (int)n, p, prog);
		if (access(buf, X_OK) == 0)
			return true;
		p += n + (p[n] == ':');
	}
	return false;
}

/* langue de l'utilisateur : « fr_FR », « fr » */
static char lang_full[16], lang_short[8];

static void init_lang(void)
{
	const char *l = getenv("LC_ALL");
	if (!l || !*l) l = getenv("LC_MESSAGES");
	if (!l || !*l) l = getenv("LANG");
	if (!l || !*l) l = "fr_FR"; /* Plume parle français */
	snprintf(lang_full, sizeof lang_full, "%.*s", (int)strcspn(l, ".@"), l);
	snprintf(lang_short, sizeof lang_short, "%.*s", (int)strcspn(l, "_.@"), l);
}

/* ------------------------- usages (fréquence) ------------------------- */

static char usage_path[PATH_MAX];

static void usage_load(void)
{
	const char *home = getenv("HOME");
	snprintf(usage_path, sizeof usage_path, "%s/.cache/plume-lanceur/usage", home ? home : "/tmp");
	FILE *f = fopen(usage_path, "r");
	char line[256];
	while (f && fgets(line, sizeof line, f)) {
		int n;
		char id[200];
		if (sscanf(line, "%d %199s", &n, id) != 2)
			continue;
		for (int i = 0; i < L.napps; i++)
			if (!strcmp(L.apps[i].id, id))
				L.apps[i].uses = n;
	}
	if (f)
		fclose(f);
}

static void usage_save(void)
{
	char dir[PATH_MAX];
	snprintf(dir, sizeof dir, "%s", usage_path);
	char *sl = strrchr(dir, '/');
	if (sl) {
		*sl = '\0';
		char *p = strrchr(dir, '/');
		if (p) {
			*p = '\0';
			mkdir(dir, 0755); /* ~/.cache */
			*p = '/';
		}
		mkdir(dir, 0755);
	}
	FILE *f = fopen(usage_path, "w");
	if (!f)
		return;
	for (int i = 0; i < L.napps; i++)
		if (L.apps[i].uses > 0)
			fprintf(f, "%d %s\n", L.apps[i].uses, L.apps[i].id);
	fclose(f);
}

/* ----------------------------- .desktop ------------------------------ */

static bool listed(const char *list, const char *what)
{
	/* « GNOME;KDE; » contient-il « GNOME » ? */
	char buf[256];
	snprintf(buf, sizeof buf, ";%s;", list);
	char k[64];
	snprintf(k, sizeof k, ";%s;", what);
	return strcasestr(buf, k) != NULL;
}

/* garde la meilleure traduction : Nom[fr_FR] > Nom[fr] > Nom */
static void localized(const char *key, const char *lk, const char *val, const char *field, char *out, size_t cap,
		      int *rank)
{
	if (strcmp(key, field))
		return;
	int r = !lk ? 1 : !strcmp(lk, lang_full) ? 3 : !strcmp(lk, lang_short) ? 2 : 0;
	if (r > *rank) {
		*rank = r;
		snprintf(out, cap, "%s", val);
	}
}

static void parse_desktop(const char *path, const char *id)
{
	for (int i = 0; i < L.napps; i++)
		if (!strcmp(L.apps[i].id, id))
			return; /* déjà vu dans un dossier prioritaire */
	FILE *f = fopen(path, "r");
	if (!f)
		return;
	struct app_entry a;
	memset(&a, 0, sizeof a);
	snprintf(a.id, sizeof a.id, "%s", id);
	char line[1024];
	bool in_entry = false, is_app = false, hidden = false;
	int rn = 0, rg = 0, rc = 0, rk = 0;
	char only[256] = "", notshow[256] = "", tryexec[256] = "";
	while (fgets(line, sizeof line, f)) {
		char *s = trim(line);
		if (s[0] == '[') {
			in_entry = !strcmp(s, "[Desktop Entry]");
			continue;
		}
		if (!in_entry || !*s || *s == '#')
			continue;
		char *eq = strchr(s, '=');
		if (!eq)
			continue;
		*eq = '\0';
		char *key = trim(s), *val = trim(eq + 1);
		char *lb = strchr(key, '['), *lk = NULL;
		if (lb) {
			*lb = '\0';
			lk = lb + 1;
			char *rb = strchr(lk, ']');
			if (rb)
				*rb = '\0';
			lk[strcspn(lk, ".@")] = '\0';
		}
		localized(key, lk, val, "Name", a.name, sizeof a.name, &rn);
		localized(key, lk, val, "GenericName", a.generic, sizeof a.generic, &rg);
		localized(key, lk, val, "Comment", a.comment, sizeof a.comment, &rc);
		localized(key, lk, val, "Keywords", a.keywords, sizeof a.keywords, &rk);
		if (lk)
			continue;
		if (!strcmp(key, "Type")) is_app = !strcmp(val, "Application");
		else if (!strcmp(key, "Exec")) snprintf(a.exec, sizeof a.exec, "%s", val);
		else if (!strcmp(key, "Icon")) snprintf(a.icon, sizeof a.icon, "%s", val);
		else if (!strcmp(key, "Terminal")) a.terminal = !strcmp(val, "true");
		else if (!strcmp(key, "NoDisplay") || !strcmp(key, "Hidden")) hidden |= !strcmp(val, "true");
		else if (!strcmp(key, "OnlyShowIn")) snprintf(only, sizeof only, "%s", val);
		else if (!strcmp(key, "NotShowIn")) snprintf(notshow, sizeof notshow, "%s", val);
		else if (!strcmp(key, "TryExec")) snprintf(tryexec, sizeof tryexec, "%s", val);
	}
	fclose(f);
	if (!is_app || hidden || !a.name[0] || !a.exec[0])
		return;
	/* réservé à un autre bureau (réglages GNOME, KDE…) */
	if (only[0] && !listed(only, "sway") && !listed(only, "wlroots") && !listed(only, "Plume"))
		return;
	if (notshow[0] && (listed(notshow, "sway") || listed(notshow, "Plume")))
		return;
	if (tryexec[0] && !in_path(tryexec))
		return;
	if (L.napps < MAX_APPS)
		L.apps[L.napps++] = a;
}

static void scan_dir(const char *dir, const char *prefix)
{
	DIR *d = opendir(dir);
	if (!d)
		return;
	struct dirent *e;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.')
			continue;
		char path[PATH_MAX], id[PATH_MAX];
		snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
		snprintf(id, sizeof id, "%s%s", prefix, e->d_name);
		struct stat st;
		if (stat(path, &st) != 0)
			continue;
		if (S_ISDIR(st.st_mode)) {
			char sub[PATH_MAX];
			snprintf(sub, sizeof sub, "%s-", id); /* kde/app.desktop -> kde-app.desktop */
			scan_dir(path, sub);
		} else {
			size_t n = strlen(e->d_name);
			if (n > 8 && !strcmp(e->d_name + n - 8, ".desktop"))
				parse_desktop(path, id);
		}
	}
	closedir(d);
}

void apps_load(void)
{
	init_lang();
	const char *home = getenv("HOME");
	char p[PATH_MAX];
	/* du plus prioritaire au moins prioritaire */
	if (home) {
		snprintf(p, sizeof p, "%s/.local/share/applications", home);
		scan_dir(p, "");
		snprintf(p, sizeof p, "%s/.local/share/flatpak/exports/share/applications", home);
		scan_dir(p, "");
	}
	scan_dir("/var/lib/flatpak/exports/share/applications", "");
	const char *xdd = getenv("XDG_DATA_DIRS");
	if (!xdd || !*xdd)
		xdd = "/usr/local/share:/usr/share";
	for (const char *s = xdd; *s;) {
		size_t n = strcspn(s, ":");
		snprintf(p, sizeof p, "%.*s/applications", (int)n, s);
		scan_dir(p, "");
		s += n + (s[n] == ':');
	}
	usage_load();
	apps_filter();
}

/* ---------------------------- recherche ------------------------------ */

static int score(const struct app_entry *a, const char *q)
{
	if (!*q)
		return 1;
	if (!strncasecmp(a->name, q, strlen(q))) return 100;
	/* début d'un mot du nom */
	for (const char *p = a->name; (p = strchr(p, ' ')); p++)
		if (!strncasecmp(p + 1, q, strlen(q))) return 80;
	if (strcasestr(a->name, q)) return 60;
	if (strcasestr(a->generic, q)) return 40;
	if (strcasestr(a->keywords, q)) return 30;
	if (strcasestr(a->comment, q)) return 20;
	if (strcasestr(a->exec, q)) return 10;
	return 0;
}

static int g_scores[MAX_APPS];

static int cmp_view(const void *x, const void *y)
{
	int i = *(const int *)x, j = *(const int *)y;
	if (g_scores[i] != g_scores[j])
		return g_scores[j] - g_scores[i];
	if (L.query[0] && L.apps[i].uses != L.apps[j].uses)
		return L.apps[j].uses - L.apps[i].uses; /* recherche : les plus utilisées d'abord */
	return strcasecmp(L.apps[i].name, L.apps[j].name);
}

void apps_filter(void)
{
	L.nview = 0;
	for (int i = 0; i < L.napps; i++) {
		g_scores[i] = score(&L.apps[i], L.query);
		if (g_scores[i] > 0)
			L.view[L.nview++] = i;
	}
	qsort(L.view, L.nview, sizeof L.view[0], cmp_view);
	L.sel = 0;
	L.page = 0;
	L.dirty = true;
}

/* ------------------------------ icônes ------------------------------- */

static cairo_surface_t *load_png(const char *path, int size)
{
	cairo_surface_t *src = cairo_image_surface_create_from_png(path);
	if (cairo_surface_status(src) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(src);
		return NULL;
	}
	int w = cairo_image_surface_get_width(src), h = cairo_image_surface_get_height(src);
	cairo_surface_t *dst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
	cairo_t *cr = cairo_create(dst);
	double sc = (double)size / (w > h ? w : h);
	cairo_translate(cr, (size - w * sc) / 2, (size - h * sc) / 2);
	cairo_scale(cr, sc, sc);
	cairo_set_source_surface(cr, src, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
	cairo_paint(cr);
	cairo_destroy(cr);
	cairo_surface_destroy(src);
	return dst;
}

static cairo_surface_t *load_svg(const char *path, int size)
{
	GError *err = NULL;
	RsvgHandle *h = rsvg_handle_new_from_file(path, &err);
	if (!h) {
		if (err)
			g_error_free(err);
		return NULL;
	}
	cairo_surface_t *dst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
	cairo_t *cr = cairo_create(dst);
	RsvgRectangle vp = { 0, 0, size, size };
	bool ok = rsvg_handle_render_document(h, cr, &vp, &err);
	cairo_destroy(cr);
	g_object_unref(h);
	if (err)
		g_error_free(err);
	if (!ok) {
		cairo_surface_destroy(dst);
		return NULL;
	}
	return dst;
}

static cairo_surface_t *load_icon_file(const char *path, int size)
{
	size_t n = strlen(path);
	if (n > 4 && !strcasecmp(path + n - 4, ".png"))
		return load_png(path, size);
	if (n > 4 && (!strcasecmp(path + n - 4, ".svg") || (n > 5 && !strcasecmp(path + n - 5, ".svgz"))))
		return load_svg(path, size);
	return NULL;
}

/* thème d'icônes choisi dans GTK (s'il y en a un) */
static const char *gtk_icon_theme(void)
{
	static char theme[64];
	static bool done;
	if (done)
		return theme;
	done = true;
	const char *home = getenv("HOME");
	char p[PATH_MAX];
	snprintf(p, sizeof p, "%s/.config/gtk-3.0/settings.ini", home ? home : "");
	FILE *f = fopen(p, "r");
	char line[256];
	while (f && fgets(line, sizeof line, f))
		if (!strncmp(line, "gtk-icon-theme-name", 19)) {
			char *eq = strchr(line, '=');
			if (eq)
				snprintf(theme, sizeof theme, "%s", trim(eq + 1));
		}
	if (f)
		fclose(f);
	return theme;
}

static bool find_icon(const char *name, char *out, size_t cap)
{
	if (name[0] == '/') {
		snprintf(out, cap, "%s", name);
		return access(out, R_OK) == 0;
	}
	const char *home = getenv("HOME");
	char bases[6][PATH_MAX];
	int nb = 0;
	if (home) {
		snprintf(bases[nb++], PATH_MAX, "%s/.local/share/icons", home);
		snprintf(bases[nb++], PATH_MAX, "%s/.local/share/flatpak/exports/share/icons", home);
	}
	snprintf(bases[nb++], PATH_MAX, "/var/lib/flatpak/exports/share/icons");
	snprintf(bases[nb++], PATH_MAX, "/usr/local/share/icons");
	snprintf(bases[nb++], PATH_MAX, "/usr/share/icons");
	const char *themes[] = { gtk_icon_theme(), "hicolor", "Adwaita" };
	const char *sizes[] = { "scalable", "256x256", "512x512", "128x128", "96x96", "64x64", "48x48", "32x32" };
	const char *exts[] = { ".svg", ".png" };
	for (size_t t = 0; t < 3; t++) {
		if (!themes[t][0])
			continue;
		for (size_t s = 0; s < sizeof sizes / sizeof sizes[0]; s++)
			for (int b = 0; b < nb; b++)
				for (size_t e = 0; e < 2; e++) {
					snprintf(out, cap, "%s/%s/%s/apps/%s%s", bases[b], themes[t], sizes[s], name, exts[e]);
					if (access(out, R_OK) == 0)
						return true;
				}
	}
	for (size_t e = 0; e < 2; e++) {
		snprintf(out, cap, "/usr/share/pixmaps/%s%s", name, exts[e]);
		if (access(out, R_OK) == 0)
			return true;
	}
	return false;
}

cairo_surface_t *app_icon(struct app_entry *a, int size)
{
	if (a->icon_surf && cairo_image_surface_get_width(a->icon_surf) == size)
		return a->icon_surf;
	if (a->icon_tried && !a->icon_surf)
		return NULL;
	a->icon_tried = true;
	if (a->icon_surf) {
		cairo_surface_destroy(a->icon_surf);
		a->icon_surf = NULL;
	}
	char path[PATH_MAX];
	if (a->icon[0] && find_icon(a->icon, path, sizeof path))
		a->icon_surf = load_icon_file(path, size);
	return a->icon_surf;
}

/* ----------------------------- lancement ----------------------------- */

void app_launch(struct app_entry *a)
{
	/* retire les codes %f %U … du champ Exec */
	char cmd[1024];
	size_t k = 0;
	for (const char *p = a->exec; *p && k < sizeof cmd - 1; p++) {
		if (*p == '%' && p[1]) {
			p++;
			if (*p == '%')
				cmd[k++] = '%';
			continue;
		}
		cmd[k++] = *p;
	}
	cmd[k] = '\0';
	char full[1200];
	if (a->terminal)
		snprintf(full, sizeof full, "exec plume-terminal sh -c '%s'", cmd);
	else
		snprintf(full, sizeof full, "exec %s", cmd);
	a->uses++;
	usage_save();
	pid_t p = fork();
	if (p == 0) {
		setsid();
		for (int s = 1; s < NSIG; s++)
			signal(s, SIG_DFL);
		if (fork() == 0) {
			execl("/bin/sh", "sh", "-c", full, (char *)NULL);
			_exit(127);
		}
		_exit(0);
	}
	if (p > 0)
		while (waitpid(p, NULL, 0) < 0 && errno == EINTR)
			;
}
