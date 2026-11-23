/* plume-shell — informations système (CPU, RAM, réseau, batterie, son, saisie) et dock. */
#include "shell.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

static bool read_line(const char *path, char *out, size_t cap)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return false;
	bool ok = fgets(out, cap, f) != NULL;
	fclose(f);
	if (ok)
		out[strcspn(out, "\n")] = '\0';
	return ok;
}

static void read_cpu(void)
{
	FILE *f = fopen("/proc/stat", "r");
	if (!f)
		return;
	unsigned long long v[8] = { 0 };
	int n = fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
		       &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]);
	fclose(f);
	if (n < 4)
		return;
	unsigned long long idle = v[3] + v[4], total = 0;
	for (int i = 0; i < 8; i++)
		total += v[i];
	unsigned long long di = idle - S.prev_idle, dt = total - S.prev_total;
	S.prev_idle = idle;
	S.prev_total = total;
	if (!dt)
		return;
	int pct = (int)((1.0 - (double)di / dt) * 100 + 0.5);
	S.cpu = pct < 0 ? 0 : pct > 100 ? 100 : pct;
	memmove(S.cpu_hist, S.cpu_hist + 1, sizeof(int) * (CPU_HIST - 1));
	S.cpu_hist[CPU_HIST - 1] = S.cpu;
}

static void read_ram(void)
{
	FILE *f = fopen("/proc/meminfo", "r");
	if (!f)
		return;
	char k[64];
	unsigned long long v, total = 0, avail = 0;
	while (fscanf(f, "%63s %llu kB\n", k, &v) == 2) {
		if (!strcmp(k, "MemTotal:")) total = v;
		else if (!strcmp(k, "MemAvailable:")) avail = v;
	}
	fclose(f);
	if (total)
		S.ram = (int)((1.0 - (double)avail / total) * 100 + 0.5);
}

static void read_power(void)
{
	S.bat = -1;
	bool chg = false, ac = false;
	DIR *d = opendir("/sys/class/power_supply");
	if (!d)
		return;
	struct dirent *e;
	char p[512], b[64];
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.')
			continue;
		snprintf(p, sizeof p, "/sys/class/power_supply/%s/type", e->d_name);
		if (!read_line(p, b, sizeof b))
			continue;
		if (!strcmp(b, "Battery")) {
			snprintf(p, sizeof p, "/sys/class/power_supply/%s/capacity", e->d_name);
			if (read_line(p, b, sizeof b) && S.bat < 0)
				S.bat = atoi(b);
			snprintf(p, sizeof p, "/sys/class/power_supply/%s/status", e->d_name);
			if (read_line(p, b, sizeof b) && (!strcmp(b, "Charging") || !strcmp(b, "Full")))
				chg = true;
		} else if (!strcmp(b, "Mains")) {
			snprintf(p, sizeof p, "/sys/class/power_supply/%s/online", e->d_name);
			if (read_line(p, b, sizeof b) && !strcmp(b, "1"))
				ac = true;
		}
	}
	closedir(d);
	S.charging = chg || ac;
}

static void read_net(void)
{
	S.online = S.wifi = false;
	DIR *d = opendir("/sys/class/net");
	if (!d)
		return;
	struct dirent *e;
	char p[512], b[32];
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.' || !strcmp(e->d_name, "lo"))
			continue;
		snprintf(p, sizeof p, "/sys/class/net/%s/operstate", e->d_name);
		if (!read_line(p, b, sizeof b) || strcmp(b, "up"))
			continue;
		S.online = true;
		snprintf(p, sizeof p, "/sys/class/net/%s/wireless", e->d_name);
		if (access(p, F_OK) == 0)
			S.wifi = true;
	}
	closedir(d);
	/* Bluetooth : segment affiché seulement s'il y a un adaptateur */
	S.bt = access("/sys/class/bluetooth/hci0", F_OK) == 0;
}

/* mises à jour : compte écrit par « plume-maj --verifier » (root, via sudo sans
 * mot de passe pour cette seule commande) ; vérifié 2 min après le démarrage
 * puis toutes les 6 h. Notification quand le nombre change. */
#define MAJ_FICHIER "/var/lib/plume/maj-dispo"
static void read_maj(void)
{
	static double next_check = -1;
	double t = now_s();
	if (next_check < 0)
		next_check = t + 120;
	if (t >= next_check) {
		next_check = t + 6 * 3600;
		spawn("command -v plume-maj >/dev/null && sudo -n /usr/bin/plume-maj --verifier >/dev/null 2>&1");
	}
	char b[16];
	int n = read_line(MAJ_FICHIER, b, sizeof b) ? atoi(b) : 0;
	static int last;
	if (n > 0 && n != last) {
		char cmd[200];
		snprintf(cmd, sizeof cmd,
			 "notify-send -a Plume 'Mises à jour disponibles' '%d paquet(s) — clic sur ⬆ MAJ dans la barre'", n);
		spawn(cmd);
	}
	last = n;
	S.maj = n;

	/* nouvelle version d'Alpine publiée (plume-maj --nouvelle-version) */
	char v[16];
	if (!read_line("/var/lib/plume/version-dispo", v, sizeof v))
		v[0] = 0;
	static char last_v[16];
	if (v[0] && strcmp(v, last_v)) {
		char cmd[220];
		snprintf(cmd, sizeof cmd,
			 "notify-send -a Plume 'Alpine %s est sortie' 'Clic sur ★ dans la barre pour y passer'", v);
		spawn(cmd);
	}
	snprintf(last_v, sizeof last_v, "%s", v);
	snprintf(S.maj_version, sizeof S.maj_version, "%s", v);
}

static void read_volume(void)
{
	S.vol = -1;
	FILE *f = popen("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null", "r");
	if (!f)
		return;
	char line[128] = "";
	if (fgets(line, sizeof line, f)) {
		double v;
		if (sscanf(line, "Volume: %lf", &v) == 1)
			S.vol = (int)(v * 100 + 0.5);
		S.muted = strstr(line, "MUTED") != NULL;
	}
	pclose(f);
}

static void read_ime(void)
{
	FILE *f = popen("ibus engine 2>/dev/null", "r");
	if (!f)
		return;
	char line[64] = "";
	if (fgets(line, sizeof line, f) && !strncmp(line, "libpinyin", 9))
		snprintf(S.kb, sizeof S.kb, "拼音");
	pclose(f);
}

#ifdef PLUME_VOID
/* corbeille vide ou pleine (icône du dock) */
static void read_trash(void)
{
	const char *home = getenv("HOME");
	char p[512];
	snprintf(p, sizeof p, "%s/.local/share/Trash/files", home ? home : "");
	int n = 0;
	DIR *d = opendir(p);
	struct dirent *e;
	while (d && (e = readdir(d)))
		if (e->d_name[0] != '.' || (e->d_name[1] && strcmp(e->d_name, "..")))
			n++;
	if (d)
		closedir(d);
	if ((n > 0) != (S.ntrash > 0)) {
		S.ntrash = n;
		dock_compose();
	}
	S.ntrash = n;
}
#endif

void sys_update(void)
{
#ifdef PLUME_VOID
	read_trash();
#endif
	read_cpu();
	read_ram();
	read_power();
	read_net();
	read_volume();
	read_maj();
	ipc_refresh_inputs();
	read_ime();
	mark_dirty(SURF_BAR);
}

/* ------------------------------- dock -------------------------------- */

static const char *DEFAULT_DOCK =
	">_ | Terminal | plume-terminal | plume-term\n"
	"◎ | Firefox | firefox | firefox\n"
	"▤ | Fichiers | pcmanfm | pcmanfm\n"
	"✎ | Éditeur | plume-terminal -a micro micro | micro\n"
	"▶ | Vidéo et musique | mpv --player-operation-mode=pseudo-gui | mpv\n"
	"◧ | PDF | zathura | org.pwmt.zathura\n"
	"◈ | Poste de pilotage | plume-term --hud -a plume-pilotage | plume-pilotage\n"
	"▦ | Logithèque | plume-logitheque | plume-logitheque\n"
	"⣿ | Toutes les applications | plume-menu | plume-lanceur\n"
	"---\n"
	"@power | Éteindre / redémarrer | swaynag -t warning -m 'Que faire ?' "
	"-B 'Éteindre' 'sudo -n /sbin/poweroff' -B 'Redémarrer' 'sudo -n /sbin/reboot' "
	"-B 'Veille' 'sudo -n /usr/sbin/zzz' -B 'Déconnexion' 'swaymsg exit' |\n";

static char *trim(char *s)
{
	while (*s == ' ' || *s == '\t')
		s++;
	char *e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r'))
		*--e = '\0';
	return s;
}

static void parse_dock(const char *text)
{
	S.napps = 0;
	bool sep = false;
	char *dup = strdup(text), *save = NULL;
	for (char *line = strtok_r(dup, "\n", &save); line && S.napps < MAX_APPS; line = strtok_r(NULL, "\n", &save)) {
		char *l = trim(line);
		if (!*l || *l == '#')
			continue;
		if (!strcmp(l, "---")) {
			sep = true;
			continue;
		}
		char *f[4] = { 0 };
		int n = 0;
		for (char *p = l; n < 4; n++) {
			f[n] = p;
			char *bar = strstr(p, " | ");
			if (!bar) {
				/* dernier champ, éventuellement terminé par « |» */
				char *last = strrchr(p, '|');
				if (last && last[1] == '\0')
					*last = '\0';
				n++;
				break;
			}
			*bar = '\0';
			p = bar + 3;
		}
		if (n < 3)
			continue;
		struct app *a = &S.apps[S.napps++];
		memset(a, 0, sizeof *a);
		char *glyph = trim(f[0]);
		a->power = !strcmp(glyph, "@power");
		snprintf(a->glyph, sizeof a->glyph, "%s", a->power ? "" : glyph);
		snprintf(a->label, sizeof a->label, "%s", trim(f[1]));
		snprintf(a->cmd, sizeof a->cmd, "%s", trim(f[2]));
		snprintf(a->app_id, sizeof a->app_id, "%s", n > 3 && f[3] ? trim(f[3]) : "");
		a->separator_before = sep;
		sep = false;
	}
	free(dup);
}

/* ~/.config/plume/dock.conf, sinon /etc/plume/dock.conf, sinon la liste intégrée */
void apps_load(void)
{
	char path[512];
	const char *home = getenv("HOME");
	const char *candidates[2] = { NULL, "/etc/plume/dock.conf" };
	if (home) {
		snprintf(path, sizeof path, "%s/.config/plume/dock.conf", home);
		candidates[0] = path;
	}
	for (int i = 0; i < 2; i++) {
		if (!candidates[i])
			continue;
		FILE *f = fopen(candidates[i], "r");
		if (!f)
			continue;
		static char buf[16384];
		size_t n = fread(buf, 1, sizeof buf - 1, f);
		fclose(f);
		buf[n] = '\0';
		parse_dock(buf);
		if (S.napps > 0)
			break;
	}
	if (S.napps == 0)
		parse_dock(DEFAULT_DOCK);
#ifdef PLUME_VOID
	memcpy(S.cfg, S.apps, sizeof S.cfg);
	S.ncfg = S.napps;
	dock_compose();
#endif
}

#ifdef PLUME_VOID
#pragma GCC diagnostic ignored "-Wformat-truncation" /* noms et chemins coupés : voulu */
/* ------------- zone du milieu du dock : applis ouvertes non épinglées ------------- */

/* nom et commande d'une appli d'après son fichier .desktop (même nom que l'app_id,
 * ou StartupWMClass=app_id) ; gardés en mémoire */
static struct { char id[64], name[48], cmd[256], icon[96]; } cache[64];
static int ncache;

static bool desktop_read(const char *path, const char *id, bool by_class, char *name, char *cmd, char *icon)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return false;
	char l[1024], nm[48] = "", ex[256] = "", ic[96] = "";
	bool cls = !by_class, in_entry = false;
	while (fgets(l, sizeof l, f)) {
		l[strcspn(l, "\r\n")] = '\0';
		if (l[0] == '[') {
			in_entry = !strcmp(l, "[Desktop Entry]");
			continue;
		}
		if (!in_entry)
			continue;
		if (!strncmp(l, "Name=", 5) && !nm[0])
			snprintf(nm, sizeof nm, "%s", l + 5);
		else if (!strncmp(l, "Exec=", 5) && !ex[0])
			snprintf(ex, sizeof ex, "%s", l + 5);
		else if (!strncmp(l, "Icon=", 5) && !ic[0])
			snprintf(ic, sizeof ic, "%s", l + 5);
		else if (!strncmp(l, "StartupWMClass=", 15) && !strcasecmp(l + 15, id))
			cls = true;
	}
	fclose(f);
	if (!cls || !nm[0])
		return false;
	/* Exec sans les codes %U, %f… */
	for (char *p = strchr(ex, '%'); p; p = strchr(p, '%')) {
		if (p[1]) memmove(p, p + 2, strlen(p + 2) + 1);
		else *p = '\0';
	}
	size_t k = strlen(ex);
	while (k && ex[k - 1] == ' ')
		ex[--k] = '\0';
	snprintf(name, 48, "%s", nm);
	snprintf(cmd, 256, "%s", ex);
	snprintf(icon, 96, "%s", ic);
	return true;
}

void desktop_info(const char *id, char *name, char *cmd, char *icon)
{
	for (int i = 0; i < ncache; i++)
		if (!strcmp(cache[i].id, id)) {
			snprintf(name, 48, "%s", cache[i].name);
			snprintf(cmd, 256, "%s", cache[i].cmd);
			snprintf(icon, 96, "%s", cache[i].icon);
			return;
		}
	const char *home = getenv("HOME");
	char dirs[4][512];
	snprintf(dirs[0], sizeof dirs[0], "%s/.local/share/applications", home ? home : "/nonexistent");
	snprintf(dirs[1], sizeof dirs[1], "/usr/share/applications");
	snprintf(dirs[2], sizeof dirs[2], "%s/.local/share/flatpak/exports/share/applications", home ? home : "/nonexistent");
	snprintf(dirs[3], sizeof dirs[3], "/var/lib/flatpak/exports/share/applications");
	bool found = false;
	/* 1) fichier au nom de l'appli ; 2) StartupWMClass */
	for (int d = 0; d < 4 && !found; d++) {
		char p[700];
		snprintf(p, sizeof p, "%s/%s.desktop", dirs[d], id);
		found = desktop_read(p, id, false, name, cmd, icon);
	}
	for (int d = 0; d < 4 && !found; d++) {
		DIR *dir = opendir(dirs[d]);
		struct dirent *e;
		while (dir && !found && (e = readdir(dir))) {
			size_t n = strlen(e->d_name);
			if (n < 9 || strcmp(e->d_name + n - 8, ".desktop"))
				continue;
			char p[800];
			snprintf(p, sizeof p, "%s/%s", dirs[d], e->d_name);
			found = desktop_read(p, id, true, name, cmd, icon);
		}
		if (dir)
			closedir(dir);
	}
	if (!found) {
		snprintf(name, 48, "%s", id);
		cmd[0] = '\0';
		icon[0] = '\0';
	}
	if (ncache < 64) {
		snprintf(cache[ncache].id, sizeof cache[0].id, "%s", id);
		snprintf(cache[ncache].name, sizeof cache[0].name, "%s", name);
		snprintf(cache[ncache].cmd, sizeof cache[0].cmd, "%s", cmd);
		snprintf(cache[ncache].icon, sizeof cache[0].icon, "%s", icon);
		ncache++;
	}
}

/* 1re lettre d'un nom, en majuscule (icône de secours) */
static void glyph_of(const char *label, char *g, size_t cap)
{
	unsigned char c = (unsigned char)label[0];
	if (c < 0x80)
		snprintf(g, cap, "%c", c >= 'a' && c <= 'z' ? c - 32 : c);
	else
		snprintf(g, cap, "%.*s", c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : 2, label);
}

void dock_compose(void)
{
	if (M.open) /* pas pendant que le menu du clic droit montre une appli */
		return;
	struct app nv[MAX_APPS];
	int n = 0;
	/* 1) épinglées */
	for (int i = 0; i < S.ncfg && n < MAX_APPS; i++)
		if (!S.cfg[i].power)
			nv[n++] = S.cfg[i];
	/* 2) ouvertes non épinglées, après un séparateur */
	bool first = true;
	for (int r = 0; r < S.nrunning && n < MAX_APPS - 2; r++) {
		const char *id = S.running[r];
		bool pinned = !*id;
		for (int i = 0; i < S.ncfg && !pinned; i++)
			if (!strcasecmp(S.cfg[i].app_id, id))
				pinned = true;
		if (pinned)
			continue;
		struct app a;
		memset(&a, 0, sizeof a);
		a.extra = true;
		a.separator_before = first;
		first = false;
		snprintf(a.app_id, sizeof a.app_id, "%s", id);
		char ic[96];
		desktop_info(id, a.label, a.cmd, ic);
		glyph_of(a.label, a.glyph, sizeof a.glyph);
		nv[n++] = a;
	}
	/* 3) fenêtres réduites (pastille jaune), comme sur Mac : à droite, avant la corbeille */
	first = true;
	for (int m = 0; m < S.nmini && n < MAX_APPS - 2; m++) {
		struct app a;
		memset(&a, 0, sizeof a);
		a.extra = true;
		a.mini_con = S.mini[m].id;
		a.separator_before = first;
		first = false;
		snprintf(a.app_id, sizeof a.app_id, "%s", S.mini[m].app_id);
		snprintf(a.label, sizeof a.label, "%s", S.mini[m].title[0] ? S.mini[m].title : S.mini[m].app_id);
		glyph_of(a.label, a.glyph, sizeof a.glyph);
		nv[n++] = a;
	}
	/* 4) corbeille */
	if (n < MAX_APPS - 1) {
		struct app a;
		memset(&a, 0, sizeof a);
		a.trash = true;
		a.separator_before = true;
		snprintf(a.label, sizeof a.label, "%s", S.ntrash ? "Corbeille" : "Corbeille (vide)");
		snprintf(a.app_id, sizeof a.app_id, "%s", S.ntrash ? "@corbeille-pleine" : "@corbeille");
		snprintf(a.cmd, sizeof a.cmd, "plume-corbeille");
		nv[n++] = a;
	}
	/* 5) ⏻ (et ce qui suit), toujours séparé */
	for (int i = 0; i < S.ncfg && n < MAX_APPS; i++)
		if (S.cfg[i].power) {
			nv[n] = S.cfg[i];
			nv[n++].separator_before = true;
		}
	/* garder l'animation de survol des applis qui restent */
	bool same = n == S.napps;
	for (int i = 0; i < n; i++) {
		for (int j = 0; j < S.napps; j++)
			if (!strcmp(nv[i].label, S.apps[j].label) && !strcmp(nv[i].app_id, S.apps[j].app_id)) {
				nv[i].hover = S.apps[j].hover;
				break;
			}
		if (same && (strcmp(nv[i].label, S.apps[i].label) || nv[i].separator_before != S.apps[i].separator_before ||
			     strcmp(nv[i].app_id, S.apps[i].app_id) || nv[i].mini_con != S.apps[i].mini_con))
			same = false;
		for (int j = 0; j < S.napps; j++) /* le rebond continue */
			if (!strcmp(nv[i].label, S.apps[j].label) && !strcmp(nv[i].app_id, S.apps[j].app_id))
				nv[i].bounce_t = S.apps[j].bounce_t;
	}
	if (same)
		return;
	memcpy(S.apps, nv, sizeof nv[0] * n);
	S.napps = n;
	dock_relayout();
}
#endif

bool app_is_running(const struct app *a)
{
	if (!a->app_id[0])
		return false;
#ifdef PLUME_VOID
	if (a->trash || a->mini_con)
		return false;
#endif
	for (int i = 0; i < S.nrunning; i++)
		if (!strcasecmp(S.running[i], a->app_id))
			return true;
	return false;
}
