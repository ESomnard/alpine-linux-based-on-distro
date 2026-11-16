/* plume-term (mode HUD) — mesures : performances, machine, réseau, stockage.
 * La partie performances vient de l'ancien plume-deck. */
#include "term.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <unistd.h>

/* --- processus : temps CPU précédent par pid (table simple) --- */
#define PT_SIZE 4096
static struct { pid_t pid; unsigned long long t; } prev_pt[PT_SIZE];
static unsigned long long prev_total_all;

static void push(double *h, double v)
{
	memmove(h, h + 1, sizeof(double) * (HIST - 1));
	h[HIST - 1] = v;
}

static void read_cpu(struct perf *p)
{
	FILE *f = fopen("/proc/stat", "r");
	if (!f)
		return;
	char line[512];
	int idx = 0;
	p->ncores = 0;
	while (fgets(line, sizeof line, f) && !strncmp(line, "cpu", 3)) {
		unsigned long long v[8] = { 0 };
		char name[16];
		if (sscanf(line, "%15s %llu %llu %llu %llu %llu %llu %llu %llu", name,
			   &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]) < 5)
			continue;
		unsigned long long idle = v[3] + v[4], total = 0;
		for (int i = 0; i < 8; i++)
			total += v[i];
		unsigned long long dt = total - p->prev_total[idx], di = idle - p->prev_idle[idx];
		int pct = dt ? (int)((1.0 - (double)di / dt) * 100 + 0.5) : 0;
		pct = pct < 0 ? 0 : pct > 100 ? 100 : pct;
		p->prev_total[idx] = total;
		p->prev_idle[idx] = idle;
		if (idx == 0) {
			p->cpu = pct;
			prev_total_all = dt;
		} else if (p->ncores < MAX_CORES) {
			p->core[p->ncores++] = pct;
		}
		if (++idx > MAX_CORES)
			break;
	}
	fclose(f);
	push(p->cpu_hist, p->cpu);
}

static void read_mem(struct perf *p)
{
	FILE *f = fopen("/proc/meminfo", "r");
	if (!f)
		return;
	char k[64];
	unsigned long long v, tot = 0, avail = 0, st = 0, sf = 0;
	while (fscanf(f, "%63s %llu kB\n", k, &v) == 2) {
		if (!strcmp(k, "MemTotal:")) tot = v;
		else if (!strcmp(k, "MemAvailable:")) avail = v;
		else if (!strcmp(k, "SwapTotal:")) st = v;
		else if (!strcmp(k, "SwapFree:")) sf = v;
	}
	fclose(f);
	p->ram_total_gb = tot / 1048576.0;
	p->ram_used_gb = (tot - avail) / 1048576.0;
	p->swap_total_mb = st / 1024.0;
	p->swap_used_mb = (st - sf) / 1024.0;
	push(p->ram_hist, tot ? 100.0 * (tot - avail) / tot : 0);
}

static void read_disk(struct perf *p)
{
	struct statvfs s;
	if (statvfs("/", &s) == 0) {
		double bs = s.f_frsize;
		p->disk_total_gb = s.f_blocks * bs / 1e9;
		p->disk_used_gb = (s.f_blocks - s.f_bfree) * bs / 1e9;
	}
}

static void read_net(struct perf *p)
{
	FILE *f = fopen("/proc/net/dev", "r");
	if (!f)
		return;
	char line[512];
	unsigned long long rx = 0, tx = 0;
	while (fgets(line, sizeof line, f)) {
		char *c = strchr(line, ':');
		if (!c)
			continue;
		*c = '\0';
		char *name = line;
		while (*name == ' ')
			name++;
		if (!strcmp(name, "lo"))
			continue;
		unsigned long long v[16] = { 0 };
		if (sscanf(c + 1, "%llu %llu %llu %llu %llu %llu %llu %llu %llu", &v[0], &v[1], &v[2], &v[3],
			   &v[4], &v[5], &v[6], &v[7], &v[8]) >= 9) {
			rx += v[0];
			tx += v[8];
		}
	}
	fclose(f);
	double t = now_s();
	if (p->prev_net_t > 0 && t > p->prev_net_t) {
		p->rx_rate = (rx - p->prev_rx) / (t - p->prev_net_t);
		p->tx_rate = (tx - p->prev_tx) / (t - p->prev_net_t);
	}
	p->prev_rx = rx;
	p->prev_tx = tx;
	p->prev_net_t = t;
	push(p->rx_hist, p->rx_rate);
	push(p->tx_hist, p->tx_rate);
}

static void read_temp(struct perf *p)
{
	p->temp = -1;
	for (int i = 0; i < 16; i++) {
		char path[128];
		snprintf(path, sizeof path, "/sys/class/thermal/thermal_zone%d/temp", i);
		FILE *f = fopen(path, "r");
		if (!f)
			continue;
		long v;
		if (fscanf(f, "%ld", &v) == 1 && v / 1000.0 > p->temp)
			p->temp = v / 1000.0;
		fclose(f);
	}
}

static void read_misc(struct perf *p)
{
	FILE *f = fopen("/proc/loadavg", "r");
	if (f) {
		if (fscanf(f, "%lf", &p->load1) != 1)
			p->load1 = 0;
		fclose(f);
	}
	f = fopen("/proc/uptime", "r");
	if (f) {
		double u;
		if (fscanf(f, "%lf", &u) == 1)
			p->uptime = (long)u;
		fclose(f);
	}
}

static int cmp_top(const void *a, const void *b)
{
	const struct proc_top *x = a, *y = b;
	if (x->cpu != y->cpu)
		return x->cpu < y->cpu ? 1 : -1;
	return x->mem_mb < y->mem_mb ? 1 : -1;
}

static void read_procs(struct perf *p)
{
	DIR *d = opendir("/proc");
	if (!d)
		return;
	static struct proc_top all[2048];
	int n = 0;
	long page_kb = sysconf(_SC_PAGESIZE) / 1024;
	static struct { pid_t pid; unsigned long long t; } cur[PT_SIZE];
	int ncur = 0;
	struct dirent *e;
	while ((e = readdir(d)) && n < 2048) {
		if (!isdigit((unsigned char)e->d_name[0]))
			continue;
		pid_t pid = atoi(e->d_name);
		char path[64], buf[1024];
		snprintf(path, sizeof path, "/proc/%d/stat", pid);
		FILE *f = fopen(path, "r");
		if (!f)
			continue;
		size_t len = fread(buf, 1, sizeof buf - 1, f);
		fclose(f);
		buf[len] = '\0';
		char *l = strchr(buf, '('), *r = strrchr(buf, ')');
		if (!l || !r)
			continue;
		struct proc_top *t = &all[n];
		size_t nl = r - l - 1 < (long)sizeof t->name - 1 ? (size_t)(r - l - 1) : sizeof t->name - 1;
		memcpy(t->name, l + 1, nl);
		t->name[nl] = '\0';
		unsigned long long ut = 0, st = 0;
		long rss = 0;
		/* champs après ')' : état(3) ... utime(14) stime(15) ... rss(24) */
		char *s = r + 2;
		int field = 3;
		for (char *tok = strtok(s, " "); tok; tok = strtok(NULL, " "), field++) {
			if (field == 14) ut = strtoull(tok, NULL, 10);
			else if (field == 15) st = strtoull(tok, NULL, 10);
			else if (field == 24) { rss = atol(tok); break; }
		}
		unsigned long long tt = ut + st, prev = 0;
		for (int i = 0; i < PT_SIZE; i++)
			if (prev_pt[i].pid == pid) {
				prev = prev_pt[i].t;
				break;
			}
		if (ncur < PT_SIZE) {
			cur[ncur].pid = pid;
			cur[ncur].t = tt;
			ncur++;
		}
		/* prev_total_all = jiffies écoulés pour tous les CPU */
		/* % de la machine entière (comme la jauge CPU globale) */
		t->cpu = (prev && prev_total_all && tt >= prev) ? 100.0 * (tt - prev) / prev_total_all : 0;
		t->mem_mb = rss * page_kb / 1024.0;
		n++;
	}
	closedir(d);
	memset(prev_pt, 0, sizeof prev_pt);
	for (int i = 0; i < ncur; i++)
		prev_pt[i].pid = cur[i].pid, prev_pt[i].t = cur[i].t;
	p->nprocs = n;
	qsort(all, n, sizeof all[0], cmp_top);
	p->ntop = n < MAX_PROCS ? n : MAX_PROCS;
	memcpy(p->top, all, sizeof(struct proc_top) * p->ntop);
}

void perf_init(struct perf *p)
{
	memset(p, 0, sizeof *p);
	perf_update(p);
}

void perf_update(struct perf *p)
{
	read_cpu(p);
	read_mem(p);
	read_disk(p);
	read_net(p);
	read_temp(p);
	read_misc(p);
	read_procs(p);
}

/* ---------------------- machine, réseau, stockage -------------------- */

static void read_first_line(const char *path, char *out, size_t cap)
{
	out[0] = '\0';
	FILE *f = fopen(path, "r");
	if (!f)
		return;
	if (fgets(out, cap, f))
		out[strcspn(out, "\n")] = '\0';
	fclose(f);
}

static void read_static(struct sysinfo_plume *s)
{
	FILE *f = fopen("/etc/os-release", "r");
	if (f) {
		char line[256];
		while (fgets(line, sizeof line, f))
			if (!strncmp(line, "PRETTY_NAME=", 12)) {
				char *v = line + 12;
				v[strcspn(v, "\n")] = '\0';
				if (*v == '"') {
					v++;
					char *q = strchr(v, '"');
					if (q) *q = '\0';
				}
				snprintf(s->os, sizeof s->os, "%s", v);
			}
		fclose(f);
	}
	struct utsname u;
	if (uname(&u) == 0)
		snprintf(s->kernel, sizeof s->kernel, "%s", u.release);
	f = fopen("/proc/cpuinfo", "r");
	if (f) {
		char line[256];
		while (fgets(line, sizeof line, f))
			if (!strncmp(line, "model name", 10)) {
				char *c = strchr(line, ':');
				if (c) {
					c++;
					while (*c == ' ') c++;
					c[strcspn(c, "\n")] = '\0';
					/* « Intel(R) Core(TM) i7-… CPU @ 2.30GHz » -> plus court */
					char buf[128] = "";
					for (char *p = c; *p; p++) {
						if (!strncmp(p, "(R)", 3) || !strncmp(p, "(TM)", 4)) {
							p += p[1] == 'R' ? 2 : 3;
							continue;
						}
						size_t n = strlen(buf);
						if (n + 1 < sizeof buf) { buf[n] = *p; buf[n + 1] = '\0'; }
					}
					char *at = strstr(buf, " CPU @");
					if (at) *at = '\0';
					snprintf(s->cpu_model, sizeof s->cpu_model, "%s", buf);
				}
				break;
			}
		fclose(f);
	}
}

static void read_battery(struct sysinfo_plume *s)
{
	s->battery = -1;
	s->charging = false;
	DIR *d = opendir("/sys/class/power_supply");
	if (!d)
		return;
	struct dirent *e;
	while ((e = readdir(d))) {
		if (strncmp(e->d_name, "BAT", 3))
			continue;
		char path[300], buf[32];
		snprintf(path, sizeof path, "/sys/class/power_supply/%s/capacity", e->d_name);
		read_first_line(path, buf, sizeof buf);
		if (buf[0]) {
			s->battery = atoi(buf);
			snprintf(path, sizeof path, "/sys/class/power_supply/%s/status", e->d_name);
			read_first_line(path, buf, sizeof buf);
			s->charging = !strcmp(buf, "Charging") || !strcmp(buf, "Full");
			break;
		}
	}
	closedir(d);
}

static void read_route(struct sysinfo_plume *s)
{
	s->iface[0] = s->gateway[0] = s->ipv4[0] = '\0';
	FILE *f = fopen("/proc/net/route", "r");
	if (f) {
		char line[256];
		while (fgets(line, sizeof line, f)) {
			char ifn[32];
			unsigned long dst, gw;
			if (sscanf(line, "%31s %lx %lx", ifn, &dst, &gw) == 3 && dst == 0) {
				snprintf(s->iface, sizeof s->iface, "%s", ifn);
				struct in_addr a = { .s_addr = (in_addr_t)gw };
				inet_ntop(AF_INET, &a, s->gateway, sizeof s->gateway);
				break;
			}
		}
		fclose(f);
	}
	struct ifaddrs *ifa = NULL;
	if (getifaddrs(&ifa) == 0) {
		for (struct ifaddrs *i = ifa; i; i = i->ifa_next) {
			if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || (i->ifa_flags & IFF_LOOPBACK))
				continue;
			/* l'interface de la route par défaut, sinon la première trouvée */
			if (s->iface[0] && strcmp(i->ifa_name, s->iface))
				continue;
			struct sockaddr_in *sin = (struct sockaddr_in *)i->ifa_addr;
			inet_ntop(AF_INET, &sin->sin_addr, s->ipv4, sizeof s->ipv4);
			if (!s->iface[0])
				snprintf(s->iface, sizeof s->iface, "%s", i->ifa_name);
			break;
		}
		freeifaddrs(ifa);
	}
	char path[128], st[32];
	snprintf(path, sizeof path, "/sys/class/net/%s/operstate", s->iface);
	read_first_line(path, st, sizeof st);
	s->online = s->gateway[0] && s->ipv4[0] && strcmp(st, "down");
	snprintf(path, sizeof path, "/sys/class/net/%s/wireless", s->iface);
	struct stat sb;
	s->wireless = s->iface[0] && stat(path, &sb) == 0;
	s->wifi_quality = -1;
	if (s->wireless && (f = fopen("/proc/net/wireless", "r"))) {
		char line[256];
		while (fgets(line, sizeof line, f)) {
			char *c = strchr(line, ':');
			if (!c)
				continue;
			*c = '\0';
			char *n = line;
			while (*n == ' ') n++;
			unsigned st2;
			double q;
			if (!strcmp(n, s->iface) && sscanf(c + 1, "%x %lf", &st2, &q) == 2)
				s->wifi_quality = (int)(q * 100 / 70);
		}
		fclose(f);
	}
	/* octets échangés depuis le démarrage sur cette interface */
	s->rx_total = s->tx_total = 0;
	if ((f = fopen("/proc/net/dev", "r"))) {
		char line[512];
		while (fgets(line, sizeof line, f)) {
			char *c = strchr(line, ':');
			if (!c)
				continue;
			*c = '\0';
			char *n = line;
			while (*n == ' ') n++;
			unsigned long long v[9] = { 0 };
			if (!strcmp(n, s->iface) &&
			    sscanf(c + 1, "%llu %llu %llu %llu %llu %llu %llu %llu %llu", &v[0], &v[1], &v[2],
				   &v[3], &v[4], &v[5], &v[6], &v[7], &v[8]) == 9) {
				s->rx_total = v[0];
				s->tx_total = v[8];
			}
		}
		fclose(f);
	}
}

static int count_established(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return 0;
	char line[512];
	int n = 0;
	while (fgets(line, sizeof line, f)) {
		unsigned st;
		/* sl local remote st … ; 01 = ESTABLISHED */
		if (sscanf(line, " %*s %*s %*s %x", &st) == 1 && st == 1)
			n++;
	}
	fclose(f);
	return n;
}

static void read_mounts(struct sysinfo_plume *s)
{
	s->nmounts = 0;
	FILE *f = fopen("/proc/mounts", "r");
	if (!f)
		return;
	char dev[256], mnt[256], type[64];
	dev_t seen[MAX_MOUNTS];
	while (s->nmounts < MAX_MOUNTS && fscanf(f, "%255s %255s %63s %*[^\n]\n", dev, mnt, type) == 3) {
		bool wanted = !strcmp(mnt, "/") || !strcmp(mnt, "/home") || !strncmp(mnt, "/media/", 7) ||
			      !strncmp(mnt, "/mnt/", 5) || !strncmp(mnt, "/run/media/", 11);
		if (!wanted || (strncmp(dev, "/dev/", 5) && strcmp(mnt, "/")))
			continue;
		struct stat sb;
		if (stat(mnt, &sb) != 0)
			continue;
		bool dup = false;
		for (int i = 0; i < s->nmounts; i++)
			dup |= seen[i] == sb.st_dev;
		struct statvfs v;
		if (dup || statvfs(mnt, &v) != 0 || v.f_blocks == 0)
			continue;
		struct mount_use *m = &s->mounts[s->nmounts];
		seen[s->nmounts++] = sb.st_dev;
		snprintf(m->path, sizeof m->path, "%s", mnt);
		m->total_gb = v.f_blocks * (double)v.f_frsize / 1e9;
		m->used_gb = (v.f_blocks - v.f_bfree) * (double)v.f_frsize / 1e9;
	}
	fclose(f);
}

void sys_update(struct sysinfo_plume *s)
{
	if (!s->os[0])
		read_static(s);
	read_battery(s);
	read_route(s);
	s->tcp_established = count_established("/proc/net/tcp") + count_established("/proc/net/tcp6");
	read_mounts(s);
}
