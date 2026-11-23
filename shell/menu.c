/*
 * plume-shell — menu du clic droit sur une appli du dock, comme sur Mac (Plume Void seulement,
 * -DPLUME_VOID) :
 *   appli ouverte : ses fenêtres (✓ = active ; clic = l'afficher) · Nouvelle fenêtre ·
 *                   Masquer / Afficher · Retirer du Dock (ou Garder dans le Dock si elle n'y est
 *                   pas épinglée) · Quitter
 *   appli fermée  : Ouvrir · Retirer du Dock
 * Le menu est dessiné dans la surface du dock, agrandie vers le haut le temps qu'il est ouvert.
 * Un clic ailleurs dans le dock, ou la souris qui quitte le dock, le referme.
 */
#include "shell.h"

#ifdef PLUME_VOID

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

struct menu M = { .hover = -1 };

/* ------------------------- fenêtres de l'appli ------------------------ */

static const char *node_app_id(struct json *n)
{
	const char *id = json_str(n, "app_id");
	if (!*id)
		id = json_str(json_get(n, "window_properties"), "class"); /* applis X11 */
	return id;
}

static void collect(struct json *n, const char *app_id, bool scratch)
{
	if (!n)
		return;
	const char *type = json_str(n, "type");
	if (!strcmp(type, "workspace") && !strcmp(json_str(n, "name"), "__i3_scratch"))
		scratch = true;
	if ((!strcmp(type, "con") || !strcmp(type, "floating_con")) && !strcasecmp(node_app_id(n), app_id)) {
		long id = (long)json_num(n, "id");
		if (scratch && M.nhidden < MENU_MAX)
			M.hidden[M.nhidden++] = id;
		if (M.nwin < MENU_WIN_MAX) {
			struct menu_item *it = &M.it[M.n++];
			memset(it, 0, sizeof *it);
			const char *t = json_str(n, "name");
			snprintf(it->label, sizeof it->label, "%s%s", scratch ? "◇ " : "", *t ? t : app_id);
			it->act = M_FOCUS;
			it->arg = id;
			it->check = json_bool(n, "focused");
			M.nwin++;
		}
	}
	const char *lists[2] = { "nodes", "floating_nodes" };
	for (int k = 0; k < 2; k++) {
		struct json *kids = json_get(n, lists[k]);
		for (struct json *c = kids ? kids->child : NULL; c; c = c->next)
			collect(c, app_id, scratch);
	}
}

static void add(const char *label, enum menu_act act, bool sep)
{
	if (M.n >= MENU_MAX)
		return;
	struct menu_item *it = &M.it[M.n++];
	memset(it, 0, sizeof *it);
	snprintf(it->label, sizeof it->label, "%s", label);
	it->act = act;
	it->sep_before = sep && M.n > 1;
}

/* -------------------------- taille du dock ---------------------------- */

static void resize_dock(struct output *o, bool big)
{
	if (!o || !o->dock.layer)
		return;
	zwlr_layer_surface_v1_set_size(o->dock.layer, 0, DOCK_SURF_H + (big ? MENU_EXTRA : 0));
	wl_surface_commit(o->dock.surface);
}

void menu_open(struct output *o, int app)
{
	if (app < 0 || app >= S.napps)
		return;
	struct app *a = &S.apps[app];
	if (a->power || !a->app_id[0] || a->mini_con) /* alimentation, fenêtre réduite : pas de menu */
		return;
	if (M.open)
		menu_close();
	M.n = M.nwin = M.nhidden = 0;
	M.hover = -1;
	M.app = app;
	M.out = o;
	if (a->trash) {
		add("Ouvrir", M_OPEN, false);
		add("Vider la corbeille…", M_EMPTY_TRASH, true);
	} else if (app_is_running(a)) {
		char *r = ipc_query(4, NULL); /* GET_TREE */
		struct json *j = json_parse(r);
		free(r);
		collect(j, a->app_id, false);
		json_free(j);
		if (a->cmd[0])
			add("Nouvelle fenêtre", M_NEW, true);
		if (M.nhidden > 0 && M.nhidden >= M.nwin)
			add("Afficher", M_SHOW, !a->cmd[0]);
		else
			add("Masquer", M_HIDE, !a->cmd[0]);
		if (a->extra)
			add("Garder dans le Dock", M_KEEP, false);
		else
			add("Retirer du Dock", M_REMOVE, false);
		add("Quitter", M_QUIT, true);
	} else {
		add("Ouvrir", M_OPEN, false);
		add("Retirer du Dock", M_REMOVE, true);
	}
	M.open = true;
	S.hover_app = -1; /* pas d'info-bulle par-dessus le menu */
	resize_dock(o, true);
	o->dock.dirty = true;
}

void menu_close(void)
{
	if (!M.open)
		return;
	M.open = false;
	struct output *o = M.out;
	M.out = NULL;
	resize_dock(o, false);
	if (o)
		o->dock.dirty = true;
	dock_compose(); /* les fenêtres ont pu changer pendant que le menu était ouvert */
}

/* ------------------------- Retirer du Dock ----------------------------- */

/* réécrit ~/.config/plume/dock.conf (copie de /etc/plume/dock.conf au besoin) sans cette appli */
static void dock_remove(const char *label)
{
	const char *home = getenv("HOME");
	if (!home)
		return;
	char user[512], tmp[520];
	snprintf(user, sizeof user, "%s/.config/plume/dock.conf", home);
	FILE *in = fopen(user, "r");
	if (!in)
		in = fopen("/etc/plume/dock.conf", "r");
	if (!in)
		return;
	static char buf[16384];
	size_t len = fread(buf, 1, sizeof buf - 1, in);
	fclose(in);
	buf[len] = '\0';

	char dir[512];
	snprintf(dir, sizeof dir, "%s/.config", home);
	mkdir(dir, 0755);
	snprintf(dir, sizeof dir, "%s/.config/plume", home);
	mkdir(dir, 0755);
	snprintf(tmp, sizeof tmp, "%s.tmp", user);
	FILE *out = fopen(tmp, "w");
	if (!out)
		return;
	for (char *l = buf; *l;) {
		char *e = strchr(l, '\n');
		size_t n = e ? (size_t)(e - l) : strlen(l);
		bool keep = true;
		/* 2e champ = nom affiché : « glyphe | nom | commande | app_id » */
		char *b1 = l[0] != '#' ? strstr(l, " | ") : NULL;
		if (b1 && b1 < l + n) {
			char *nom = b1 + 3, *b2 = strstr(nom, " | ");
			size_t k = strlen(label);
			if (b2 && b2 < l + n && (size_t)(b2 - nom) == k && !strncmp(nom, label, k))
				keep = false;
		}
		if (keep) {
			fwrite(l, 1, n, out);
			fputc('\n', out);
		}
		l += n + (e ? 1 : 0);
	}
	fclose(out);
	rename(tmp, user);
	apps_load();
}

void dock_remove_label(const char *label)
{
	dock_remove(label);
	mark_dirty(SURF_DOCK);
}

/* glisser une icône : sa ligne de dock.conf passe juste avant celle de BEFORE (NULL : en dernier
 * des épinglées, avant le séparateur de ⏻) */
void dock_move(const char *label, const char *before_label)
{
	const char *home = getenv("HOME");
	if (!home || (before_label && !strcmp(label, before_label)))
		return;
	char user[512], tmp[520], dir[512];
	snprintf(user, sizeof user, "%s/.config/plume/dock.conf", home);
	FILE *in = fopen(user, "r");
	if (!in)
		in = fopen("/etc/plume/dock.conf", "r");
	if (!in)
		return;
	static char buf[16384];
	size_t len = fread(buf, 1, sizeof buf - 1, in);
	fclose(in);
	buf[len] = '\0';
	/* découper en lignes */
	static char *lines[512];
	int nl = 0;
	for (char *l = buf; *l && nl < 512;) {
		char *e = strchr(l, '\n');
		lines[nl++] = l;
		if (!e)
			break;
		*e = '\0';
		l = e + 1;
	}
	/* nom affiché d'une ligne « glyphe | nom | commande | app_id » */
	int moved = -1, before = -1, power = -1;
	for (int i = 0; i < nl; i++) {
		char *l = lines[i];
		if (l[0] == '#')
			continue;
		if (strstr(l, "@power") == l)
			power = i;
		char *b1 = strstr(l, " | ");
		if (!b1)
			continue;
		char *nom = b1 + 3, *b2 = strstr(nom, " | ");
		size_t k = b2 ? (size_t)(b2 - nom) : strlen(nom);
		if (strlen(label) == k && !strncmp(nom, label, k))
			moved = i;
		if (before_label && strlen(before_label) == k && !strncmp(nom, before_label, k))
			before = i;
	}
	if (moved < 0)
		return;
	if (before < 0) { /* en dernier : avant le « --- » qui précède ⏻ (ou avant ⏻) */
		before = power >= 0 ? power : nl;
		while (before > 0 && before - 1 != moved && !strcmp(lines[before - 1], "---"))
			before--;
	}
	snprintf(dir, sizeof dir, "%s/.config", home);
	mkdir(dir, 0755);
	snprintf(dir, sizeof dir, "%s/.config/plume", home);
	mkdir(dir, 0755);
	snprintf(tmp, sizeof tmp, "%s.tmp", user);
	FILE *out = fopen(tmp, "w");
	if (!out)
		return;
	for (int i = 0; i <= nl; i++) {
		if (i == before)
			fprintf(out, "%s\n", lines[moved]);
		if (i < nl && i != moved)
			fprintf(out, "%s\n", lines[i]);
	}
	fclose(out);
	rename(tmp, user);
	apps_load();
	mark_dirty(SURF_DOCK);
}

/* Garder dans le Dock : ajoute l'appli à la fin des épinglées (avant le séparateur de ⏻) */
static void dock_keep(const struct app *a)
{
	const char *home = getenv("HOME");
	if (!home)
		return;
	char user[512], tmp[520], dir[512];
	snprintf(user, sizeof user, "%s/.config/plume/dock.conf", home);
	FILE *in = fopen(user, "r");
	if (!in)
		in = fopen("/etc/plume/dock.conf", "r");
	static char buf[16384];
	size_t len = in ? fread(buf, 1, sizeof buf - 1, in) : 0;
	if (in)
		fclose(in);
	buf[len] = '\0';
	snprintf(dir, sizeof dir, "%s/.config", home);
	mkdir(dir, 0755);
	snprintf(dir, sizeof dir, "%s/.config/plume", home);
	mkdir(dir, 0755);
	snprintf(tmp, sizeof tmp, "%s.tmp", user);
	FILE *out = fopen(tmp, "w");
	if (!out)
		return;
	/* insérer juste avant le dernier « --- » qui précède « @power » (sinon : à la fin) */
	char *power = strstr(buf, "@power"), *at = NULL;
	for (char *p = strstr(buf, "---"); p && (!power || p < power); p = strstr(p + 3, "---"))
		if (p == buf || p[-1] == '\n')
			at = p;
	char line[400];
	snprintf(line, sizeof line, "%s | %s | %s | %s\n", a->glyph, a->label, a->cmd[0] ? a->cmd : a->app_id, a->app_id);
	if (at) {
		fwrite(buf, 1, (size_t)(at - buf), out);
		fputs(line, out);
		fputs(at, out);
	} else {
		fputs(buf, out);
		if (len && buf[len - 1] != '\n')
			fputc('\n', out);
		fputs(line, out);
	}
	fclose(out);
	rename(tmp, user);
	apps_load();
}

/* ------------------------------ souris -------------------------------- */

void menu_motion(double x, double y)
{
	int h = -1;
	for (int i = 0; i < M.n; i++) {
		struct rect r = M.it[i].r;
		if (r.w > 0 && x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h)
			h = i;
	}
	if (h != M.hover) {
		M.hover = h;
		if (M.out)
			M.out->dock.dirty = true;
	}
}

void menu_click(double x, double y)
{
	menu_motion(x, y);
	int i = M.hover;
	if (i < 0 || i >= M.n || M.app < 0 || M.app >= S.napps) {
		menu_close(); /* clic à côté : on referme, sans rien faire d'autre (comme sur Mac) */
		return;
	}
	struct menu_item it = M.it[i];
	struct app a = S.apps[M.app];
	long hidden[MENU_MAX];
	int nhidden = M.nhidden;
	memcpy(hidden, M.hidden, sizeof hidden);
	menu_close();
	switch (it.act) {
	case M_FOCUS:
		ipc_command("[con_id=%ld] focus", it.arg);
		break;
	case M_OPEN:
	case M_NEW:
		spawn(a.cmd);
		break;
	case M_HIDE:
		ipc_command("[app_id=\"^%s$\"] move scratchpad", a.app_id);
		ipc_command("[class=\"^%s$\"] move scratchpad", a.app_id);
		break;
	case M_SHOW:
		for (int k = 0; k < nhidden; k++)
			ipc_command("[con_id=%ld] focus", hidden[k]);
		break;
	case M_REMOVE:
		dock_remove(a.label);
		mark_dirty(SURF_DOCK);
		break;
	case M_KEEP:
		dock_keep(&a);
		mark_dirty(SURF_DOCK);
		break;
	case M_EMPTY_TRASH:
		spawn("plume-corbeille --vider");
		break;
	case M_QUIT:
		ipc_command("[app_id=\"^%s$\"] kill", a.app_id);
		ipc_command("[class=\"^%s$\"] kill", a.app_id);
		break;
	}
}

#else
typedef int plume_menu_absent; /* version sans menu (Alpine) */
#endif
