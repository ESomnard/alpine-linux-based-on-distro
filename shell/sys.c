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

void sys_update(void)
{
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
			return;
	}
	parse_dock(DEFAULT_DOCK);
}

bool app_is_running(const struct app *a)
{
	if (!a->app_id[0])
		return false;
	for (int i = 0; i < S.nrunning; i++)
		if (!strcasecmp(S.running[i], a->app_id))
			return true;
	return false;
}
