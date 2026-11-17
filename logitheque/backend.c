/* Logithèque Plume — catalogue, index Alpine, Flatpak, recherche, tâches, mot de passe. */
#include "logitheque.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define PW_KEEP 300.0 /* le mot de passe est oublié après 5 min */

static const char *FLATHUB_URL = "https://dl.flathub.org/repo/flathub.flatpakrepo";

enum { Q_INDEX = 1, Q_FP_LIST, Q_FP_REMOTES, Q_FP_SEARCH, Q_APK_VERSION, Q_FP_UPDATES };

/* ------------------------------ outils ------------------------------- */

static char *trim(char *s)
{
	while (*s == ' ' || *s == '\t')
		s++;
	char *e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r'))
		*--e = '\0';
	return s;
}

static int cmp_str(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

static int cmp_pkg(const void *a, const void *b)
{
	return strcmp(((const struct pkg *)a)->name, ((const struct pkg *)b)->name);
}

static bool in_path(const char *prog)
{
	const char *path = getenv("PATH");
	char buf[1024];
	for (const char *p = path ? path : "/usr/bin:/bin"; *p;) {
		size_t n = strcspn(p, ":");
		snprintf(buf, sizeof buf, "%.*s/%s", (int)n, p, prog);
		if (access(buf, X_OK) == 0)
			return true;
		p += n + (p[n] == ':');
	}
	return false;
}

/* gestionnaire de paquets : xbps (Plume Void, glibc) ou apk (Plume Alpine, musl) */
bool pm_xbps(void)
{
	static int x = -1;
	if (x < 0)
		x = access("/var/db/xbps", F_OK) == 0 || getenv("PLUME_XBPS") != NULL;
	return x;
}

const char *pm_distro(void) { return pm_xbps() ? "Void" : "Alpine"; }

/* « nom-1.2.3-r0 » (apk) ou « nom-1.2.3_1 » (xbps) -> nom + version */
static void split_nv(const char *nv, char *name, size_t ncap, char *ver, size_t vcap)
{
	size_t n = strlen(nv);
	if (pm_xbps()) {
		/* xbps : la version est après le dernier « - » (elle n'en contient jamais) */
		const char *d = strrchr(nv, '-');
		if (!d) {
			snprintf(name, ncap, "%s", nv);
			if (vcap) ver[0] = '\0';
		} else {
			snprintf(name, ncap, "%.*s", (int)(d - nv), nv);
			if (vcap) snprintf(ver, vcap, "%s", d + 1);
		}
		return;
	}
	/* cherche « -r<chiffres> » à la fin, puis le « - » qui précède la version */
	const char *r = NULL;
	for (const char *p = nv + n; p > nv; p--)
		if (p[-1] == '-' && p[0] == 'r' && isdigit((unsigned char)p[1])) {
			r = p - 1;
			break;
		}
	const char *v = NULL;
	if (r)
		for (const char *p = r; p > nv; p--)
			if (p[-1] == '-' && isdigit((unsigned char)p[0])) {
				v = p - 1;
				break;
			}
	if (!v) {
		snprintf(name, ncap, "%s", nv);
		if (vcap) ver[0] = '\0';
		return;
	}
	snprintf(name, ncap, "%.*s", (int)(v - nv), nv);
	if (vcap)
		snprintf(ver, vcap, "%s", v + 1);
}

static bool icontains(const char *hay, const char *needle)
{
	if (!*needle)
		return true;
	size_t n = strlen(hay), m = strlen(needle);
	for (size_t i = 0; i + m <= n; i++)
		if (!strncasecmp(hay + i, needle, m))
			return true;
	return false;
}

/* ----------------------------- catalogue ----------------------------- */

static void load_catalog(void)
{
	const char *paths[] = { getenv("PLUME_LOGITHEQUE_CATALOG"), "/usr/local/share/plume/logitheque.txt",
				"/usr/share/plume/logitheque.txt", "catalogue.txt" };
	FILE *f = NULL;
	for (size_t i = 0; i < sizeof paths / sizeof paths[0] && !f; i++)
		if (paths[i])
			f = fopen(paths[i], "r");
	if (!f)
		return;
	char line[1024];
	while (fgets(line, sizeof line, f) && A.ncatalog < MAX_CATALOG) {
		char *s = trim(line);
		if (!*s || *s == '#')
			continue;
		char *fld[6] = { 0 };
		int nf = 0;
		char *p = s;
		while (nf < 6) {
			char *bar = nf < 5 ? strchr(p, '|') : NULL;
			if (bar)
				*bar = '\0';
			fld[nf++] = trim(p);
			if (!bar)
				break;
			p = bar + 1;
		}
		if (nf < 6)
			continue;
		struct entry *e = &A.catalog[A.ncatalog++];
		snprintf(e->name, sizeof e->name, "%s", fld[0]);
		snprintf(e->cat, sizeof e->cat, "%s", fld[1]);
		snprintf(e->apk, sizeof e->apk, "%s", fld[2]);
		snprintf(e->flatpak, sizeof e->flatpak, "%s", fld[3]);
		snprintf(e->term_cmd, sizeof e->term_cmd, "%s", fld[4]);
		snprintf(e->summary, sizeof e->summary, "%s", fld[5]);
	}
	fclose(f);
}

/* ---------------------------- index Alpine --------------------------- */

static void free_index(void)
{
	for (int i = 0; i < A.npkgs; i++) {
		free(A.pkgs[i].name);
		free(A.pkgs[i].version);
		free(A.pkgs[i].desc);
	}
	free(A.pkgs);
	A.pkgs = NULL;
	A.npkgs = 0;
}

static void index_done(struct proc *p, int status)
{
	(void)status;
	free_index();
	int cap = 0;
	for (char *line = strtok(p->out, "\n"); line; line = strtok(NULL, "\n")) {
		char *sep, *desc;
		if (pm_xbps()) {
			/* « [-] nom-1.0_1      description » */
			if (strlen(line) < 5 || line[0] != '[')
				continue;
			line += 4;
			sep = strchr(line, ' ');
			if (!sep)
				continue;
			*sep = '\0';
			desc = trim(sep + 1);
		} else {
			sep = strstr(line, " - ");
			if (!sep)
				continue;
			*sep = '\0';
			desc = sep + 3;
		}
		char name[128], ver[64];
		split_nv(line, name, sizeof name, ver, sizeof ver);
		if (A.npkgs == cap) {
			cap = cap ? cap * 2 : 4096;
			struct pkg *np = realloc(A.pkgs, sizeof *np * cap);
			if (!np)
				break;
			A.pkgs = np;
		}
		struct pkg *k = &A.pkgs[A.npkgs++];
		k->name = strdup(name);
		k->version = strdup(ver);
		k->desc = strdup(desc);
	}
	qsort(A.pkgs, A.npkgs, sizeof *A.pkgs, cmp_pkg);
	A.index_ready = true;
	search_update();
}

static void load_index(void)
{
	char *argv[] = { "apk", "search", "-v", NULL };
	char *xargv[] = { "xbps-query", "-Rs", "", NULL };
	proc_run(pm_xbps() ? xargv : argv, NULL, index_done, Q_INDEX, false);
}

const struct pkg *pkg_find(const char *name)
{
	if (!A.npkgs || !name || !*name)
		return NULL;
	struct pkg key = { .name = (char *)name };
	return bsearch(&key, A.pkgs, A.npkgs, sizeof *A.pkgs, cmp_pkg);
}

/* ------------------------- paquets installés ------------------------- */

static void free_list(char ***list, int *n)
{
	for (int i = 0; i < *n; i++)
		free((*list)[i]);
	free(*list);
	*list = NULL;
	*n = 0;
}

static void push(char ***list, int *n, int *cap, const char *s)
{
	if (*n == *cap) {
		*cap = *cap ? *cap * 2 : 256;
		char **nl = realloc(*list, sizeof *nl * *cap);
		if (!nl)
			return;
		*list = nl;
	}
	(*list)[(*n)++] = strdup(s);
}

void load_installed(void)
{
	free_list(&A.installed, &A.ninstalled);
	free_list(&A.world, &A.nworld);
	int cap = 0;
	if (pm_xbps()) {
		/* « ii nom-1.0_1  description » ; demandés à la main : xbps-query -m */
		char line[512], name[160];
		FILE *q = popen("xbps-query -l 2>/dev/null", "r");
		while (q && fgets(line, sizeof line, q)) {
			char nv[200];
			if (sscanf(line, "%*s %199s", nv) == 1) {
				split_nv(nv, name, sizeof name, NULL, 0);
				push(&A.installed, &A.ninstalled, &cap, name);
			}
		}
		if (q)
			pclose(q);
		qsort(A.installed, A.ninstalled, sizeof *A.installed, cmp_str);
		cap = 0;
		q = popen("xbps-query -m 2>/dev/null", "r");
		while (q && fgets(line, sizeof line, q)) {
			split_nv(trim(line), name, sizeof name, NULL, 0);
			if (*name)
				push(&A.world, &A.nworld, &cap, name);
		}
		if (q)
			pclose(q);
		qsort(A.world, A.nworld, sizeof *A.world, cmp_str);
		return;
	}
	FILE *f = fopen("/lib/apk/db/installed", "r");
	if (f) {
		char line[512];
		while (fgets(line, sizeof line, f))
			if (line[0] == 'P' && line[1] == ':')
				push(&A.installed, &A.ninstalled, &cap, trim(line + 2));
		fclose(f);
	}
	qsort(A.installed, A.ninstalled, sizeof *A.installed, cmp_str);
	cap = 0;
	f = fopen("/etc/apk/world", "r");
	if (f) {
		char line[512];
		while (fgets(line, sizeof line, f)) {
			char *s = trim(line);
			s[strcspn(s, "<>=~@ ")] = '\0'; /* « paquet>=1.0 », « paquet@edge » */
			if (*s)
				push(&A.world, &A.nworld, &cap, s);
		}
		fclose(f);
	}
	qsort(A.world, A.nworld, sizeof *A.world, cmp_str);
}

bool apk_installed(const char *name)
{
	return name && *name && A.ninstalled &&
	       bsearch(&name, A.installed, A.ninstalled, sizeof *A.installed, cmp_str) != NULL;
}

bool apk_in_world(const char *name)
{
	return name && *name && A.nworld && bsearch(&name, A.world, A.nworld, sizeof *A.world, cmp_str) != NULL;
}

/* ------------------------------ Flatpak ------------------------------ */

static void fp_list_done(struct proc *p, int status)
{
	A.nfp_installed = 0;
	if (status != 0)
		return;
	for (char *line = strtok(p->out, "\n"); line && A.nfp_installed < MAX_RESULTS * 2; line = strtok(NULL, "\n")) {
		char *f[4] = { 0 };
		int n = 0;
		for (char *t = line; n < 4; n++) {
			f[n] = t;
			char *tab = strchr(t, '\t');
			if (!tab) {
				n++;
				break;
			}
			*tab = '\0';
			t = tab + 1;
		}
		if (n < 2 || !strchr(f[0], '.'))
			continue;
		struct result *r = &A.fp_installed[A.nfp_installed++];
		memset(r, 0, sizeof *r);
		r->kind = K_FLATPAK;
		snprintf(r->id, sizeof r->id, "%s", f[0]);
		snprintf(r->name, sizeof r->name, "%s", f[1]);
		if (n > 2 && f[2]) snprintf(r->version, sizeof r->version, "%s", f[2]);
		/* « summary » sert ici à retenir l'installation : user / system */
		if (n > 3 && f[3]) snprintf(r->summary, sizeof r->summary, "%s", f[3]);
	}
}

static void fp_remotes_done(struct proc *p, int status)
{
	A.flathub_ready = status == 0 && strstr(p->out, "flathub") != NULL;
	request_redraw();
}

void refresh_flatpak(void)
{
	A.flatpak_present = in_path("flatpak");
	if (!A.flatpak_present) {
		A.flathub_ready = false;
		A.nfp_installed = 0;
		return;
	}
	char *l[] = { "flatpak", "list", "--app", "--columns=application,name,version,installation", NULL };
	proc_run(l, NULL, fp_list_done, Q_FP_LIST, false);
	char *r[] = { "flatpak", "remotes", "--columns=name", NULL };
	proc_run(r, NULL, fp_remotes_done, Q_FP_REMOTES, false);
}

bool fp_is_installed(const char *id, bool *user)
{
	for (int i = 0; i < A.nfp_installed; i++)
		if (!strcmp(A.fp_installed[i].id, id)) {
			if (user)
				*user = strcmp(A.fp_installed[i].summary, "system") != 0;
			return true;
		}
	return false;
}

/* ------------------------------ recherche ---------------------------- */

static void fp_search_done(struct proc *p, int status)
{
	A.fp_searching = false;
	A.nfp_results = 0;
	if (status != 0) {
		request_redraw();
		return;
	}
	for (char *line = strtok(p->out, "\n"); line && A.nfp_results < MAX_RESULTS; line = strtok(NULL, "\n")) {
		char *f[5] = { 0 };
		int n = 0;
		for (char *t = line; n < 5; n++) {
			f[n] = t;
			char *tab = strchr(t, '\t');
			if (!tab) {
				n++;
				break;
			}
			*tab = '\0';
			t = tab + 1;
		}
		if (n < 4 || !strchr(f[0], '.'))
			continue;
		if (n >= 5 && f[4] && !strstr(f[4], "flathub"))
			continue;
		/* les applis du catalogue apparaissent déjà plus haut */
		bool dup = false;
		for (int i = 0; i < A.ncatalog && !dup; i++)
			dup = !strcmp(A.catalog[i].flatpak, f[0]);
		if (dup)
			continue;
		struct result *r = &A.fp_results[A.nfp_results++];
		memset(r, 0, sizeof *r);
		r->kind = K_FLATPAK;
		snprintf(r->id, sizeof r->id, "%s", f[0]);
		snprintf(r->name, sizeof r->name, "%s", f[1]);
		snprintf(r->summary, sizeof r->summary, "%s", f[2]);
		snprintf(r->version, sizeof r->version, "%s", f[3]);
	}
	request_redraw();
}

static int rank(const char *name, const char *q)
{
	size_t m = strlen(q);
	if (!strcasecmp(name, q)) return 0;
	if (!strncasecmp(name, q, m)) return 1;
	return 2;
}

/* recherche dans l'index Alpine (instantanée, en mémoire) */
void search_update(void)
{
	A.napk_results = 0;
	if (!A.query[0] || !A.index_ready)
		return;
	/* trois passes : nom exact, nom qui commence par, nom qui contient */
	for (int pass = 0; pass < 3 && A.napk_results < MAX_RESULTS; pass++)
		for (int i = 0; i < A.npkgs && A.napk_results < MAX_RESULTS; i++) {
			struct pkg *k = &A.pkgs[i];
			if (!icontains(k->name, A.query) || rank(k->name, A.query) != pass)
				continue;
			/* on cache les sous-paquets techniques, sauf recherche explicite */
			const char *suffix[] = { "-doc", "-dev", "-lang", "-dbg", "-static", "-openrc", "-libs",
						 "-bash-completion", "-zsh-completion", "-fish-completion", "-pyc" };
			bool tech = false;
			for (size_t s = 0; s < sizeof suffix / sizeof suffix[0]; s++) {
				size_t ln = strlen(k->name), ls = strlen(suffix[s]);
				if (ln > ls && !strcmp(k->name + ln - ls, suffix[s]) && !strstr(A.query, suffix[s]))
					tech = true;
			}
			if (tech)
				continue;
			bool dup = false;
			for (int c = 0; c < A.ncatalog && !dup; c++)
				dup = !strcmp(A.catalog[c].apk, k->name);
			if (dup)
				continue;
			struct result *r = &A.apk_results[A.napk_results++];
			memset(r, 0, sizeof *r);
			r->kind = K_APK;
			snprintf(r->id, sizeof r->id, "%s", k->name);
			snprintf(r->name, sizeof r->name, "%s", k->name);
			snprintf(r->version, sizeof r->version, "%s", k->version);
			snprintf(r->summary, sizeof r->summary, "%s", k->desc);
		}
	request_redraw();
}

/* recherche Flathub : lancée 0,4 s après la dernière frappe */
void tick_search(void)
{
	if (!A.flathub_ready || !A.query[0] || A.fp_searching || !strcmp(A.fp_query, A.query))
		return;
	if (now_s() - A.query_t < 0.4)
		return;
	snprintf(A.fp_query, sizeof A.fp_query, "%s", A.query);
	char q[128];
	snprintf(q, sizeof q, "%s", A.query);
	char *argv[] = { "flatpak", "search", "--columns=application,name,description,version,remotes", q, NULL };
	if (proc_run(argv, NULL, fp_search_done, Q_FP_SEARCH, false))
		A.fp_searching = true;
}

/* -------------------------- mises à jour ----------------------------- */

static void apk_version_done(struct proc *p, int status)
{
	(void)status;
	/* garde les mises à jour Flatpak déjà trouvées */
	int k = 0;
	for (int i = 0; i < A.nupdates; i++)
		if (A.updates[i].kind == K_FLATPAK)
			A.updates[k++] = A.updates[i];
	A.nupdates = k;
	for (char *line = strtok(p->out, "\n"); line && A.nupdates < 256; line = strtok(NULL, "\n")) {
		char nv[160], op[8], to[64];
		if (pm_xbps()) {
			/* « nom-2.0_1 update x86_64 dépôt … » (version installée : non donnée) */
			if (sscanf(line, "%159s %7s", nv, op) != 2 || strcmp(op, "update"))
				continue;
			struct update_row *u = &A.updates[A.nupdates++];
			u->kind = K_APK;
			split_nv(nv, u->name, sizeof u->name, u->to, sizeof u->to);
			u->from[0] = '\0';
			continue;
		}
		if (sscanf(line, "%159s %7s %63s", nv, op, to) != 3 || strcmp(op, "<"))
			continue;
		struct update_row *u = &A.updates[A.nupdates++];
		u->kind = K_APK;
		split_nv(nv, u->name, sizeof u->name, u->from, sizeof u->from);
		snprintf(u->to, sizeof u->to, "%s", to);
	}
	A.updates_checked = true;
	request_redraw();
}

static void fp_updates_done(struct proc *p, int status)
{
	if (status != 0)
		return;
	for (char *line = strtok(p->out, "\n"); line && A.nupdates < 256; line = strtok(NULL, "\n")) {
		char *tab = strchr(line, '\t');
		if (tab)
			*tab = '\0';
		if (!strchr(line, '.'))
			continue;
		struct update_row *u = &A.updates[A.nupdates++];
		memset(u, 0, sizeof *u);
		u->kind = K_FLATPAK;
		snprintf(u->name, sizeof u->name, "%s", line);
		if (tab)
			snprintf(u->to, sizeof u->to, "%s", tab + 1);
	}
	request_redraw();
}

static void list_updates(void)
{
	A.nupdates = 0;
	char *a[] = { "apk", "version", "-l", "<", NULL };
	char *xa[] = { "xbps-install", "-un", NULL };
	proc_run(pm_xbps() ? xa : a, NULL, apk_version_done, Q_APK_VERSION, false);
	if (A.flathub_ready) {
		char *f[] = { "flatpak", "remote-ls", "--updates", "--app", "--columns=application,version", NULL };
		proc_run(f, NULL, fp_updates_done, Q_FP_UPDATES, false);
	}
}

void check_updates(void)
{
	queue_job(J_APK_UPDATE, "");
}

void update_all(void)
{
	queue_job(J_APK_UPGRADE, "");
	if (A.flathub_ready)
		queue_job(J_FP_UPDATE, "");
}

void enable_flathub(void)
{
	if (!A.flatpak_present)
		queue_job(J_ENABLE_FLATPAK, "flatpak");
	else
		queue_job(J_ADD_FLATHUB, "");
}

/* ------------------------------- tâches ------------------------------ */

static bool job_is_root(enum job_kind k)
{
	return k == J_APK_ADD || k == J_APK_DEL || k == J_ENABLE_FLATPAK || k == J_APK_UPDATE ||
	       k == J_APK_UPGRADE || k == J_FP_REMOVE_SYS;
}

void queue_job(enum job_kind kind, const char *arg)
{
	if (A.njobs >= MAX_JOBS)
		return;
	struct job *j = &A.jobs[A.njobs++];
	memset(j, 0, sizeof *j);
	j->kind = kind;
	j->root = job_is_root(kind) && getuid() != 0;
	snprintf(j->arg, sizeof j->arg, "%s", arg ? arg : "");
	switch (kind) {
	case J_APK_ADD: snprintf(j->title, sizeof j->title, "Installation de %s", arg); break;
	case J_APK_DEL: snprintf(j->title, sizeof j->title, "Suppression de %s", arg); break;
	case J_FP_INSTALL: snprintf(j->title, sizeof j->title, "Installation de %s (Flathub)", arg); break;
	case J_FP_REMOVE: case J_FP_REMOVE_SYS: snprintf(j->title, sizeof j->title, "Suppression de %s", arg); break;
	case J_ENABLE_FLATPAK: snprintf(j->title, sizeof j->title, "Installation de Flatpak"); break;
	case J_ADD_FLATHUB: snprintf(j->title, sizeof j->title, "Ajout de Flathub"); break;
	case J_FP_APPSTREAM: snprintf(j->title, sizeof j->title, "Téléchargement du catalogue Flathub"); break;
	case J_APK_UPDATE: snprintf(j->title, sizeof j->title, "Recherche des mises à jour"); break;
	case J_APK_UPGRADE: snprintf(j->title, sizeof j->title, "Mise à jour du système"); break;
	case J_FP_UPDATE: snprintf(j->title, sizeof j->title, "Mise à jour des applis Flathub"); break;
	}
	jobs_pump();
	request_redraw();
}

static bool output_says_bad_password(const char *out)
{
	return strstr(out, "incorrect password") || strstr(out, "Sorry, try again") ||
	       strstr(out, "no password was provided") || strstr(out, "a password is required");
}

static void job_done(struct proc *p, int status)
{
	struct job j = A.current;
	A.job_running = false;
	if (j.root && output_says_bad_password(p->out)) {
		/* mauvais mot de passe : on redemande, la tâche reste en tête */
		memset(A.pw_cached, 0, sizeof A.pw_cached);
		memmove(&A.jobs[1], &A.jobs[0], sizeof A.jobs[0] * A.njobs);
		A.jobs[0] = j;
		A.njobs++;
		A.pw_error = true;
		A.pw_open = true;
		request_redraw();
		return;
	}
	if (j.root)
		A.pw_cached_t = now_s(); /* le mot de passe est bon : on le garde 5 min */
	if (status == 0) {
		switch (j.kind) {
		case J_APK_ADD: set_toast(false, "%s installé", j.arg); break;
		case J_APK_DEL: set_toast(false, "%s supprimé", j.arg); break;
		case J_FP_INSTALL: set_toast(false, "%s installé", j.arg); break;
		case J_FP_REMOVE: case J_FP_REMOVE_SYS: set_toast(false, "%s supprimé", j.arg); break;
		case J_ENABLE_FLATPAK: queue_job(J_ADD_FLATHUB, ""); break;
		case J_ADD_FLATHUB: queue_job(J_FP_APPSTREAM, ""); break;
		case J_FP_APPSTREAM: set_toast(false, "Flathub est prêt : cherche une appli"); A.fp_query[0] = '\0'; break;
		case J_APK_UPDATE: load_index(); list_updates(); break;
		case J_APK_UPGRADE: set_toast(false, "Système à jour"); A.updates_checked = false; break;
		case J_FP_UPDATE: set_toast(false, "Applis Flathub à jour"); break;
		}
	} else {
		/* dernière ligne non vide de la sortie, pour comprendre l'erreur */
		char last[160] = "";
		for (char *line = strtok(p->out, "\n"); line; line = strtok(NULL, "\n")) {
			char *s = trim(line);
			if (*s)
				snprintf(last, sizeof last, "%s", s);
		}
		set_toast(true, "Échec : %s — %s", j.title, last[0] ? last : "erreur inconnue");
	}
	load_installed();
	refresh_flatpak();
	jobs_pump();
}

static void start_job(struct job *j)
{
	char *cmd[16] = { 0 };
	int n = 0;
	char *remote_url = (char *)FLATHUB_URL;
	bool x = pm_xbps();
	switch (j->kind) {
	case J_APK_ADD:
		if (x) { cmd[n++] = "xbps-install"; cmd[n++] = "-y"; cmd[n++] = j->arg; break; }
		cmd[n++] = "apk"; cmd[n++] = "add"; cmd[n++] = j->arg; break;
	case J_APK_DEL:
		if (x) { cmd[n++] = "xbps-remove"; cmd[n++] = "-Ry"; cmd[n++] = j->arg; break; }
		cmd[n++] = "apk"; cmd[n++] = "del"; cmd[n++] = j->arg; break;
	case J_ENABLE_FLATPAK:
		cmd[n++] = x ? "xbps-install" : "apk"; cmd[n++] = x ? "-y" : "add"; cmd[n++] = "flatpak"; break;
	case J_APK_UPDATE:
		if (x) { cmd[n++] = "xbps-install"; cmd[n++] = "-S"; break; }
		cmd[n++] = "apk"; cmd[n++] = "update"; break;
	case J_APK_UPGRADE:
		if (x) { cmd[n++] = "xbps-install"; cmd[n++] = "-Syu"; break; }
		cmd[n++] = "apk"; cmd[n++] = "upgrade"; cmd[n++] = "-U"; break;
	case J_FP_INSTALL:
		cmd[n++] = "flatpak"; cmd[n++] = "install"; cmd[n++] = "--user"; cmd[n++] = "-y";
		cmd[n++] = "--noninteractive"; cmd[n++] = "flathub"; cmd[n++] = j->arg; break;
	case J_FP_REMOVE:
		cmd[n++] = "flatpak"; cmd[n++] = "uninstall"; cmd[n++] = "--user"; cmd[n++] = "-y";
		cmd[n++] = "--noninteractive"; cmd[n++] = j->arg; break;
	case J_FP_REMOVE_SYS:
		cmd[n++] = "flatpak"; cmd[n++] = "uninstall"; cmd[n++] = "--system"; cmd[n++] = "-y";
		cmd[n++] = "--noninteractive"; cmd[n++] = j->arg; break;
	case J_ADD_FLATHUB:
		cmd[n++] = "flatpak"; cmd[n++] = "remote-add"; cmd[n++] = "--user"; cmd[n++] = "--if-not-exists";
		cmd[n++] = "flathub"; cmd[n++] = remote_url; break;
	case J_FP_APPSTREAM:
		cmd[n++] = "flatpak"; cmd[n++] = "update"; cmd[n++] = "--user"; cmd[n++] = "--appstream";
		cmd[n++] = "-y"; cmd[n++] = "--noninteractive"; break;
	case J_FP_UPDATE:
		cmd[n++] = "flatpak"; cmd[n++] = "update"; cmd[n++] = "--user"; cmd[n++] = "-y";
		cmd[n++] = "--noninteractive"; break;
	}
	char *argv[24] = { 0 };
	int k = 0;
	char stdin_buf[160] = "";
	if (j->root) {
		/* sudo lit le mot de passe sur l'entrée standard ; -k : jamais de cache système */
		argv[k++] = "sudo"; argv[k++] = "-S"; argv[k++] = "-k"; argv[k++] = "-p"; argv[k++] = ""; argv[k++] = "--";
		snprintf(stdin_buf, sizeof stdin_buf, "%s\n", A.pw_cached);
	}
	for (int i = 0; i < n; i++)
		argv[k++] = cmd[i];
	A.current = *j;
	A.job_running = true;
	A.job_line[0] = '\0';
	A.job_progress = -1;
	A.job_start = now_s();
	if (!proc_run(argv, j->root ? stdin_buf : NULL, job_done, 0, true)) {
		A.job_running = false;
		set_toast(true, "Impossible de lancer la tâche");
	}
	memset(stdin_buf, 0, sizeof stdin_buf);
	request_redraw();
}

void jobs_pump(void)
{
	if (A.job_running || A.njobs == 0 || A.pw_open)
		return;
	/* mot de passe oublié au bout de 5 min */
	if (A.pw_cached[0] && now_s() - A.pw_cached_t > PW_KEEP && A.pw_cached_t > 0)
		memset(A.pw_cached, 0, sizeof A.pw_cached);
	struct job *j = &A.jobs[0];
	if (j->root && !A.pw_cached[0]) {
		A.pw_open = true;
		A.pw[0] = '\0';
		request_redraw();
		return;
	}
	struct job cur = *j;
	memmove(&A.jobs[0], &A.jobs[1], sizeof A.jobs[0] * (A.njobs - 1));
	A.njobs--;
	start_job(&cur);
}

void password_submitted(void)
{
	snprintf(A.pw_cached, sizeof A.pw_cached, "%s", A.pw);
	A.pw_cached_t = 0; /* sera daté quand sudo l'aura accepté */
	memset(A.pw, 0, sizeof A.pw);
	A.pw_open = false;
	A.pw_error = false;
	jobs_pump();
}

void password_cancelled(void)
{
	memset(A.pw, 0, sizeof A.pw);
	A.pw_open = false;
	A.pw_error = false;
	/* on abandonne les tâches qui demandent les droits administrateur */
	int k = 0;
	for (int i = 0; i < A.njobs; i++)
		if (!A.jobs[i].root)
			A.jobs[k++] = A.jobs[i];
	A.njobs = k;
	set_toast(true, "Annulé : il faut le mot de passe administrateur");
	jobs_pump();
}

/* ------------------------------ éléments ----------------------------- */

bool item_has_apk(const struct item *it, const char **name)
{
	const char *n = NULL;
	if (it->kind == K_CATALOG)
		n = A.catalog[it->idx].apk[0] ? A.catalog[it->idx].apk : NULL;
	else if (it->kind == K_APK)
		n = it->r.id;
	/* paquet absent de l'index (autre architecture…) : on le cache, sauf s'il est installé */
	if (n && A.index_ready && !pkg_find(n) && !apk_installed(n))
		n = NULL;
	if (name)
		*name = n;
	return n != NULL;
}

bool item_has_flatpak(const struct item *it, const char **id)
{
	const char *n = NULL;
	if (it->kind == K_CATALOG)
		n = A.catalog[it->idx].flatpak[0] ? A.catalog[it->idx].flatpak : NULL;
	else if (it->kind == K_FLATPAK)
		n = it->r.id;
	if (id)
		*id = n;
	return n != NULL;
}

/* source 0 : Alpine, 1 : Flathub */
bool item_installed(const struct item *it, int source)
{
	const char *n;
	if (source == 0)
		return item_has_apk(it, &n) && apk_installed(n);
	return item_has_flatpak(it, &n) && fp_is_installed(n, NULL);
}

void item_title(const struct item *it, char *name, size_t ncap, char *summary, size_t scap)
{
	if (it->kind == K_CATALOG) {
		snprintf(name, ncap, "%s", A.catalog[it->idx].name);
		snprintf(summary, scap, "%s", A.catalog[it->idx].summary);
	} else {
		snprintf(name, ncap, "%s", it->r.name);
		snprintf(summary, scap, "%s", it->r.summary);
	}
}

void install_item(const struct item *it, int source)
{
	const char *n;
	if (source == 0 && item_has_apk(it, &n))
		queue_job(J_APK_ADD, n);
	else if (source == 1 && item_has_flatpak(it, &n)) {
		if (!A.flathub_ready) {
			set_toast(true, "Active d'abord Flathub (bouton dans Explorer)");
			return;
		}
		queue_job(J_FP_INSTALL, n);
	}
}

void remove_item(const struct item *it, int source)
{
	const char *n;
	if (source == 0 && item_has_apk(it, &n)) {
		if (!apk_in_world(n)) {
			set_toast(true, "%s est nécessaire à un autre paquet : impossible de le retirer seul", n);
			return;
		}
		queue_job(J_APK_DEL, n);
	} else if (source == 1 && item_has_flatpak(it, &n)) {
		bool user = true;
		fp_is_installed(n, &user);
		queue_job(user ? J_FP_REMOVE : J_FP_REMOVE_SYS, n);
	}
}

/* « Ouvrir » : lanceur .desktop du paquet, sinon commande dans le terminal */
void launch_item(const struct item *it, int source)
{
	const char *n;
	char cmdline[600];
	if (source == 1 && item_has_flatpak(it, &n)) {
		snprintf(cmdline, sizeof cmdline, "flatpak run %s", n);
		spawn_detached(cmdline);
		return;
	}
	if (!item_has_apk(it, &n))
		return;
	/* fichiers du paquet : on cherche son .desktop */
	char cmd[200];
	snprintf(cmd, sizeof cmd, "apk info -L '%s' 2>/dev/null", n);
	FILE *f = popen(cmd, "r");
	char path[512], exec[512] = "";
	while (f && fgets(path, sizeof path, f)) {
		char *s = trim(path);
		if (strncmp(s, "usr/share/applications/", 23) || !strstr(s, ".desktop"))
			continue;
		char full[600];
		snprintf(full, sizeof full, "/%s", s);
		FILE *d = fopen(full, "r");
		char line[600];
		bool in_entry = false, nodisplay = false;
		char ex[512] = "";
		while (d && fgets(line, sizeof line, d)) {
			char *l = trim(line);
			if (l[0] == '[')
				in_entry = !strcmp(l, "[Desktop Entry]");
			else if (in_entry && !strncmp(l, "Exec=", 5) && !ex[0])
				snprintf(ex, sizeof ex, "%s", l + 5);
			else if (in_entry && !strcmp(l, "NoDisplay=true"))
				nodisplay = true;
		}
		if (d)
			fclose(d);
		if (ex[0] && (!exec[0] || !nodisplay))
			snprintf(exec, sizeof exec, "%s", ex);
		if (exec[0] && !nodisplay)
			break;
	}
	if (f)
		pclose(f);
	if (exec[0]) {
		/* retire les codes %U %f … */
		char clean[512];
		size_t k = 0;
		for (const char *p = exec; *p && k < sizeof clean - 1; p++) {
			if (*p == '%' && p[1]) {
				p++;
				continue;
			}
			clean[k++] = *p;
		}
		clean[k] = '\0';
		spawn_detached(clean);
		set_toast(false, "Ouverture de %s", n);
		return;
	}
	const char *term = it->kind == K_CATALOG ? A.catalog[it->idx].term_cmd : "";
	if (term[0]) {
		snprintf(cmdline, sizeof cmdline, "plume-terminal %s", term);
		spawn_detached(cmdline);
		return;
	}
	set_toast(true, "%s n'a pas de fenêtre : utilise-le dans le terminal", n);
}

/* ------------------------------ départ ------------------------------- */

void backend_init(void)
{
	load_catalog();
	load_installed();
	load_index();
	refresh_flatpak();
}
