/* plume-hud — informations système (CPU, RAM, uptime, batterie, réseau). */
#include "hud.h"

#include <dirent.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
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

static int read_cpu(struct sysinfo_hud *s)
{
	FILE *f = fopen("/proc/stat", "r");
	if (!f)
		return 0;
	unsigned long long v[8] = { 0 };
	int n = fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
		       &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]);
	fclose(f);
	if (n < 4)
		return 0;
	unsigned long long idle = v[3] + v[4], total = 0;
	for (int i = 0; i < 8; i++)
		total += v[i];
	unsigned long long di = idle - s->prev_idle, dt = total - s->prev_total;
	s->prev_idle = idle;
	s->prev_total = total;
	if (dt == 0)
		return 0;
	int pct = (int)((1.0 - (double)di / (double)dt) * 100.0 + 0.5);
	return pct < 0 ? 0 : pct > 100 ? 100 : pct;
}

static int read_ram(void)
{
	FILE *f = fopen("/proc/meminfo", "r");
	if (!f)
		return 0;
	char key[64];
	unsigned long long val, total = 0, avail = 0;
	while (fscanf(f, "%63s %llu kB\n", key, &val) == 2) {
		if (!strcmp(key, "MemTotal:"))
			total = val;
		else if (!strcmp(key, "MemAvailable:"))
			avail = val;
	}
	fclose(f);
	if (!total)
		return 0;
	return (int)((1.0 - (double)avail / (double)total) * 100.0 + 0.5);
}

static void read_power(struct sysinfo_hud *s)
{
	s->bat = -1;
	bool ac = false, charging = false;
	DIR *d = opendir("/sys/class/power_supply");
	if (!d)
		return;
	struct dirent *e;
	char path[512], buf[64];
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.')
			continue;
		snprintf(path, sizeof path, "/sys/class/power_supply/%s/type", e->d_name);
		if (!read_line(path, buf, sizeof buf))
			continue;
		if (!strcmp(buf, "Battery")) {
			snprintf(path, sizeof path, "/sys/class/power_supply/%s/capacity", e->d_name);
			if (read_line(path, buf, sizeof buf) && s->bat < 0)
				s->bat = atoi(buf);
			snprintf(path, sizeof path, "/sys/class/power_supply/%s/status", e->d_name);
			if (read_line(path, buf, sizeof buf) && (!strcmp(buf, "Charging") || !strcmp(buf, "Full")))
				charging = true;
		} else if (!strcmp(buf, "Mains")) {
			snprintf(path, sizeof path, "/sys/class/power_supply/%s/online", e->d_name);
			if (read_line(path, buf, sizeof buf) && !strcmp(buf, "1"))
				ac = true;
		}
	}
	closedir(d);
	s->charging = charging || ac;
}

static bool read_online(void)
{
	DIR *d = opendir("/sys/class/net");
	if (!d)
		return false;
	struct dirent *e;
	char path[512], buf[32];
	bool up = false;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.' || !strcmp(e->d_name, "lo"))
			continue;
		snprintf(path, sizeof path, "/sys/class/net/%s/operstate", e->d_name);
		if (read_line(path, buf, sizeof buf) && !strcmp(buf, "up"))
			up = true;
	}
	closedir(d);
	return up;
}

void first_human_user(char *out, size_t cap)
{
	out[0] = '\0';
	setpwent();
	struct passwd *pw;
	while ((pw = getpwent())) {
		if (pw->pw_uid >= 1000 && pw->pw_uid < 60000) {
			snprintf(out, cap, "%s", pw->pw_name);
			break;
		}
	}
	endpwent();
}

void sys_init(struct hud *h)
{
	struct sysinfo_hud *s = &h->sys;
	gethostname(s->host, sizeof s->host - 1);
	struct utsname u;
	if (uname(&u) == 0) {
		snprintf(s->os, sizeof s->os, "Plume %s", u.release);
		snprintf(s->arch, sizeof s->arch, "%s", u.machine);
	}
	long c = sysconf(_SC_NPROCESSORS_ONLN);
	s->cores = c > 0 ? (int)c : 1;
	s->first = true;
	read_cpu(s);
	sys_update(h);
	s->cpu_disp = 0;
	s->ram_disp = 0;
	s->bat_disp = 0;
}

void sys_update(struct hud *h)
{
	struct sysinfo_hud *s = &h->sys;
	s->cpu = read_cpu(s);
	s->ram = read_ram();
	char buf[64];
	if (read_line("/proc/uptime", buf, sizeof buf))
		s->uptime = atol(buf);

	bool was_charging = s->charging, was_online = s->online;
	int had_bat = s->bat;
	read_power(s);
	s->online = read_online();
	if (!s->first) {
		if (had_bat >= 0 && s->bat >= 0 && was_charging != s->charging)
			hud_log(h, MSG_NORMAL, s->charging ? "Alimentation secteur connectée"
							  : "Alimentation secteur déconnectée");
		if (was_online != s->online) {
			if (s->online)
				hud_log(h, MSG_NORMAL, "Connexion réseau rétablie");
			else
				hud_log(h, MSG_ERR, "Connexion réseau perdue");
		}
	}
	s->first = false;
}

/* Transition douce des jauges (équivalent du `transition: width .8s` CSS) */
void sys_animate(struct hud *h, double dt)
{
	struct sysinfo_hud *s = &h->sys;
	double k = dt / 0.25;
	if (k > 1)
		k = 1;
	s->cpu_disp += (s->cpu - s->cpu_disp) * k;
	s->ram_disp += (s->ram - s->ram_disp) * k;
	s->bat_disp += ((s->bat < 0 ? 0 : s->bat) - s->bat_disp) * k;
}
