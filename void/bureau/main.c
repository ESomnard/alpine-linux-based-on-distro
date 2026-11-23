/*
 * plume-icones-bureau — les fichiers du dossier Bureau, directement sur le bureau (Plume Void).
 *
 *   clic : sélectionner (Ctrl : ajouter / retirer)      double-clic, Entrée : ouvrir
 *   glisser dans le vide (bouton gauche OU droit) : sélectionner un groupe, comme sur Mac
 *   glisser une icône : la déplacer (les places sont gardées)
 *   clic droit sur une icône : Ouvrir · Renommer · Dupliquer · Copier le chemin · Mettre à la corbeille
 *   clic droit dans le vide : Nouveau dossier · Nouveau document · Ranger · Ouvrir dans Fichiers ·
 *                             Changer le fond d'écran
 *   Suppr : corbeille · F2 : renommer · Ctrl+A : tout · Échap : rien · Espace : aperçu rapide
 *   glisser-déposer (comme sur Mac) : depuis Fichiers vers le bureau (déplacé ; copié s'il vient d'un
 *   autre disque), du bureau vers Fichiers ou une appli, sur un dossier du bureau pour l'y ranger
 *   Copier / Couper / Coller (menu, Ctrl+C / Ctrl+X / Ctrl+V) : même presse-papiers que Fichiers
 *   clés USB et disques externes : affichés sur le bureau, clic droit > Éjecter
 *
 * Surface wlr-layer-shell sur la couche « bottom » : au-dessus du fond d'écran, sous les fenêtres.
 * Le dossier est surveillé (inotify) : un fichier enregistré sur le Bureau apparaît tout de suite.
 */
#define _GNU_SOURCE
#include <cairo.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <librsvg/rsvg.h>
#include <limits.h>
#include <linux/input-event-codes.h>
#include <locale.h>
#include <math.h>
#include <pango/pangocairo.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/inotify.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#pragma GCC diagnostic ignored "-Wformat-truncation" /* noms et chemins coupés : voulu */

#define CELL_W 104
#define CELL_H 112
#define MARGIN 18
#define ICON 56
#define MAX_ITEMS 400
#define FONT "Inter, DejaVu Sans"
#define ACCENT 0x4fb8e0

/* ------------------------------ état -------------------------------- */

enum kind { K_FILE, K_DIR, K_APP, K_IMAGE, K_DISK };

struct item {
	char name[256];         /* nom du fichier */
	char label[256];        /* affiché (nom de l'appli pour un .desktop) */
	char ext[12];
	char exec[512], icon[256];
	char mnt[512];          /* disque externe : point de montage (sinon vide) */
	enum kind kind;
	int col, row;           /* place dans la grille (col 0 = à droite) */
	bool sel, placed;
	cairo_surface_t *img;   /* vignette / icône d'appli */
	bool img_tried;
};

enum drag { D_NONE, D_PRESS_ICON, D_MOVE, D_PRESS_EMPTY, D_BAND };

struct menu_entry { char label[48]; int act; bool sep; };
enum { A_OPEN = 1, A_RENAME, A_DUP, A_PATH, A_TRASH, A_NEWDIR, A_NEWDOC, A_SORT, A_FILES, A_WALL, A_EJECT, A_PREVIEW, A_COPY, A_CUT, A_PASTE };

struct buffer { struct wl_buffer *wl; void *data; size_t size; cairo_surface_t *cs; int w, h; bool busy; };
/* ce qu'une autre appli propose (glisser-déposer ou presse-papiers) */
struct offer_info { bool uri, gnome; uint32_t action; };

static struct {
	struct wl_display *display;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct wl_seat *seat;
	struct wl_pointer *pointer;
	struct wl_keyboard *keyboard;
	struct zwlr_layer_shell_v1 *layer_shell;
	uint32_t layer_ver;
	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer;
	struct wl_callback *frame_cb;
	struct buffer buffers[2];
	int width, height;
	bool configured, dirty, running;

	struct xkb_context *xkb;
	struct xkb_keymap *keymap;
	struct xkb_state *xkb_state;

	char dir[PATH_MAX];     /* dossier du Bureau */
	char posfile[PATH_MAX]; /* places des icônes */
	struct item items[MAX_ITEMS];
	int n;
	int hover;

	double px, py;
	enum drag drag;
	uint32_t drag_button;
	double ax, ay;          /* où le bouton a été enfoncé */
	int press_item;
	bool ctrl;
	double last_click_t;
	int last_click_item;

	bool menu_open;
	double mx, my;
	struct menu_entry menu[12];
	int nmenu, menu_hover;
	int menu_col, menu_row; /* case du clic droit (nouveau dossier à cet endroit) */

	int rename_idx;         /* -1 : pas de renommage en cours */
	char rename_buf[256];
	bool rename_fresh;      /* nom encore tout sélectionné : la 1re touche le remplace (comme sur Mac) */

	int ino;
	bool ino_reload;        /* relire le dossier (fichiers partis par glisser-déposer) */

	/* glisser-déposer (wl_data_device) */
	struct wl_data_device_manager *ddm;
	uint32_t ddm_ver;
	struct wl_data_device *dd;
	uint32_t press_serial;
	struct wl_data_offer *offer;      /* ce qu'on glisse au-dessus du bureau */
	struct wl_data_offer *drop_offer; /* ce qui vient d'être lâché (lecture en cours) */
	int drop_fd;
	char *drop_buf;
	size_t drop_len;
	double drop_x, drop_y;
	int drop_target;                  /* dossier ou disque survolé pendant un glisser (-1 : aucun) */
	int drop_dest;                    /* le même, gardé au moment du lâcher (« leave » suit le lâcher) */
	struct wl_data_source *src;       /* glisser parti du bureau */
	bool dragging_out;
	struct wl_surface *drag_icon;
	struct buffer drag_buf;
	pid_t preview_pid;                /* aperçu rapide (Espace) */
	uint32_t input_serial;            /* dernier clic ou touche (pour prendre le presse-papiers) */
	struct wl_data_source *clip;      /* ce que le bureau a copié */
	char clip_text[16384];            /* « copy » ou « cut », puis les adresses file:// */
	struct wl_data_offer *sel_offer;  /* ce que contient le presse-papiers (venant d'ailleurs) */
	int paste_fd;
	char *paste_buf;
	size_t paste_len;
	bool paste_gnome;                 /* format « x-special/gnome-copied-files » (copy/cut en 1re ligne) */
	int mounts_fd;                    /* /proc/self/mounts : clés USB branchées / retirées */
} B = { .drop_fd = -1, .drop_target = -1, .mounts_fd = -1, .paste_fd = -1, .hover = -1, .press_item = -1, .rename_idx = -1, .menu_hover = -1, .last_click_item = -1, .ino = -1 };

static double now_s(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void rgba(cairo_t *cr, uint32_t c, double a)
{
	cairo_set_source_rgba(cr, ((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0, (c & 255) / 255.0, a);
}

static void rrect(cairo_t *cr, double x, double y, double w, double h, double r)
{
	cairo_new_sub_path(cr);
	cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
	cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
	cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
	cairo_arc(cr, x + r, y + r, r, M_PI, 1.5 * M_PI);
	cairo_close_path(cr);
}

/* ---------------------------- lancement ------------------------------ */

static void run_argv(char *const argv[])
{
	pid_t p = fork();
	if (p == 0) {
		setsid();
		if (fork() == 0) {
			execvp(argv[0], argv);
			_exit(127);
		}
		_exit(0);
	}
	if (p > 0)
		waitpid(p, NULL, 0);
}

static void run_sh(const char *cmd)
{
	char *argv[] = { "sh", "-c", (char *)cmd, NULL };
	run_argv(argv);
}

static void item_path(const struct item *it, char *out, size_t cap)
{
	if (it->mnt[0])
		snprintf(out, cap, "%s", it->mnt);
	else
		snprintf(out, cap, "%s/%s", B.dir, it->name);
}

static void open_item(struct item *it)
{
	char p[PATH_MAX];
	item_path(it, p, sizeof p);
	if (it->kind == K_APP && it->exec[0]) {
		/* Exec sans les codes %U, %f… */
		char cmd[512];
		size_t k = 0;
		for (const char *s = it->exec; *s && k < sizeof cmd - 1; s++) {
			if (*s == '%' && s[1]) {
				s++;
				continue;
			}
			cmd[k++] = *s;
		}
		cmd[k] = '\0';
		run_sh(cmd);
		return;
	}
	char *argv[] = { "xdg-open", p, NULL };
	run_argv(argv);
}

/* ------------------------------ fichiers ----------------------------- */

static const char *IMG_EXT[] = { "png", "jpg", "jpeg", "webp", "gif", "bmp", "svg", "avif", NULL };

static void find_desktop_dir(void)
{
	const char *home = getenv("HOME");
	if (!home)
		home = "/tmp";
	B.dir[0] = '\0';
	/* ~/.config/user-dirs.dirs : XDG_DESKTOP_DIR="$HOME/Bureau" */
	char p[PATH_MAX];
	snprintf(p, sizeof p, "%s/.config/user-dirs.dirs", home);
	FILE *f = fopen(p, "r");
	char l[PATH_MAX];
	while (f && fgets(l, sizeof l, f)) {
		if (strncmp(l, "XDG_DESKTOP_DIR=\"", 17))
			continue;
		char *v = l + 17, *e = strchr(v, '"');
		if (e)
			*e = '\0';
		if (!strncmp(v, "$HOME", 5))
			snprintf(B.dir, sizeof B.dir, "%s%s", home, v + 5);
		else
			snprintf(B.dir, sizeof B.dir, "%s", v);
	}
	if (f)
		fclose(f);
	struct stat st;
	if (!B.dir[0] || stat(B.dir, &st) || !S_ISDIR(st.st_mode)) {
		snprintf(B.dir, sizeof B.dir, "%s/Bureau", home);
		if (stat(B.dir, &st) || !S_ISDIR(st.st_mode)) {
			char d2[PATH_MAX];
			snprintf(d2, sizeof d2, "%s/Desktop", home);
			if (!stat(d2, &st) && S_ISDIR(st.st_mode))
				snprintf(B.dir, sizeof B.dir, "%s", d2);
			else
				mkdir(B.dir, 0755);
		}
	}
	snprintf(p, sizeof p, "%s/.config", home);
	mkdir(p, 0755);
	snprintf(p, sizeof p, "%s/.config/plume", home);
	mkdir(p, 0755);
	snprintf(B.posfile, sizeof B.posfile, "%s/.config/plume/bureau-places", home);
}

static void read_desktop_file(struct item *it, const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return;
	char l[1024];
	bool entry = false, app = false;
	char name[256] = "", exec[512] = "", icon[256] = "";
	while (fgets(l, sizeof l, f)) {
		l[strcspn(l, "\r\n")] = '\0';
		if (l[0] == '[') {
			entry = !strcmp(l, "[Desktop Entry]");
			continue;
		}
		if (!entry)
			continue;
		if (!strncmp(l, "Name=", 5) && !name[0]) snprintf(name, sizeof name, "%s", l + 5);
		else if (!strncmp(l, "Name[fr]=", 9)) snprintf(name, sizeof name, "%s", l + 9);
		else if (!strncmp(l, "Exec=", 5)) snprintf(exec, sizeof exec, "%s", l + 5);
		else if (!strncmp(l, "Icon=", 5)) snprintf(icon, sizeof icon, "%s", l + 5);
		else if (!strcmp(l, "Type=Application")) app = true;
	}
	fclose(f);
	if (app && exec[0]) {
		it->kind = K_APP;
		snprintf(it->exec, sizeof it->exec, "%s", exec);
		snprintf(it->icon, sizeof it->icon, "%s", icon);
		if (name[0])
			snprintf(it->label, sizeof it->label, "%s", name);
	}
}

static void places_save(void)
{
	char tmp[PATH_MAX + 8];
	snprintf(tmp, sizeof tmp, "%s.tmp", B.posfile);
	FILE *f = fopen(tmp, "w");
	if (!f)
		return;
	for (int i = 0; i < B.n; i++)
		fprintf(f, "%d\t%d\t%s\n", B.items[i].col, B.items[i].row, B.items[i].name);
	fclose(f);
	rename(tmp, B.posfile);
}

static int grid_cols(void) { int c = (B.width - 2 * MARGIN) / CELL_W; return c < 1 ? 1 : c; }
static int grid_rows(void) { int r = (B.height - 2 * MARGIN) / CELL_H; return r < 1 ? 1 : r; }

static int item_at_cell(int col, int row, int skip)
{
	for (int i = 0; i < B.n; i++)
		if (i != skip && B.items[i].placed && B.items[i].col == col && B.items[i].row == row)
			return i;
	return -1;
}

/* 1re case libre : colonnes de droite à gauche, de haut en bas (comme sur Mac) */
static void place_free(struct item *it)
{
	int rows = grid_rows(), cols = grid_cols();
	for (int c = 0; c < cols + 50; c++)
		for (int r = 0; r < rows; r++)
			if (item_at_cell(c, r, -1) < 0) {
				it->col = c;
				it->row = r;
				it->placed = true;
				return;
			}
}

static void free_item(struct item *it)
{
	if (it->img)
		cairo_surface_destroy(it->img);
	it->img = NULL;
}

static int cmp_label(const void *a, const void *b)
{
	const struct item *x = a, *y = b;
	if ((x->kind == K_DIR) != (y->kind == K_DIR))
		return x->kind == K_DIR ? -1 : 1;
	return strcasecmp(x->label, y->label);
}

static void load_items(void)
{
	/* garder la sélection et les vignettes des fichiers qui restent */
	static struct item old[MAX_ITEMS];
	int nold = B.n;
	memcpy(old, B.items, sizeof(struct item) * nold);
	B.n = 0;
	DIR *d = opendir(B.dir);
	struct dirent *e;
	while (d && (e = readdir(d)) && B.n < MAX_ITEMS) {
		if (e->d_name[0] == '.')
			continue;
		struct item *it = &B.items[B.n];
		memset(it, 0, sizeof *it);
		snprintf(it->name, sizeof it->name, "%s", e->d_name);
		snprintf(it->label, sizeof it->label, "%s", e->d_name);
		char p[PATH_MAX];
		item_path(it, p, sizeof p);
		struct stat st;
		if (stat(p, &st))
			continue;
		const char *dot = strrchr(it->name, '.');
		if (dot && dot != it->name && strlen(dot + 1) < sizeof it->ext) {
			snprintf(it->ext, sizeof it->ext, "%s", dot + 1);
			for (char *c = it->ext; *c; c++)
				if (*c >= 'A' && *c <= 'Z') *c += 32;
		}
		if (S_ISDIR(st.st_mode))
			it->kind = K_DIR;
		else if (!strcmp(it->ext, "desktop"))
			read_desktop_file(it, p);
		else
			for (int k = 0; IMG_EXT[k]; k++)
				if (!strcmp(it->ext, IMG_EXT[k]))
					it->kind = K_IMAGE;
		for (int j = 0; j < nold; j++)
			if (!strcmp(old[j].name, it->name)) {
				it->sel = old[j].sel;
				it->img = old[j].img;
				it->img_tried = old[j].img_tried;
				old[j].img = NULL;
				break;
			}
		B.n++;
	}
	if (d)
		closedir(d);
	/* clés USB et disques externes montés (udiskie) : /run/media/<toi>/<nom> */
	const char *user = getenv("USER");
	char media[300];
	snprintf(media, sizeof media, "/run/media/%s", user ? user : "");
	struct stat pst;
	DIR *md = user && !stat(media, &pst) ? opendir(media) : NULL;
	while (md && (e = readdir(md)) && B.n < MAX_ITEMS) {
		if (e->d_name[0] == '.')
			continue;
		struct item *it = &B.items[B.n];
		memset(it, 0, sizeof *it);
		snprintf(it->mnt, sizeof it->mnt, "%s/%s", media, e->d_name);
		struct stat st;
		if (stat(it->mnt, &st) || !S_ISDIR(st.st_mode) || st.st_dev == pst.st_dev)
			continue; /* pas (ou plus) monté */
		it->kind = K_DISK;
		snprintf(it->name, sizeof it->name, "disque:%s", e->d_name);
		snprintf(it->label, sizeof it->label, "%s", e->d_name);
		for (int j = 0; j < nold; j++)
			if (!strcmp(old[j].name, it->name))
				it->sel = old[j].sel;
		B.n++;
	}
	if (md)
		closedir(md);
	for (int j = 0; j < nold; j++)
		free_item(&old[j]);
	/* places enregistrées, puis les nouveaux dans les cases libres */
	FILE *f = fopen(B.posfile, "r");
	char l[600];
	while (f && fgets(l, sizeof l, f)) {
		l[strcspn(l, "\n")] = '\0';
		int c, r, k = 0;
		if (sscanf(l, "%d\t%d\t%n", &c, &r, &k) < 2 || !k)
			continue;
		for (int i = 0; i < B.n; i++)
			if (!B.items[i].placed && !strcmp(B.items[i].name, l + k) && item_at_cell(c, r, i) < 0) {
				B.items[i].col = c;
				B.items[i].row = r;
				B.items[i].placed = true;
			}
	}
	if (f)
		fclose(f);
	for (int i = 0; i < B.n; i++)
		if (!B.items[i].placed)
			place_free(&B.items[i]);
	places_save();
	if (B.rename_idx >= B.n)
		B.rename_idx = -1;
	B.hover = -1;
	B.dirty = true;
}

static void sort_items(void)
{
	qsort(B.items, B.n, sizeof B.items[0], cmp_label);
	for (int i = 0; i < B.n; i++)
		B.items[i].placed = false;
	for (int i = 0; i < B.n; i++)
		place_free(&B.items[i]);
	places_save();
	B.dirty = true;
}

/* nom libre dans DIR : « Nouveau dossier », « Nouveau dossier 2 »… */
static void unique_in(const char *dir, const char *base, const char *ext, char *out, size_t cap)
{
	struct stat st;
	for (int i = 1; i < 1000; i++) {
		char p[PATH_MAX];
		if (i == 1)
			snprintf(out, cap, "%s%s", base, ext);
		else
			snprintf(out, cap, "%s %d%s", base, i, ext);
		snprintf(p, sizeof p, "%s/%s", dir, out);
		if (lstat(p, &st))
			return;
	}
}

static void unique_name(const char *base, const char *ext, char *out, size_t cap)
{
	unique_in(B.dir, base, ext, out, cap);
}

/* corbeille freedesktop : ~/.local/share/Trash/{files,info} */
static void url_encode(const char *s, char *out, size_t cap)
{
	size_t k = 0;
	for (; *s && k + 4 < cap; s++) {
		unsigned char c = (unsigned char)*s;
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr("/-_.~", c))
			out[k++] = c;
		else
			k += snprintf(out + k, cap - k, "%%%02X", c);
	}
	out[k] = '\0';
}

static void trash_item(struct item *it)
{
	const char *home = getenv("HOME");
	if (!home)
		return;
	char base[PATH_MAX], files[PATH_MAX], info[PATH_MAX];
	snprintf(base, sizeof base, "%s/.local/share/Trash", home);
	snprintf(files, sizeof files, "%s/files", base);
	snprintf(info, sizeof info, "%s/info", base);
	char p[PATH_MAX];
	snprintf(p, sizeof p, "%s/.local", home); mkdir(p, 0700);
	snprintf(p, sizeof p, "%s/.local/share", home); mkdir(p, 0700);
	mkdir(base, 0700); mkdir(files, 0700); mkdir(info, 0700);
	char name[256], dst[PATH_MAX], inf[PATH_MAX];
	struct stat st;
	snprintf(name, sizeof name, "%s", it->name);
	for (int i = 2; i < 1000; i++) {
		snprintf(dst, sizeof dst, "%s/%s", files, name);
		snprintf(inf, sizeof inf, "%s/%s.trashinfo", info, name);
		if (stat(dst, &st) && stat(inf, &st))
			break;
		snprintf(name, sizeof name, "%s.%d", it->name, i);
	}
	char src[PATH_MAX], enc[PATH_MAX * 3];
	item_path(it, src, sizeof src);
	url_encode(src, enc, sizeof enc);
	FILE *f = fopen(inf, "w");
	if (!f)
		return;
	time_t t = time(NULL);
	struct tm tm;
	localtime_r(&t, &tm);
	char date[32];
	strftime(date, sizeof date, "%Y-%m-%dT%H:%M:%S", &tm);
	fprintf(f, "[Trash Info]\nPath=%s\nDeletionDate=%s\n", enc, date);
	fclose(f);
	if (rename(src, dst) != 0) {
		if (errno == EXDEV) { /* autre disque : mv copie puis efface */
			char *argv[] = { "mv", "--", src, dst, NULL };
			pid_t pid = fork();
			if (pid == 0) {
				execvp("mv", argv);
				_exit(127);
			}
			int stt = 0;
			waitpid(pid, &stt, 0);
			if (WIFEXITED(stt) && !WEXITSTATUS(stt))
				return;
		}
		unlink(inf);
	}
}

static void duplicate_item(struct item *it)
{
	char base[256], ext[16] = "", dst[256];
	snprintf(base, sizeof base, "%s", it->name);
	char *dot = it->kind == K_DIR ? NULL : strrchr(base, '.');
	if (dot && dot != base) {
		snprintf(ext, sizeof ext, "%s", dot);
		*dot = '\0';
	}
	char b2[300];
	snprintf(b2, sizeof b2, "%s copie", base);
	unique_name(b2, ext, dst, sizeof dst);
	char src[PATH_MAX], d[PATH_MAX];
	item_path(it, src, sizeof src);
	snprintf(d, sizeof d, "%s/%s", B.dir, dst);
	char *argv[] = { "cp", "-a", "--", src, d, NULL };
	pid_t pid = fork();
	if (pid == 0) {
		execvp("cp", argv);
		_exit(127);
	}
	waitpid(pid, NULL, 0);
}

static void copy_path(void)
{
	char all[8192] = "";
	size_t k = 0;
	for (int i = 0; i < B.n; i++)
		if (B.items[i].sel) {
			char p[PATH_MAX];
			item_path(&B.items[i], p, sizeof p);
			k += snprintf(all + k, sizeof all - k, "%s%s", k ? "\n" : "", p);
		}
	int fd[2];
	if (!k || pipe(fd))
		return;
	pid_t pid = fork();
	if (pid == 0) {
		dup2(fd[0], 0);
		close(fd[0]);
		close(fd[1]);
		execlp("wl-copy", "wl-copy", (char *)NULL);
		_exit(127);
	}
	close(fd[0]);
	if (write(fd[1], all, k) < 0) { /* rien */ }
	close(fd[1]);
	waitpid(pid, NULL, 0);
}

/* ------------------------------ icônes ------------------------------- */

static cairo_surface_t *scale_into(cairo_surface_t *src, int w, int h, int size)
{
	cairo_surface_t *dst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
	cairo_t *cr = cairo_create(dst);
	double sc = (double)size / (w > h ? w : h);
	cairo_translate(cr, (size - w * sc) / 2, (size - h * sc) / 2);
	cairo_scale(cr, sc, sc);
	cairo_set_source_surface(cr, src, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
	cairo_paint(cr);
	cairo_destroy(cr);
	return dst;
}

/* image -> surface cairo (gdk-pixbuf : png, jpeg, webp…) */
static cairo_surface_t *load_pixbuf(const char *path, int size)
{
	GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_scale(path, size, size, TRUE, NULL);
	if (!pb)
		return NULL;
	GdkPixbuf *rot = gdk_pixbuf_apply_embedded_orientation(pb);
	if (rot) {
		g_object_unref(pb);
		pb = rot;
	}
	int w = gdk_pixbuf_get_width(pb), h = gdk_pixbuf_get_height(pb), n = gdk_pixbuf_get_n_channels(pb);
	int rs = gdk_pixbuf_get_rowstride(pb);
	const guchar *px = gdk_pixbuf_get_pixels(pb);
	cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	unsigned char *d = cairo_image_surface_get_data(s);
	int ds = cairo_image_surface_get_stride(s);
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			const guchar *p = px + y * rs + x * n;
			unsigned a = n == 4 ? p[3] : 255;
			uint32_t *o = (uint32_t *)(d + y * ds + x * 4);
			*o = (a << 24) | ((p[0] * a / 255) << 16) | ((p[1] * a / 255) << 8) | (p[2] * a / 255);
		}
	cairo_surface_mark_dirty(s);
	g_object_unref(pb);
	return s;
}

static cairo_surface_t *load_svg(const char *path, int size)
{
	RsvgHandle *h = rsvg_handle_new_from_file(path, NULL);
	if (!h)
		return NULL;
	cairo_surface_t *dst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
	cairo_t *cr = cairo_create(dst);
	RsvgRectangle vp = { 0, 0, size, size };
	bool ok = rsvg_handle_render_document(h, cr, &vp, NULL);
	cairo_destroy(cr);
	g_object_unref(h);
	if (!ok) {
		cairo_surface_destroy(dst);
		return NULL;
	}
	return dst;
}

static bool find_icon(const char *name, char *out, size_t cap)
{
	if (name[0] == '/') {
		snprintf(out, cap, "%s", name);
		return access(out, R_OK) == 0;
	}
	const char *home = getenv("HOME");
	char bases[5][PATH_MAX];
	int nb = 0;
	if (home) {
		snprintf(bases[nb++], PATH_MAX, "%s/.local/share/icons", home);
		snprintf(bases[nb++], PATH_MAX, "%s/.local/share/flatpak/exports/share/icons", home);
	}
	snprintf(bases[nb++], PATH_MAX, "/var/lib/flatpak/exports/share/icons");
	snprintf(bases[nb++], PATH_MAX, "/usr/share/icons");
	const char *themes[] = { "hicolor", "Adwaita", "Papirus" };
	const char *sizes[] = { "scalable", "256x256", "128x128", "96x96", "64x64", "48x48" };
	const char *exts[] = { ".svg", ".png" };
	for (size_t t = 0; t < 3; t++)
		for (size_t s = 0; s < sizeof sizes / sizeof sizes[0]; s++)
			for (int b = 0; b < nb; b++)
				for (size_t e = 0; e < 2; e++) {
					snprintf(out, cap, "%s/%s/%s/apps/%s%s", bases[b], themes[t], sizes[s], name, exts[e]);
					if (access(out, R_OK) == 0)
						return true;
				}
	for (size_t e = 0; e < 2; e++) {
		snprintf(out, cap, "/usr/share/pixmaps/%s%s", name, exts[e]);
		if (access(out, R_OK) == 0)
			return true;
	}
	return false;
}

static cairo_surface_t *item_image(struct item *it)
{
	if (it->img_tried)
		return it->img;
	it->img_tried = true;
	char p[PATH_MAX];
	if (it->kind == K_IMAGE) {
		item_path(it, p, sizeof p);
		cairo_surface_t *s = !strcmp(it->ext, "svg") ? load_svg(p, ICON * 2) : load_pixbuf(p, ICON * 2);
		if (s) {
			int w = cairo_image_surface_get_width(s), h = cairo_image_surface_get_height(s);
			it->img = scale_into(s, w, h, ICON * 2);
			cairo_surface_destroy(s);
		}
	} else if (it->kind == K_APP && it->icon[0] && find_icon(it->icon, p, sizeof p)) {
		size_t n = strlen(p);
		it->img = n > 4 && !strcasecmp(p + n - 4, ".svg") ? load_svg(p, ICON * 2) : load_pixbuf(p, ICON * 2);
	}
	return it->img;
}

/* dossier façon Mac, en couleurs Plume */
static void draw_folder(cairo_t *cr, double x, double y, double s)
{
	double w = s, h = s * 0.78, top = y + (s - h) / 2 + 2;
	cairo_pattern_t *g = cairo_pattern_create_linear(0, top, 0, top + h);
	cairo_pattern_add_color_stop_rgb(g, 0, 0.36, 0.72, 0.96);
	cairo_pattern_add_color_stop_rgb(g, 1, 0.16, 0.50, 0.86);
	/* onglet */
	rrect(cr, x + 2, top - 6, w * 0.42, 14, 4);
	cairo_set_source(cr, g);
	cairo_fill(cr);
	/* dos */
	rrect(cr, x + 2, top, w - 4, h - 4, 6);
	cairo_set_source(cr, g);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
	/* face avant, plus claire */
	cairo_pattern_t *f = cairo_pattern_create_linear(0, top + 8, 0, top + h);
	cairo_pattern_add_color_stop_rgb(f, 0, 0.55, 0.84, 1.0);
	cairo_pattern_add_color_stop_rgb(f, 1, 0.30, 0.66, 0.95);
	rrect(cr, x + 2, top + 9, w - 4, h - 13, 6);
	cairo_set_source(cr, f);
	cairo_fill(cr);
	cairo_pattern_destroy(f);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.35);
	cairo_rectangle(cr, x + 6, top + 10, w - 12, 1.2);
	cairo_fill(cr);
}

/* disque externe : boîtier argenté avec un voyant */
static void draw_disk(cairo_t *cr, double x, double y, double s)
{
	double w = s, h = s * 0.62, top = y + (s - h) / 2 + 4;
	cairo_pattern_t *g = cairo_pattern_create_linear(0, top, 0, top + h);
	cairo_pattern_add_color_stop_rgb(g, 0, 0.93, 0.95, 0.97);
	cairo_pattern_add_color_stop_rgb(g, 1, 0.66, 0.70, 0.76);
	rrect(cr, x + 3, top, w - 6, h, 8);
	cairo_set_source(cr, g);
	cairo_fill_preserve(cr);
	cairo_pattern_destroy(g);
	cairo_set_source_rgba(cr, 0, 0, 0, 0.25);
	cairo_set_line_width(cr, 1);
	cairo_stroke(cr);
	cairo_set_source_rgba(cr, 0, 0, 0, 0.18);
	cairo_rectangle(cr, x + 10, top + h * 0.62, w - 20, 1.5);
	cairo_fill(cr);
	cairo_arc(cr, x + w - 14, top + h * 0.80, 2.6, 0, 2 * M_PI);
	cairo_set_source_rgb(cr, 0.31, 0.88, 0.64);
	cairo_fill(cr);
}

/* document : feuille blanche au coin plié + bande de couleur avec l'extension */
static void draw_document(cairo_t *cr, double x, double y, double s, const char *ext)
{
	static const struct { const char *e; uint32_t c; } COL[] = {
		{ "pdf", 0xe5484d }, { "txt", 0x7b8794 }, { "md", 0x7b8794 }, { "odt", 0x2f7de1 }, { "doc", 0x2f7de1 },
		{ "docx", 0x2f7de1 }, { "ods", 0x22a55b }, { "xls", 0x22a55b }, { "xlsx", 0x22a55b }, { "csv", 0x22a55b },
		{ "odp", 0xf08c2e }, { "ppt", 0xf08c2e }, { "pptx", 0xf08c2e }, { "zip", 0xd9a21b }, { "tar", 0xd9a21b },
		{ "gz", 0xd9a21b }, { "xz", 0xd9a21b }, { "7z", 0xd9a21b }, { "mp3", 0xe0569b }, { "ogg", 0xe0569b },
		{ "flac", 0xe0569b }, { "wav", 0xe0569b }, { "mp4", 0xf06a3c }, { "mkv", 0xf06a3c }, { "webm", 0xf06a3c },
		{ "avi", 0xf06a3c }, { "sh", 0x2fb27a }, { "py", 0x3572a5 }, { "c", 0x5c6bc0 }, { "html", 0xe8663d },
		{ "iso", 0x8d6e63 }, { NULL, 0x4fb8e0 },
	};
	uint32_t c = 0x4fb8e0;
	for (int i = 0; COL[i].e; i++)
		if (!strcmp(COL[i].e, ext))
			c = COL[i].c;
	double w = s * 0.74, h = s, x0 = x + (s - w) / 2, fold = w * 0.28;
	cairo_new_path(cr);
	cairo_move_to(cr, x0 + 4, y);
	cairo_line_to(cr, x0 + w - fold, y);
	cairo_line_to(cr, x0 + w, y + fold);
	cairo_line_to(cr, x0 + w, y + h - 4);
	cairo_arc(cr, x0 + w - 4, y + h - 4, 4, 0, M_PI / 2);
	cairo_line_to(cr, x0 + 4, y + h);
	cairo_arc(cr, x0 + 4, y + h - 4, 4, M_PI / 2, M_PI);
	cairo_line_to(cr, x0, y + 4);
	cairo_arc(cr, x0 + 4, y + 4, 4, M_PI, 1.5 * M_PI);
	cairo_close_path(cr);
	cairo_set_source_rgb(cr, 0.97, 0.98, 0.99);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0, 0, 0, 0.18);
	cairo_set_line_width(cr, 1);
	cairo_stroke(cr);
	/* coin plié */
	cairo_move_to(cr, x0 + w - fold, y);
	cairo_line_to(cr, x0 + w - fold, y + fold - 3);
	cairo_arc_negative(cr, x0 + w - fold + 3, y + fold - 3, 3, M_PI, M_PI / 2);
	cairo_line_to(cr, x0 + w, y + fold);
	cairo_close_path(cr);
	cairo_set_source_rgb(cr, 0.82, 0.85, 0.89);
	cairo_fill(cr);
	/* lignes de texte */
	cairo_set_source_rgba(cr, 0, 0, 0, 0.12);
	for (int i = 0; i < 3; i++)
		cairo_rectangle(cr, x0 + 7, y + fold + 6 + i * 6, w - 14 - (i == 2 ? 10 : 0), 2);
	cairo_fill(cr);
	/* bande avec l'extension */
	if (ext[0]) {
		char up[12];
		size_t k = 0;
		for (; ext[k] && k < 5; k++)
			up[k] = ext[k] >= 'a' && ext[k] <= 'z' ? ext[k] - 32 : ext[k];
		up[k] = '\0';
		rrect(cr, x0 - 4, y + h - 22, w + 8, 15, 3);
		rgba(cr, c, 1);
		cairo_fill(cr);
		PangoLayout *l = pango_cairo_create_layout(cr);
		PangoFontDescription *fd = pango_font_description_from_string("DejaVu Sans Bold");
		pango_font_description_set_absolute_size(fd, 9 * PANGO_SCALE);
		pango_layout_set_font_description(l, fd);
		pango_font_description_free(fd);
		pango_layout_set_text(l, up, -1);
		int tw, th;
		pango_layout_get_pixel_size(l, &tw, &th);
		cairo_move_to(cr, x0 + w / 2 - tw / 2.0, y + h - 22 + (15 - th) / 2.0);
		cairo_set_source_rgb(cr, 1, 1, 1);
		pango_cairo_show_layout(cr, l);
		g_object_unref(l);
	}
}

/* ------------------------------ dessin ------------------------------- */

static void cell_xy(int col, int row, double *x, double *y)
{
	*x = B.width - MARGIN - (col + 1) * CELL_W;
	*y = MARGIN + row * CELL_H;
}

/* zone de l'icône elle-même (pour les clics et la sélection en groupe) */
static void icon_rect(const struct item *it, double dx, double dy, double *x, double *y, double *w, double *h)
{
	double cx, cy;
	cell_xy(it->col, it->row, &cx, &cy);
	*x = cx + 8 + dx;
	*y = cy + 2 + dy;
	*w = CELL_W - 16;
	*h = CELL_H - 6;
}

static int item_at(double px, double py)
{
	for (int i = B.n - 1; i >= 0; i--) {
		double x, y, w, h;
		icon_rect(&B.items[i], 0, 0, &x, &y, &w, &h);
		if (px >= x && px <= x + w && py >= y && py <= y + h)
			return i;
	}
	return -1;
}

static void draw_label(cairo_t *cr, const char *s, double cx, double y, bool sel, bool editing)
{
	PangoLayout *l = pango_cairo_create_layout(cr);
	PangoFontDescription *fd = pango_font_description_from_string(FONT);
	pango_font_description_set_absolute_size(fd, 12 * PANGO_SCALE);
	if (sel)
		pango_font_description_set_weight(fd, PANGO_WEIGHT_SEMIBOLD);
	pango_layout_set_font_description(l, fd);
	pango_font_description_free(fd);
	pango_layout_set_width(l, (CELL_W - 6) * PANGO_SCALE);
	pango_layout_set_alignment(l, PANGO_ALIGN_CENTER);
	pango_layout_set_wrap(l, PANGO_WRAP_WORD_CHAR);
	if (!editing) {
		pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_END);
		pango_layout_set_height(l, -2); /* 2 lignes au plus */
	}
	pango_layout_set_text(l, s, -1);
	PangoRectangle ink, log;
	pango_layout_get_pixel_extents(l, &ink, &log);
	double x = cx - (CELL_W - 6) / 2.0;
	if (editing || sel) {
		double bw = log.width + 10, bh = log.height + 4;
		rrect(cr, cx - bw / 2, y - 2, bw, bh, 5);
		if (editing && B.rename_fresh) {
			rgba(cr, ACCENT, 1);
			cairo_fill(cr);
		} else if (editing) {
			cairo_set_source_rgba(cr, 1, 1, 1, 0.95);
			cairo_fill_preserve(cr);
			rgba(cr, ACCENT, 1);
			cairo_set_line_width(cr, 1.5);
			cairo_stroke(cr);
		} else {
			rgba(cr, ACCENT, 0.95);
			cairo_fill(cr);
		}
	}
	if (!editing) { /* ombre pour rester lisible sur n'importe quel fond */
		cairo_move_to(cr, x, y + 1);
		cairo_set_source_rgba(cr, 0, 0, 0, sel ? 0 : 0.75);
		pango_cairo_show_layout(cr, l);
	}
	cairo_move_to(cr, x, y);
	if (editing && !B.rename_fresh)
		cairo_set_source_rgb(cr, 0.05, 0.08, 0.12);
	else
		cairo_set_source_rgb(cr, 1, 1, 1);
	pango_cairo_show_layout(cr, l);
	if (editing && !B.rename_fresh) { /* curseur de texte à la fin */
		PangoRectangle cur;
		pango_layout_get_cursor_pos(l, (int)strlen(s), &cur, NULL);
		cairo_rectangle(cr, x + cur.x / (double)PANGO_SCALE, y + cur.y / (double)PANGO_SCALE, 1.5,
				cur.height / (double)PANGO_SCALE);
		rgba(cr, ACCENT, 1);
		cairo_fill(cr);
	}
	g_object_unref(l);
}

static void draw_item(cairo_t *cr, struct item *it, int idx, double dx, double dy, double alpha)
{
	double cx, cy;
	cell_xy(it->col, it->row, &cx, &cy);
	cx += dx;
	cy += dy;
	double ix = cx + (CELL_W - ICON) / 2.0, iy = cy + 6;
	if (idx == B.drop_target) { /* dossier ou disque où l'on va lâcher : il s'éclaire */
		rrect(cr, ix - 7, iy - 6, ICON + 14, ICON + 12, 9);
		rgba(cr, ACCENT, 0.35);
		cairo_fill_preserve(cr);
		rgba(cr, ACCENT, 1);
		cairo_set_line_width(cr, 2);
		cairo_stroke(cr);
	} else if (it->sel || idx == B.hover) {
		rrect(cr, ix - 6, iy - 5, ICON + 12, ICON + 10, 8);
		cairo_set_source_rgba(cr, 1, 1, 1, it->sel ? 0.22 : 0.10);
		cairo_fill(cr);
	}
	cairo_push_group(cr);
	cairo_surface_t *img = (it->kind == K_IMAGE || it->kind == K_APP) ? item_image(it) : NULL;
	if (it->kind == K_DIR) {
		draw_folder(cr, ix, iy, ICON);
	} else if (it->kind == K_DISK) {
		draw_disk(cr, ix, iy, ICON);
	} else if (img) {
		double s = ICON;
		if (it->kind == K_IMAGE) { /* vignette avec un cadre blanc */
			int w = cairo_image_surface_get_width(img);
			cairo_save(cr);
			cairo_translate(cr, ix, iy);
			cairo_scale(cr, s / w, s / w);
			cairo_set_source_surface(cr, img, 0, 0);
			cairo_paint(cr);
			cairo_restore(cr);
		} else {
			cairo_save(cr);
			cairo_translate(cr, ix, iy);
			cairo_scale(cr, s / (ICON * 2), s / (ICON * 2));
			cairo_set_source_surface(cr, img, 0, 0);
			cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
			cairo_paint(cr);
			cairo_restore(cr);
		}
	} else {
		draw_document(cr, ix, iy, ICON, it->kind == K_APP ? "" : it->ext);
	}
	cairo_pop_group_to_source(cr);
	cairo_paint_with_alpha(cr, alpha);
	bool editing = idx == B.rename_idx;
	draw_label(cr, editing ? B.rename_buf : it->label, cx + CELL_W / 2.0, iy + ICON + 8, it->sel && !editing, editing);
}

static void draw_menu(cairo_t *cr)
{
	const double ih = 28, sh = 9, pad = 6, w = 240;
	double h = 2 * pad;
	for (int i = 0; i < B.nmenu; i++)
		h += ih + (B.menu[i].sep ? sh : 0);
	double x = B.mx, y = B.my;
	if (x + w > B.width - 4) x = B.width - 4 - w;
	if (y + h > B.height - 4) y = B.height - 4 - h;
	rrect(cr, x, y, w, h, 9);
	cairo_set_source_rgba(cr, 0.012, 0.031, 0.055, 0.97);
	cairo_fill_preserve(cr);
	rgba(cr, ACCENT, 0.9);
	cairo_set_line_width(cr, 1.2);
	cairo_stroke(cr);
	double yy = y + pad;
	PangoFontDescription *fd = pango_font_description_from_string(FONT);
	pango_font_description_set_absolute_size(fd, 13 * PANGO_SCALE);
	for (int i = 0; i < B.nmenu; i++) {
		if (B.menu[i].sep) {
			cairo_set_source_rgba(cr, 1, 1, 1, 0.12);
			cairo_rectangle(cr, x + 10, yy + sh / 2, w - 20, 1);
			cairo_fill(cr);
			yy += sh;
		}
		bool hv = i == B.menu_hover;
		if (hv) {
			rrect(cr, x + 4, yy + 1, w - 8, ih - 2, 5);
			rgba(cr, ACCENT, 0.30);
			cairo_fill(cr);
		}
		PangoLayout *l = pango_cairo_create_layout(cr);
		pango_layout_set_font_description(l, fd);
		pango_layout_set_text(l, B.menu[i].label, -1);
		int tw, th;
		pango_layout_get_pixel_size(l, &tw, &th);
		cairo_move_to(cr, x + 16, yy + (ih - th) / 2.0);
		cairo_set_source_rgb(cr, hv ? 1 : 0.85, hv ? 1 : 0.93, hv ? 1 : 0.98);
		pango_cairo_show_layout(cr, l);
		g_object_unref(l);
		yy += ih;
	}
	pango_font_description_free(fd);
}

static int menu_index_at(double px, double py)
{
	const double ih = 28, sh = 9, pad = 6, w = 240;
	double h = 2 * pad;
	for (int i = 0; i < B.nmenu; i++)
		h += ih + (B.menu[i].sep ? sh : 0);
	double x = B.mx, y = B.my;
	if (x + w > B.width - 4) x = B.width - 4 - w;
	if (y + h > B.height - 4) y = B.height - 4 - h;
	if (px < x || px > x + w)
		return -1;
	double yy = y + pad;
	for (int i = 0; i < B.nmenu; i++) {
		if (B.menu[i].sep)
			yy += sh;
		if (py >= yy && py < yy + ih)
			return i;
		yy += ih;
	}
	return -1;
}

static void draw(cairo_t *cr)
{
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	double dx = 0, dy = 0;
	if (B.drag == D_MOVE) {
		dx = B.px - B.ax;
		dy = B.py - B.ay;
	}
	for (int i = 0; i < B.n; i++)
		if (!(B.drag == D_MOVE && B.items[i].sel))
			draw_item(cr, &B.items[i], i, 0, 0, 1);
	if (B.drag == D_MOVE) /* les icônes déplacées suivent la souris */
		for (int i = 0; i < B.n; i++)
			if (B.items[i].sel)
				draw_item(cr, &B.items[i], i, dx, dy, 0.75);
	if (B.drag == D_BAND) {
		double x = fmin(B.ax, B.px), y = fmin(B.ay, B.py), w = fabs(B.px - B.ax), h = fabs(B.py - B.ay);
		cairo_rectangle(cr, x + 0.5, y + 0.5, w, h);
		rgba(cr, ACCENT, 0.18);
		cairo_fill_preserve(cr);
		rgba(cr, ACCENT, 0.9);
		cairo_set_line_width(cr, 1);
		cairo_stroke(cr);
	}
	if (B.menu_open)
		draw_menu(cr);
}

/* ------------------------------ tampons ------------------------------ */

static void buffer_release(void *data, struct wl_buffer *b)
{
	(void)b;
	((struct buffer *)data)->busy = false;
}
static const struct wl_buffer_listener buffer_listener = { .release = buffer_release };

static struct buffer *get_buffer(int w, int h)
{
	for (int i = 0; i < 2; i++) {
		struct buffer *b = &B.buffers[i];
		if (b->busy)
			continue;
		if (b->wl && (b->w != w || b->h != h)) {
			cairo_surface_destroy(b->cs);
			wl_buffer_destroy(b->wl);
			munmap(b->data, b->size);
			memset(b, 0, sizeof *b);
		}
		if (!b->wl) {
			int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, w);
			size_t size = (size_t)stride * h;
			int fd = memfd_create("plume-icones-bureau", MFD_CLOEXEC);
			if (fd < 0 || ftruncate(fd, size) < 0) {
				if (fd >= 0) close(fd);
				return NULL;
			}
			void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
			if (data == MAP_FAILED) {
				close(fd);
				return NULL;
			}
			struct wl_shm_pool *pool = wl_shm_create_pool(B.shm, fd, size);
			b->wl = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
			wl_shm_pool_destroy(pool);
			close(fd);
			b->data = data;
			b->size = size;
			b->w = w;
			b->h = h;
			b->cs = cairo_image_surface_create_for_data(data, CAIRO_FORMAT_ARGB32, w, h, stride);
			wl_buffer_add_listener(b->wl, &buffer_listener, b);
		}
		return b;
	}
	return NULL;
}

static void frame_done(void *d, struct wl_callback *cb, uint32_t t)
{
	(void)d; (void)t;
	wl_callback_destroy(cb);
	B.frame_cb = NULL;
}
static const struct wl_callback_listener frame_listener = { .done = frame_done };

static void render(void)
{
	if (!B.configured || B.width <= 0 || B.height <= 0)
		return;
	struct buffer *b = get_buffer(B.width, B.height);
	if (!b)
		return;
	cairo_t *cr = cairo_create(b->cs);
	draw(cr);
	cairo_destroy(cr);
	cairo_surface_flush(b->cs);
	wl_surface_attach(B.surface, b->wl, 0, 0);
	wl_surface_damage_buffer(B.surface, 0, 0, b->w, b->h);
	B.frame_cb = wl_surface_frame(B.surface);
	wl_callback_add_listener(B.frame_cb, &frame_listener, NULL);
	wl_surface_commit(B.surface);
	b->busy = true;
	B.dirty = false;
}

/* ------------------------------ actions ------------------------------ */

static void clear_sel(void)
{
	for (int i = 0; i < B.n; i++)
		B.items[i].sel = false;
}

static int nsel(void)
{
	int k = 0;
	for (int i = 0; i < B.n; i++)
		k += B.items[i].sel;
	return k;
}

static void rename_start(int i)
{
	if (i < 0 || i >= B.n)
		return;
	B.rename_idx = i;
	snprintf(B.rename_buf, sizeof B.rename_buf, "%s", B.items[i].name);
	B.rename_fresh = true;
	B.dirty = true;
}

static void rename_commit(bool ok)
{
	int i = B.rename_idx;
	B.rename_idx = -1;
	B.dirty = true;
	if (!ok || i < 0 || i >= B.n || !B.rename_buf[0] || strchr(B.rename_buf, '/') ||
	    !strcmp(B.rename_buf, B.items[i].name))
		return;
	char a[PATH_MAX], b[PATH_MAX];
	struct stat st;
	item_path(&B.items[i], a, sizeof a);
	snprintf(b, sizeof b, "%s/%s", B.dir, B.rename_buf);
	if (stat(b, &st) == 0) /* ce nom existe déjà : on ne l'écrase pas */
		return;
	if (rename(a, b) == 0) {
		snprintf(B.items[i].name, sizeof B.items[i].name, "%s", B.rename_buf);
		places_save(); /* garder la place sous le nouveau nom */
		load_items();
	}
}

static void menu_add(const char *label, int act, bool sep)
{
	if (B.nmenu >= 12)
		return;
	struct menu_entry *e = &B.menu[B.nmenu++];
	snprintf(e->label, sizeof e->label, "%s", label);
	e->act = act;
	e->sep = sep;
}

static void menu_show(double x, double y, int on_item)
{
	B.nmenu = 0;
	B.menu_hover = -1;
	B.mx = x;
	B.my = y;
	int cols = grid_cols();
	B.menu_col = (int)((B.width - MARGIN - x) / CELL_W);
	B.menu_row = (int)((y - MARGIN) / CELL_H);
	if (B.menu_col < 0) B.menu_col = 0;
	if (B.menu_col >= cols) B.menu_col = cols - 1;
	if (B.menu_row < 0) B.menu_row = 0;
	if (on_item >= 0 && B.items[on_item].kind == K_DISK) {
		clear_sel();
		B.items[on_item].sel = true;
		menu_add("Ouvrir", A_OPEN, false);
		menu_add("Éjecter", A_EJECT, true);
	} else if (on_item >= 0) {
		if (!B.items[on_item].sel) {
			clear_sel();
			B.items[on_item].sel = true;
		}
		menu_add("Ouvrir", A_OPEN, false);
		menu_add("Aperçu rapide (Espace)", A_PREVIEW, false);
		if (nsel() == 1)
			menu_add("Renommer", A_RENAME, true);
		menu_add("Copier", A_COPY, nsel() != 1);
		menu_add("Couper", A_CUT, false);
		menu_add("Dupliquer", A_DUP, false);
		menu_add("Copier le chemin", A_PATH, false);
		menu_add("Mettre à la corbeille", A_TRASH, true);
	} else {
		clear_sel();
		{
			struct offer_info *inf = B.sel_offer ? wl_data_offer_get_user_data(B.sel_offer) : NULL;
			if (inf && (inf->gnome || inf->uri))
				menu_add("Coller", A_PASTE, false);
		}
		menu_add("Nouveau dossier", A_NEWDIR, B.nmenu > 0);
		menu_add("Nouveau document texte", A_NEWDOC, false);
		menu_add("Ranger par nom", A_SORT, true);
		menu_add("Ouvrir le Bureau dans Fichiers", A_FILES, false);
		menu_add("Changer le fond d'écran…", A_WALL, true);
	}
	B.menu_open = true;
	B.dirty = true;
}

static void create_at_menu(const char *base, const char *ext, bool dir)
{
	char name[256], p[PATH_MAX];
	unique_name(base, ext, name, sizeof name);
	snprintf(p, sizeof p, "%s/%s", B.dir, name);
	if (dir) {
		if (mkdir(p, 0755))
			return;
	} else {
		int fd = open(p, O_WRONLY | O_CREAT | O_EXCL, 0644);
		if (fd < 0)
			return;
		close(fd);
	}
	/* à l'endroit du clic droit si la case est libre */
	FILE *f = fopen(B.posfile, "a");
	if (f && item_at_cell(B.menu_col, B.menu_row, -1) < 0)
		fprintf(f, "%d\t%d\t%s\n", B.menu_col, B.menu_row, name);
	if (f)
		fclose(f);
	load_items();
	clear_sel();
	for (int i = 0; i < B.n; i++)
		if (!strcmp(B.items[i].name, name)) {
			B.items[i].sel = true;
			rename_start(i); /* on tape tout de suite le nom, comme sur Mac */
		}
}

static void preview_toggle(void);
static void clip_set(bool cut);
static void paste_start(void);

static void do_action(int act)
{
	B.menu_open = false;
	B.dirty = true;
	switch (act) {
	case A_OPEN:
		for (int i = 0; i < B.n; i++)
			if (B.items[i].sel)
				open_item(&B.items[i]);
		break;
	case A_RENAME:
		for (int i = 0; i < B.n; i++)
			if (B.items[i].sel) {
				rename_start(i);
				break;
			}
		break;
	case A_DUP:
		for (int i = 0; i < B.n; i++)
			if (B.items[i].sel)
				duplicate_item(&B.items[i]);
		load_items();
		break;
	case A_PATH:
		copy_path();
		break;
	case A_TRASH:
		for (int i = 0; i < B.n; i++)
			if (B.items[i].sel && B.items[i].kind != K_DISK)
				trash_item(&B.items[i]);
		load_items();
		break;
	case A_EJECT:
		for (int i = 0; i < B.n; i++)
			if (B.items[i].sel && B.items[i].kind == K_DISK) {
				char *argv[] = { "sh", "-c",
					"d=$(findmnt -no SOURCE \"$1\"); udisksctl unmount --no-user-interaction -b \"$d\" && "
					"{ udisksctl power-off --no-user-interaction -b \"$d\" 2>/dev/null; "
					"notify-send -a Plume \"Disque éjecté\" \"Tu peux le débrancher.\"; } || "
					"notify-send -a Plume -u critical \"Impossible d'éjecter\" \"Un fichier du disque est peut-être encore ouvert.\"",
					"sh", B.items[i].mnt, NULL };
				run_argv(argv);
			}
		break;
	case A_PREVIEW:
		preview_toggle();
		break;
	case A_COPY:
	case A_CUT:
		clip_set(act == A_CUT);
		break;
	case A_PASTE:
		paste_start();
		break;
	case A_NEWDIR:
		create_at_menu("Nouveau dossier", "", true);
		break;
	case A_NEWDOC:
		create_at_menu("Nouveau document", ".txt", false);
		break;
	case A_SORT:
		sort_items();
		break;
	case A_FILES: {
		char *argv[] = { "pcmanfm", B.dir, NULL };
		run_argv(argv);
		break;
	}
	case A_WALL:
		run_sh("plume-fonds");
		break;
	}
}

/* fin d'un déplacement : chaque icône sélectionnée avance d'autant de cases (si libre) */
static void move_commit(void)
{
	int dc = (int)lround(-(B.px - B.ax) / CELL_W), dr = (int)lround((B.py - B.ay) / CELL_H);
	if (!dc && !dr)
		return;
	int cols = grid_cols(), rows = grid_rows();
	/* d'abord vérifier que toutes les cases visées sont libres (hors icônes déplacées) */
	for (int i = 0; i < B.n; i++) {
		if (!B.items[i].sel)
			continue;
		int c = B.items[i].col + dc, r = B.items[i].row + dr;
		if (c < 0 || r < 0 || c >= cols || r >= rows)
			return;
		int o = item_at_cell(c, r, i);
		if (o >= 0 && !B.items[o].sel)
			return;
	}
	for (int i = 0; i < B.n; i++)
		if (B.items[i].sel) {
			B.items[i].col += dc;
			B.items[i].row += dr;
		}
	places_save();
}

static void band_select(void)
{
	double x0 = fmin(B.ax, B.px), y0 = fmin(B.ay, B.py), x1 = fmax(B.ax, B.px), y1 = fmax(B.ay, B.py);
	for (int i = 0; i < B.n; i++) {
		double x, y, w, h;
		icon_rect(&B.items[i], 0, 0, &x, &y, &w, &h);
		bool in = x < x1 && x + w > x0 && y < y1 && y + h > y0;
		if (B.ctrl)
			B.items[i].sel = B.items[i].sel || in;
		else
			B.items[i].sel = in;
	}
}


static void src_target_noop(void *d, struct wl_data_source *s, const char *m) { (void)d; (void)s; (void)m; }
static void paste_files(char *text, bool cut);

/* -------------------------- aperçu rapide ---------------------------- */

/* Espace (ou menu) : l'image, le PDF, le son ou la vidéo s'ouvre dans une visionneuse légère ;
 * Espace à nouveau la referme tant que le bureau a la main */
static void preview_toggle(void)
{
	if (B.preview_pid > 0 && kill(B.preview_pid, 0) == 0) {
		kill(B.preview_pid, SIGTERM);
		B.preview_pid = 0;
		return;
	}
	B.preview_pid = 0;
	int i = -1;
	for (int k = 0; k < B.n && i < 0; k++)
		if (B.items[k].sel && B.items[k].kind != K_DISK && B.items[k].kind != K_DIR)
			i = k;
	if (i < 0)
		return;
	static const char *AV[] = { "mp3", "ogg", "oga", "opus", "flac", "wav", "m4a", "mp4", "mkv", "webm", "avi", "mov", NULL };
	const char *prog = "xdg-open";
	if (B.items[i].kind == K_IMAGE)
		prog = "imv";
	else if (!strcmp(B.items[i].ext, "pdf"))
		prog = "zathura";
	else
		for (int k = 0; AV[k]; k++)
			if (!strcmp(B.items[i].ext, AV[k]))
				prog = "mpv";
	char p[PATH_MAX];
	item_path(&B.items[i], p, sizeof p);
	pid_t pid = fork();
	if (pid == 0) {
		setsid();
		execlp(prog, prog, p, (char *)NULL);
		_exit(127);
	}
	if (pid > 0)
		B.preview_pid = pid;
}

/* ------------------------- glisser-déposer --------------------------- */

#define DND_COPY WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY
#define DND_MOVE WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE


static void of_offer(void *d, struct wl_data_offer *o, const char *mime)
{
	(void)o;
	if (!strcmp(mime, "text/uri-list"))
		((struct offer_info *)d)->uri = true;
	else if (!strcmp(mime, "x-special/gnome-copied-files"))
		((struct offer_info *)d)->gnome = true;
}
static void of_source_actions(void *d, struct wl_data_offer *o, uint32_t a) { (void)d; (void)o; (void)a; }
static void of_action(void *d, struct wl_data_offer *o, uint32_t a)
{
	(void)o;
	((struct offer_info *)d)->action = a;
}
static const struct wl_data_offer_listener offer_listener = {
	.offer = of_offer, .source_actions = of_source_actions, .action = of_action,
};

static void offer_free(struct wl_data_offer *o)
{
	if (!o)
		return;
	free(wl_data_offer_get_user_data(o));
	wl_data_offer_destroy(o);
}

/* dossier ou disque du bureau sous la souris, qui peut recevoir ce qu'on glisse */
static int drop_target_at(double x, double y)
{
	int i = item_at(x, y);
	if (i < 0 || (B.items[i].kind != K_DIR && B.items[i].kind != K_DISK))
		return -1;
	if (B.dragging_out && B.items[i].sel) /* pas dans lui-même */
		return -1;
	return i;
}

static void dd_data_offer(void *d, struct wl_data_device *dd, struct wl_data_offer *o)
{
	(void)d; (void)dd;
	wl_data_offer_add_listener(o, &offer_listener, calloc(1, sizeof(struct offer_info)));
}

static void dd_enter(void *d, struct wl_data_device *dd, uint32_t serial, struct wl_surface *sf, wl_fixed_t x,
		     wl_fixed_t y, struct wl_data_offer *o)
{
	(void)d; (void)dd; (void)sf;
	if (B.offer && B.offer != o && B.offer != B.drop_offer)
		offer_free(B.offer);
	B.offer = o;
	B.drop_x = wl_fixed_to_double(x);
	B.drop_y = wl_fixed_to_double(y);
	struct offer_info *inf = o ? wl_data_offer_get_user_data(o) : NULL;
	if (o && inf && inf->uri) {
		wl_data_offer_accept(o, serial, "text/uri-list");
		if (B.ddm_ver >= 3)
			wl_data_offer_set_actions(o, DND_COPY | DND_MOVE, DND_MOVE);
	} else if (o) {
		wl_data_offer_accept(o, serial, NULL);
	}
	B.drop_target = drop_target_at(B.drop_x, B.drop_y);
	B.dirty = true;
}

static void dd_leave(void *d, struct wl_data_device *dd)
{
	(void)d; (void)dd;
	if (B.offer && B.offer != B.drop_offer)
		offer_free(B.offer);
	B.offer = NULL;
	B.drop_target = -1;
	B.dirty = true;
}

static void dd_motion(void *d, struct wl_data_device *dd, uint32_t t, wl_fixed_t x, wl_fixed_t y)
{
	(void)d; (void)dd; (void)t;
	B.drop_x = wl_fixed_to_double(x);
	B.drop_y = wl_fixed_to_double(y);
	int tg = drop_target_at(B.drop_x, B.drop_y);
	if (tg != B.drop_target) {
		B.drop_target = tg;
		B.dirty = true;
	}
}

static void dd_drop(void *d, struct wl_data_device *dd)
{
	(void)d; (void)dd;
	struct wl_data_offer *o = B.offer;
	B.offer = NULL;
	struct offer_info *inf = o ? wl_data_offer_get_user_data(o) : NULL;
	int fds[2];
	if (!o || !inf || !inf->uri || B.drop_fd >= 0 || pipe2(fds, O_CLOEXEC)) {
		offer_free(o);
		B.drop_target = -1;
		return;
	}
	/* la liste des fichiers arrive par un tube, lu dans la boucle principale (sans bloquer) */
	wl_data_offer_receive(o, "text/uri-list", fds[1]);
	close(fds[1]);
	fcntl(fds[0], F_SETFL, O_NONBLOCK);
	B.drop_fd = fds[0];
	B.drop_offer = o;
	B.drop_dest = B.drop_target;
	free(B.drop_buf);
	B.drop_buf = NULL;
	B.drop_len = 0;
	wl_display_flush(B.display);
}

static void dd_selection(void *d, struct wl_data_device *dd, struct wl_data_offer *o)
{
	(void)d; (void)dd;
	/* presse-papiers : on garde le dernier (pour Coller) */
	if (B.sel_offer && B.sel_offer != o)
		offer_free(B.sel_offer);
	B.sel_offer = o;
}

static const struct wl_data_device_listener dd_listener = {
	.data_offer = dd_data_offer, .enter = dd_enter, .leave = dd_leave, .motion = dd_motion,
	.drop = dd_drop, .selection = dd_selection,
};

/* « file:///home/moi/Mes%20photos » -> « /home/moi/Mes photos » */
static bool uri_to_path(const char *uri, char *out, size_t cap)
{
	if (strncmp(uri, "file://", 7))
		return false;
	const char *s = uri + 7;
	if (!strncmp(s, "localhost/", 10))
		s += 9;
	if (*s != '/')
		return false;
	size_t k = 0;
	for (; *s && k + 1 < cap; s++) {
		if (*s == '%' && s[1] && s[2]) {
			char h[3] = { s[1], s[2], 0 };
			out[k++] = (char)strtol(h, NULL, 16);
			s += 2;
		} else {
			out[k++] = *s;
		}
	}
	out[k] = '\0';
	return true;
}

static void cell_at(double x, double y, int *col, int *row)
{
	int cols = grid_cols(), rows = grid_rows();
	*col = (int)((B.width - MARGIN - x) / CELL_W);
	*row = (int)((y - MARGIN) / CELL_H);
	if (*col < 0) *col = 0;
	if (*col >= cols) *col = cols - 1;
	if (*row < 0) *row = 0;
	if (*row >= rows) *row = rows - 1;
}

/* fichiers lâchés sur le bureau (ou sur un de ses dossiers / disques).
 * mode : 0 = comme sur Mac (déplacé sur le même disque, sinon copié), 1 = copié, 2 = déplacé */
static void transfer(char *text, double x, double y, int target, int mode);
static void process_drop(char *text, double x, double y, int target)
{
	transfer(text, x, y, target, 0);
}
static void paste_files(char *text, bool cut)
{
	/* à la 1re case libre (pas de position de souris pour un collage au clavier) */
	transfer(text, -1, -1, -1, cut ? 2 : 1);
}
static void transfer(char *text, double x, double y, int target, int mode)
{
	char dest[PATH_MAX];
	if (target >= 0 && target < B.n)
		item_path(&B.items[target], dest, sizeof dest);
	else
		snprintf(dest, sizeof dest, "%s", B.dir);
	bool on_desktop = !strcmp(dest, B.dir);
	struct stat dst_st;
	if (stat(dest, &dst_st))
		return;

	/* icônes du bureau lâchées sur le bureau : on les déplace seulement (comme avant) */
	if (on_desktop && B.dragging_out) {
		B.px = x;
		B.py = y;
		move_commit();
		B.dirty = true;
		return;
	}
	int col = -1, row = -1;
	if (x >= 0)
		cell_at(x, y, &col, &row);
	FILE *pos = on_desktop && x >= 0 ? fopen(B.posfile, "a") : NULL;
	for (char *l = strtok(text, "\r\n"); l; l = strtok(NULL, "\r\n")) {
		char src[PATH_MAX];
		if (l[0] == '#' || !uri_to_path(l, src, sizeof src))
			continue;
		const char *base = strrchr(src, '/');
		base = base ? base + 1 : src;
		char srcdir[PATH_MAX];
		snprintf(srcdir, sizeof srcdir, "%.*s", (int)(base - src - 1), src);
		if (!strcmp(src, dest) || (!strcmp(srcdir, dest) && mode != 1)) /* déjà là, ou dans lui-même */
			continue;
		struct stat st;
		if (lstat(src, &st))
			continue;
		/* nom libre à l'arrivée : « photo.png », sinon « photo 2.png » */
		char nb[256], ext[32] = "", name[300], to[PATH_MAX];
		snprintf(nb, sizeof nb, "%s", base);
		char *dot = S_ISDIR(st.st_mode) ? NULL : strrchr(nb, '.');
		if (dot && dot != nb && strlen(dot) < sizeof ext) {
			snprintf(ext, sizeof ext, "%s", dot);
			*dot = '\0';
		}
		unique_in(dest, nb, ext, name, sizeof name);
		snprintf(to, sizeof to, "%s/%s", dest, name);
		/* comme sur Mac : déplacé sur le même disque, copié depuis un autre disque */
		bool move = mode == 2 || (mode == 0 && st.st_dev == dst_st.st_dev);
		if (move && rename(src, to) == 0) {
			/* déplacé */
		} else {
			char *argv[] = { "cp", "-a", "--", src, to, NULL };
			pid_t pid = fork();
			if (pid == 0) {
				execvp("cp", argv);
				_exit(127);
			}
			if (pid > 0)
				waitpid(pid, NULL, 0);
		}
		if (pos && item_at_cell(col, row, -1) < 0) { /* là où on l'a lâché */
			fprintf(pos, "%d\t%d\t%s\n", col, row, name);
			row++;
		}
	}
	if (pos)
		fclose(pos);
	load_items();
}

static void drop_read(void)
{
	char buf[4096];
	for (;;) {
		ssize_t n = read(B.drop_fd, buf, sizeof buf);
		if (n > 0) {
			char *nb = realloc(B.drop_buf, B.drop_len + n + 1);
			if (!nb)
				break;
			B.drop_buf = nb;
			memcpy(B.drop_buf + B.drop_len, buf, n);
			B.drop_len += n;
			B.drop_buf[B.drop_len] = '\0';
			continue;
		}
		if (n < 0 && (errno == EAGAIN || errno == EINTR))
			return; /* la suite plus tard */
		break;      /* fin (ou erreur) : on traite */
	}
	close(B.drop_fd);
	B.drop_fd = -1;
	if (B.drop_buf)
		process_drop(B.drop_buf, B.drop_x, B.drop_y, B.drop_dest);
	struct offer_info *inf = B.drop_offer ? wl_data_offer_get_user_data(B.drop_offer) : NULL;
	if (B.drop_offer && B.ddm_ver >= 3 && inf && inf->action)
		wl_data_offer_finish(B.drop_offer);
	offer_free(B.drop_offer);
	B.drop_offer = NULL;
	B.drop_target = -1;
	free(B.drop_buf);
	B.drop_buf = NULL;
	B.drop_len = 0;
	B.dirty = true;
}

/* --- presse-papiers : Copier / Couper (bureau -> Fichiers) et Coller (Fichiers -> bureau) --- */

static void clip_send(void *d, struct wl_data_source *s, const char *mime, int32_t fd)
{
	(void)d; (void)s;
	const char *t = B.clip_text;
	if (strcmp(mime, "x-special/gnome-copied-files")) { /* text/uri-list : sans la 1re ligne copy/cut */
		const char *nl = strchr(t, '\n');
		t = nl ? nl + 1 : t;
	}
	size_t len = strlen(t), off = 0;
	while (off < len) {
		ssize_t w = write(fd, t + off, len - off);
		if (w <= 0)
			break;
		off += w;
	}
	close(fd);
}
static void clip_cancelled(void *d, struct wl_data_source *s)
{
	(void)d;
	wl_data_source_destroy(s); /* une autre appli a pris le presse-papiers */
	if (B.clip == s)
		B.clip = NULL;
}
static const struct wl_data_source_listener clip_listener = {
	.target = src_target_noop, .send = clip_send, .cancelled = clip_cancelled,
};

static void clip_set(bool cut)
{
	if (!B.ddm || !B.dd)
		return;
	size_t k = snprintf(B.clip_text, sizeof B.clip_text, "%s\n", cut ? "cut" : "copy");
	int n = 0;
	for (int i = 0; i < B.n; i++) {
		if (!B.items[i].sel || B.items[i].kind == K_DISK)
			continue;
		char p[PATH_MAX], enc[PATH_MAX * 3];
		item_path(&B.items[i], p, sizeof p);
		url_encode(p, enc, sizeof enc);
		if (k + strlen(enc) + 12 >= sizeof B.clip_text)
			break;
		k += snprintf(B.clip_text + k, sizeof B.clip_text - k, "file://%s\n", enc);
		n++;
	}
	if (!n)
		return;
	if (B.clip)
		wl_data_source_destroy(B.clip);
	B.clip = wl_data_device_manager_create_data_source(B.ddm);
	wl_data_source_add_listener(B.clip, &clip_listener, NULL);
	wl_data_source_offer(B.clip, "x-special/gnome-copied-files");
	wl_data_source_offer(B.clip, "text/uri-list");
	wl_data_device_set_selection(B.dd, B.clip, B.input_serial);
}

static void paste_start(void)
{
	struct offer_info *inf = B.sel_offer ? wl_data_offer_get_user_data(B.sel_offer) : NULL;
	int fds[2];
	if (!inf || (!inf->gnome && !inf->uri) || B.paste_fd >= 0 || pipe2(fds, O_CLOEXEC))
		return;
	B.paste_gnome = inf->gnome;
	wl_data_offer_receive(B.sel_offer, inf->gnome ? "x-special/gnome-copied-files" : "text/uri-list", fds[1]);
	close(fds[1]);
	fcntl(fds[0], F_SETFL, O_NONBLOCK);
	B.paste_fd = fds[0];
	free(B.paste_buf);
	B.paste_buf = NULL;
	B.paste_len = 0;
	wl_display_flush(B.display);
}

/* collé sur le bureau : copié (ou déplacé si on avait fait « Couper ») */
static void paste_read(void)
{
	char buf[4096];
	for (;;) {
		ssize_t n = read(B.paste_fd, buf, sizeof buf);
		if (n > 0) {
			char *nb = realloc(B.paste_buf, B.paste_len + n + 1);
			if (!nb)
				break;
			B.paste_buf = nb;
			memcpy(B.paste_buf + B.paste_len, buf, n);
			B.paste_len += n;
			B.paste_buf[B.paste_len] = '\0';
			continue;
		}
		if (n < 0 && (errno == EAGAIN || errno == EINTR))
			return;
		break;
	}
	close(B.paste_fd);
	B.paste_fd = -1;
	char *t = B.paste_buf;
	bool cut = false;
	if (t && B.paste_gnome) { /* « copy » ou « cut » sur la 1re ligne */
		cut = !strncmp(t, "cut", 3);
		char *nl = strchr(t, '\n');
		t = nl ? nl + 1 : t + strlen(t);
	}
	if (t && *t) {
		bool was = B.dragging_out;
		B.dragging_out = false;
		paste_files(t, cut);
		B.dragging_out = was;
	}
	free(B.paste_buf);
	B.paste_buf = NULL;
}

/* --- glisser depuis le bureau vers une autre appli --- */

static void drag_end(void)
{
	if (B.src)
		wl_data_source_destroy(B.src);
	B.src = NULL;
	B.dragging_out = false;
	if (B.drag_icon)
		wl_surface_destroy(B.drag_icon);
	B.drag_icon = NULL;
	B.dirty = true;
}

static void src_target(void *d, struct wl_data_source *s, const char *m) { (void)d; (void)s; (void)m; }
static void src_send(void *d, struct wl_data_source *s, const char *mime, int32_t fd)
{
	(void)d; (void)s;
	bool uri = !strcmp(mime, "text/uri-list");
	for (int i = 0; i < B.n; i++) {
		if (!B.items[i].sel)
			continue;
		char p[PATH_MAX], enc[PATH_MAX * 3], line[PATH_MAX * 3 + 16];
		item_path(&B.items[i], p, sizeof p);
		if (uri) {
			url_encode(p, enc, sizeof enc);
			snprintf(line, sizeof line, "file://%s\r\n", enc);
		} else {
			snprintf(line, sizeof line, "%s\n", p);
		}
		size_t len = strlen(line), off = 0;
		while (off < len) {
			ssize_t w = write(fd, line + off, len - off);
			if (w <= 0)
				break;
			off += w;
		}
	}
	close(fd);
}
static void src_cancelled(void *d, struct wl_data_source *s) { (void)d; (void)s; drag_end(); }
static void src_dropped(void *d, struct wl_data_source *s) { (void)d; (void)s; }
static void src_finished(void *d, struct wl_data_source *s)
{
	(void)d; (void)s;
	drag_end();
	B.ino_reload = true; /* l'autre appli a peut-être déplacé les fichiers */
}
static void src_action(void *d, struct wl_data_source *s, uint32_t a) { (void)d; (void)s; (void)a; }
static const struct wl_data_source_listener src_listener = {
	.target = src_target, .send = src_send, .cancelled = src_cancelled,
	.dnd_drop_performed = src_dropped, .dnd_finished = src_finished, .action = src_action,
};

/* image qui suit la souris pendant le glisser : l'icône, et le nombre de fichiers s'il y en a plusieurs */
static void make_drag_icon(int idx)
{
	const int S = 84;
	struct buffer *b = &B.drag_buf;
	if (b->wl) {
		cairo_surface_destroy(b->cs);
		wl_buffer_destroy(b->wl);
		munmap(b->data, b->size);
		memset(b, 0, sizeof *b);
	}
	int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, S);
	size_t size = (size_t)stride * S;
	int fd = memfd_create("plume-glisser", MFD_CLOEXEC);
	if (fd < 0 || ftruncate(fd, size) < 0) {
		if (fd >= 0) close(fd);
		return;
	}
	void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) {
		close(fd);
		return;
	}
	struct wl_shm_pool *pool = wl_shm_create_pool(B.shm, fd, size);
	b->wl = wl_shm_pool_create_buffer(pool, 0, S, S, stride, WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);
	b->data = data;
	b->size = size;
	b->w = b->h = S;
	b->cs = cairo_image_surface_create_for_data(data, CAIRO_FORMAT_ARGB32, S, S, stride);
	cairo_t *cr = cairo_create(b->cs);
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	cairo_push_group(cr);
	struct item *it = &B.items[idx];
	cairo_surface_t *img = (it->kind == K_IMAGE || it->kind == K_APP) ? item_image(it) : NULL;
	double ix = (S - ICON) / 2.0, iy = 12;
	if (it->kind == K_DIR)
		draw_folder(cr, ix, iy, ICON);
	else if (img) {
		cairo_save(cr);
		cairo_translate(cr, ix, iy);
		cairo_scale(cr, (double)ICON / cairo_image_surface_get_width(img), (double)ICON / cairo_image_surface_get_width(img));
		cairo_set_source_surface(cr, img, 0, 0);
		cairo_paint(cr);
		cairo_restore(cr);
	} else
		draw_document(cr, ix, iy, ICON, it->kind == K_APP ? "" : it->ext);
	cairo_pop_group_to_source(cr);
	cairo_paint_with_alpha(cr, 0.85);
	int k = nsel();
	if (k > 1) { /* pastille rouge avec le nombre, comme sur Mac */
		char t[8];
		snprintf(t, sizeof t, "%d", k);
		cairo_arc(cr, S - 14, 14, 11, 0, 2 * M_PI);
		cairo_set_source_rgb(cr, 0.93, 0.27, 0.27);
		cairo_fill(cr);
		PangoLayout *l = pango_cairo_create_layout(cr);
		PangoFontDescription *fdsc = pango_font_description_from_string("DejaVu Sans Bold");
		pango_font_description_set_absolute_size(fdsc, 11 * PANGO_SCALE);
		pango_layout_set_font_description(l, fdsc);
		pango_font_description_free(fdsc);
		pango_layout_set_text(l, t, -1);
		int tw, th;
		pango_layout_get_pixel_size(l, &tw, &th);
		cairo_move_to(cr, S - 14 - tw / 2.0, 14 - th / 2.0);
		cairo_set_source_rgb(cr, 1, 1, 1);
		pango_cairo_show_layout(cr, l);
		g_object_unref(l);
	}
	cairo_destroy(cr);
	cairo_surface_flush(b->cs);
	B.drag_icon = wl_compositor_create_surface(B.compositor);
	wl_surface_attach(B.drag_icon, b->wl, -S / 2, -S / 2);
	wl_surface_damage(B.drag_icon, 0, 0, S, S);
}

static void start_drag_out(int idx)
{
	B.src = wl_data_device_manager_create_data_source(B.ddm);
	wl_data_source_add_listener(B.src, &src_listener, NULL);
	wl_data_source_offer(B.src, "text/uri-list");
	wl_data_source_offer(B.src, "text/plain;charset=utf-8");
	wl_data_source_offer(B.src, "text/plain");
	if (B.ddm_ver >= 3)
		wl_data_source_set_actions(B.src, DND_COPY | DND_MOVE);
	make_drag_icon(idx);
	wl_data_device_start_drag(B.dd, B.src, B.surface, B.drag_icon, B.press_serial);
	if (B.drag_icon)
		wl_surface_commit(B.drag_icon);
	B.dragging_out = true;
}

/* ------------------------------ souris ------------------------------- */

static struct wl_cursor_theme *cursor_theme;
static struct wl_surface *cursor_surface;

static void p_enter(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *sf, wl_fixed_t x, wl_fixed_t y)
{
	(void)d; (void)sf;
	B.px = wl_fixed_to_double(x);
	B.py = wl_fixed_to_double(y);
	struct wl_cursor *c = cursor_theme ? wl_cursor_theme_get_cursor(cursor_theme, "left_ptr") : NULL;
	if (c && c->image_count) {
		struct wl_cursor_image *img = c->images[0];
		wl_surface_attach(cursor_surface, wl_cursor_image_get_buffer(img), 0, 0);
		wl_surface_damage(cursor_surface, 0, 0, img->width, img->height);
		wl_surface_commit(cursor_surface);
		wl_pointer_set_cursor(p, serial, cursor_surface, img->hotspot_x, img->hotspot_y);
	}
}

static void p_leave(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *sf)
{
	(void)d; (void)p; (void)s; (void)sf;
	if (B.hover >= 0) {
		B.hover = -1;
		B.dirty = true;
	}
}

static void p_motion(void *d, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y)
{
	(void)d; (void)p; (void)t;
	B.px = wl_fixed_to_double(x);
	B.py = wl_fixed_to_double(y);
	bool far = hypot(B.px - B.ax, B.py - B.ay) > 5;
	if (B.drag == D_PRESS_ICON && far && B.drag_button == BTN_LEFT) {
		bool disk = false;
		for (int i = 0; i < B.n; i++)
			disk |= B.items[i].sel && B.items[i].kind == K_DISK;
		if (B.dd && !disk && B.press_item >= 0) {
			/* vrai glisser-déposer : vers Fichiers, une appli, un dossier… ou ailleurs sur le bureau */
			start_drag_out(B.press_item);
			B.drag = D_NONE;
		} else {
			B.drag = D_MOVE; /* disques : seulement déplacés sur le bureau */
		}
	}
	else if (B.drag == D_PRESS_EMPTY && far)
		B.drag = D_BAND;
	if (B.drag == D_BAND)
		band_select();
	if (B.menu_open) {
		int h = menu_index_at(B.px, B.py);
		if (h != B.menu_hover) {
			B.menu_hover = h;
			B.dirty = true;
		}
	}
	int h = B.drag == D_NONE ? item_at(B.px, B.py) : -1;
	if (h != B.hover)
		B.hover = h;
	B.dirty = true;
}

static void p_button(void *d, struct wl_pointer *p, uint32_t serial, uint32_t t, uint32_t button, uint32_t state)
{
	(void)d; (void)p; (void)t;
	bool down = state == WL_POINTER_BUTTON_STATE_PRESSED;
	B.dirty = true;
	if (down) {
		B.press_serial = serial;
		B.input_serial = serial;
		if (B.menu_open) {
			int i = menu_index_at(B.px, B.py);
			B.menu_open = false;
			if (i >= 0 && button == BTN_LEFT)
				do_action(B.menu[i].act);
			return;
		}
		if (B.rename_idx >= 0)
			rename_commit(true);
		if (button != BTN_LEFT && button != BTN_RIGHT)
			return;
		B.ax = B.px;
		B.ay = B.py;
		B.drag_button = button;
		int i = item_at(B.px, B.py);
		B.press_item = i;
		if (i >= 0) {
			B.drag = D_PRESS_ICON;
			if (button == BTN_LEFT) {
				if (B.ctrl) {
					B.items[i].sel = !B.items[i].sel;
				} else if (!B.items[i].sel) {
					clear_sel();
					B.items[i].sel = true;
				}
				double tn = now_s();
				if (B.last_click_item == i && tn - B.last_click_t < 0.45) {
					open_item(&B.items[i]);
					B.last_click_item = -1;
					B.drag = D_NONE;
				} else {
					B.last_click_item = i;
					B.last_click_t = tn;
				}
			}
		} else {
			B.drag = D_PRESS_EMPTY;
			if (!B.ctrl && button == BTN_LEFT)
				clear_sel();
		}
		return;
	}
	/* relâché */
	if (button != B.drag_button)
		return;
	enum drag was = B.drag;
	B.drag = D_NONE;
	if (was == D_MOVE)
		move_commit();
	else if (button == BTN_RIGHT && (was == D_PRESS_ICON || was == D_PRESS_EMPTY))
		menu_show(B.px, B.py, was == D_PRESS_ICON ? B.press_item : -1);
	else if (was == D_PRESS_ICON && button == BTN_LEFT && !B.ctrl && B.press_item >= 0) {
		/* simple clic sur une icône d'un groupe : ne garder qu'elle */
		if (nsel() > 1 && B.last_click_item == B.press_item) {
			clear_sel();
			B.items[B.press_item].sel = true;
		}
	}
}

static void p_axis(void *d, struct wl_pointer *p, uint32_t t, uint32_t a, wl_fixed_t v)
{
	(void)d; (void)p; (void)t; (void)a; (void)v;
}

static const struct wl_pointer_listener pointer_listener = {
	.enter = p_enter, .leave = p_leave, .motion = p_motion, .button = p_button, .axis = p_axis,
};

/* ------------------------------ clavier ------------------------------ */

static void kb_keymap(void *dd, struct wl_keyboard *k, uint32_t fmt, int32_t fd, uint32_t size)
{
	(void)dd; (void)k;
	if (fmt != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
		close(fd);
		return;
	}
	char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (map == MAP_FAILED)
		return;
	struct xkb_keymap *km = xkb_keymap_new_from_string(B.xkb, map, XKB_KEYMAP_FORMAT_TEXT_V1, 0);
	munmap(map, size);
	if (!km)
		return;
	if (B.xkb_state) xkb_state_unref(B.xkb_state);
	if (B.keymap) xkb_keymap_unref(B.keymap);
	B.keymap = km;
	B.xkb_state = xkb_state_new(km);
}
static void kb_enter(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *sf, struct wl_array *keys)
{
	(void)d; (void)k; (void)s; (void)sf; (void)keys;
}
static void kb_leave(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *sf)
{
	(void)d; (void)k; (void)s; (void)sf;
	B.ctrl = false;
	if (B.rename_idx >= 0)
		rename_commit(true);
}

static void kb_key(void *dd, struct wl_keyboard *k, uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
	(void)dd; (void)k; (void)time;
	if (!B.xkb_state || state != WL_KEYBOARD_KEY_STATE_PRESSED)
		return;
	xkb_keysym_t sym = xkb_state_key_get_one_sym(B.xkb_state, key + 8);
	B.input_serial = serial;
	B.dirty = true;
	if (B.rename_idx >= 0) { /* saisie du nouveau nom */
		if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter)
			rename_commit(true);
		else if (sym == XKB_KEY_Escape)
			rename_commit(false);
		else if (sym == XKB_KEY_BackSpace && B.rename_fresh) {
			B.rename_buf[0] = '\0';
			B.rename_fresh = false;
		} else if (sym == XKB_KEY_Left || sym == XKB_KEY_Right || sym == XKB_KEY_End) {
			B.rename_fresh = false; /* garder le nom et écrire à la suite */
		} else if (sym == XKB_KEY_BackSpace) {
			size_t n = strlen(B.rename_buf);
			while (n && (B.rename_buf[n - 1] & 0xc0) == 0x80) /* caractère UTF-8 entier */
				n--;
			if (n)
				n--;
			B.rename_buf[n] = '\0';
		} else {
			char u[16] = "";
			xkb_state_key_get_utf8(B.xkb_state, key + 8, u, sizeof u);
			if ((unsigned char)u[0] >= 0x20 && u[0] != 0x7f && u[0] != '/') {
				if (B.rename_fresh)
					B.rename_buf[0] = '\0';
				B.rename_fresh = false;
				if (strlen(B.rename_buf) + strlen(u) < sizeof B.rename_buf - 1)
					strcat(B.rename_buf, u);
			}
		}
		return;
	}
	if (sym == XKB_KEY_Escape) {
		B.menu_open = false;
		clear_sel();
	} else if (sym == XKB_KEY_Delete) {
		do_action(A_TRASH);
	} else if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
		do_action(A_OPEN);
	} else if (sym == XKB_KEY_F2) {
		do_action(A_RENAME);
	} else if (sym == XKB_KEY_space) {
		preview_toggle();
	} else if (B.ctrl && (sym == XKB_KEY_c || sym == XKB_KEY_C)) {
		clip_set(false);
	} else if (B.ctrl && (sym == XKB_KEY_x || sym == XKB_KEY_X)) {
		clip_set(true);
	} else if (B.ctrl && (sym == XKB_KEY_v || sym == XKB_KEY_V)) {
		paste_start();
	} else if (B.ctrl && (sym == XKB_KEY_a || sym == XKB_KEY_A)) {
		for (int i = 0; i < B.n; i++)
			B.items[i].sel = true;
	}
}

static void kb_mods(void *dd, struct wl_keyboard *k, uint32_t serial, uint32_t dep, uint32_t lat, uint32_t lck,
		    uint32_t group)
{
	(void)dd; (void)k; (void)serial;
	if (!B.xkb_state)
		return;
	xkb_state_update_mask(B.xkb_state, dep, lat, lck, 0, 0, group);
	B.ctrl = xkb_state_mod_name_is_active(B.xkb_state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0;
}
static void kb_repeat(void *d, struct wl_keyboard *k, int32_t r, int32_t dl) { (void)d; (void)k; (void)r; (void)dl; }
static const struct wl_keyboard_listener kb_listener = {
	.keymap = kb_keymap, .enter = kb_enter, .leave = kb_leave, .key = kb_key, .modifiers = kb_mods,
	.repeat_info = kb_repeat,
};

static void seat_caps(void *d, struct wl_seat *s, uint32_t caps)
{
	(void)d;
	bool kb = caps & WL_SEAT_CAPABILITY_KEYBOARD, ptr = caps & WL_SEAT_CAPABILITY_POINTER;
	if (ptr && !B.pointer) {
		B.pointer = wl_seat_get_pointer(s);
		wl_pointer_add_listener(B.pointer, &pointer_listener, NULL);
	} else if (!ptr && B.pointer) {
		wl_pointer_release(B.pointer);
		B.pointer = NULL;
	}
	if (kb && !B.keyboard) {
		B.keyboard = wl_seat_get_keyboard(s);
		wl_keyboard_add_listener(B.keyboard, &kb_listener, NULL);
	} else if (!kb && B.keyboard) {
		wl_keyboard_release(B.keyboard);
		B.keyboard = NULL;
	}
}
static void seat_name(void *d, struct wl_seat *s, const char *n) { (void)d; (void)s; (void)n; }
static const struct wl_seat_listener seat_listener = { .capabilities = seat_caps, .name = seat_name };

/* ------------------------------ surface ------------------------------ */

static void layer_configure(void *d, struct zwlr_layer_surface_v1 *l, uint32_t serial, uint32_t w, uint32_t h)
{
	(void)d;
	zwlr_layer_surface_v1_ack_configure(l, serial);
	bool first = !B.configured;
	B.width = w;
	B.height = h;
	B.configured = true;
	if (first)
		load_items(); /* la grille dépend de la taille */
	B.dirty = true;
}
static void layer_closed(void *d, struct zwlr_layer_surface_v1 *l)
{
	(void)d; (void)l;
	B.running = false;
}
static const struct zwlr_layer_surface_v1_listener layer_listener = { .configure = layer_configure, .closed = layer_closed };

static uint32_t minv(uint32_t a, uint32_t b) { return a < b ? a : b; }

static void reg_global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t ver)
{
	(void)d;
	if (!strcmp(iface, wl_compositor_interface.name))
		B.compositor = wl_registry_bind(r, name, &wl_compositor_interface, minv(ver, 4));
	else if (!strcmp(iface, wl_shm_interface.name))
		B.shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
	else if (!strcmp(iface, wl_seat_interface.name) && !B.seat) {
		B.seat = wl_registry_bind(r, name, &wl_seat_interface, minv(ver, 4));
		wl_seat_add_listener(B.seat, &seat_listener, NULL);
	} else if (!strcmp(iface, wl_data_device_manager_interface.name)) {
		B.ddm_ver = minv(ver, 3);
		B.ddm = wl_registry_bind(r, name, &wl_data_device_manager_interface, B.ddm_ver);
	} else if (!strcmp(iface, zwlr_layer_shell_v1_interface.name)) {
		B.layer_ver = minv(ver, 4);
		B.layer_shell = wl_registry_bind(r, name, &zwlr_layer_shell_v1_interface, B.layer_ver);
	}
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg_listener = { .global = reg_global, .global_remove = reg_remove };

/* ------------------------------ main --------------------------------- */

int main(int argc, char **argv)
{
	if (argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
		printf("Usage : %s    les fichiers du dossier Bureau sur le bureau\n", argv[0]);
		return 0;
	}
	setlocale(LC_ALL, "");
	signal(SIGPIPE, SIG_IGN);
	signal(SIGCHLD, SIG_DFL);
	find_desktop_dir();

	B.display = wl_display_connect(NULL);
	if (!B.display) {
		fprintf(stderr, "plume-icones-bureau : pas de connexion Wayland\n");
		return 1;
	}
	B.xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct wl_registry *reg = wl_display_get_registry(B.display);
	wl_registry_add_listener(reg, &reg_listener, NULL);
	wl_display_roundtrip(B.display);
	if (!B.compositor || !B.shm || !B.layer_shell) {
		fprintf(stderr, "plume-icones-bureau : le compositeur ne gère pas wlr-layer-shell\n");
		return 1;
	}
	const char *xs = getenv("XCURSOR_SIZE");
	cursor_theme = wl_cursor_theme_load(getenv("XCURSOR_THEME"), xs && atoi(xs) > 0 ? atoi(xs) : 24, B.shm);
	cursor_surface = wl_compositor_create_surface(B.compositor);

	B.surface = wl_compositor_create_surface(B.compositor);
	B.layer = zwlr_layer_shell_v1_get_layer_surface(B.layer_shell, B.surface, NULL,
							ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM, "plume-icones-bureau");
	zwlr_layer_surface_v1_add_listener(B.layer, &layer_listener, NULL);
	zwlr_layer_surface_v1_set_anchor(B.layer, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
						  ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
	zwlr_layer_surface_v1_set_exclusive_zone(B.layer, 0); /* entre la barre et le dock */
	/* clavier quand on clique sur le bureau (Suppr, F2, Entrée…), puis rendu aux fenêtres */
	if (B.layer_ver >= 4)
		zwlr_layer_surface_v1_set_keyboard_interactivity(B.layer, 2 /* on_demand */);
	wl_surface_commit(B.surface);

	/* glisser-déposer avec les autres applis */
	if (B.ddm && B.seat) {
		B.dd = wl_data_device_manager_get_data_device(B.ddm, B.seat);
		wl_data_device_add_listener(B.dd, &dd_listener, NULL);
	}
	/* clés USB : le noyau signale chaque montage / démontage sur /proc/self/mounts */
	B.mounts_fd = open("/proc/self/mounts", O_RDONLY | O_CLOEXEC);

	B.ino = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (B.ino >= 0)
		inotify_add_watch(B.ino, B.dir, IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE |
						IN_ATTRIB | IN_DELETE_SELF | IN_MOVE_SELF);

	B.running = true;
	double reload_at = 0;
	while (B.running) {
		if (B.dirty && !B.frame_cb && B.configured)
			render();
		while (wl_display_prepare_read(B.display) != 0)
			wl_display_dispatch_pending(B.display);
		wl_display_flush(B.display);
		struct pollfd fds[5] = { { .fd = wl_display_get_fd(B.display), .events = POLLIN },
					 { .fd = B.ino, .events = POLLIN },
					 { .fd = B.drop_fd, .events = POLLIN },
					 { .fd = B.mounts_fd, .events = POLLPRI },
					 { .fd = B.paste_fd, .events = POLLIN } };
		int timeout = reload_at > 0 ? 150 : -1;
		if (B.dirty && !B.frame_cb)
			timeout = 0;
		while (waitpid(-1, NULL, WNOHANG) > 0) /* aperçus rapides refermés */
			;
		if (poll(fds, 5, timeout) < 0) {
			wl_display_cancel_read(B.display);
			if (errno == EINTR)
				continue;
			break;
		}
		if (fds[0].revents & POLLIN) {
			if (wl_display_read_events(B.display) < 0)
				break;
		} else {
			wl_display_cancel_read(B.display);
		}
		if (fds[0].revents & (POLLERR | POLLHUP))
			break;
		if (wl_display_dispatch_pending(B.display) < 0)
			break;
		if (B.ino >= 0 && (fds[1].revents & POLLIN)) {
			char buf[4096];
			while (read(B.ino, buf, sizeof buf) > 0)
				;
			reload_at = now_s() + 0.15; /* plusieurs changements d'un coup : une seule relecture */
		}
		if (B.drop_fd >= 0 && (fds[2].revents & (POLLIN | POLLHUP | POLLERR)))
			drop_read();
		if (B.paste_fd >= 0 && (fds[4].revents & (POLLIN | POLLHUP | POLLERR)))
			paste_read();
		if (B.mounts_fd >= 0 && (fds[3].revents & (POLLPRI | POLLERR))) {
			char buf[4096];
			lseek(B.mounts_fd, 0, SEEK_SET);
			while (read(B.mounts_fd, buf, sizeof buf) > 0)
				;
			reload_at = now_s() + 0.4; /* clé USB branchée ou retirée */
		}
		if (B.ino_reload) {
			B.ino_reload = false;
			reload_at = now_s() + 0.3;
		}
		if (reload_at > 0 && now_s() >= reload_at && B.drag == D_NONE && B.rename_idx < 0) {
			reload_at = 0;
			load_items();
		}
	}
	return 0;
}
