/* plume-wifi — dialogue avec iwd par iwctl : une commande à la fois, lue sans
 * bloquer la fenêtre. iwctl est lancé sans shell (execvp) : un nom de réseau
 * ou un mot de passe ne peut pas devenir une commande. */
#include "wifi.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

enum job { J_DEVICE, J_SHOW, J_NETS, J_KNOWN, J_SCAN, J_CONNECT, J_DISCONNECT, J_FORGET, J_POWER };

#define MAX_ARGS 10
#define QLEN 16
#define OUT_MAX 65536

struct cmd {
	enum job job;
	int argc;
	char *argv[MAX_ARGS + 1];
	char ssid[128];
	bool with_pass;
};

static struct cmd queue[QLEN];
static int qn;
static struct cmd cur;
static pid_t pid = -1;
static int pfd = -1;
static char out[OUT_MAX];
static size_t outn;
static double nets_at = -1;   /* relire la liste à ce moment (après une recherche) */
static double refresh_at = -1; /* actualisation périodique */
static char known[8192];      /* sortie de « known-networks list » */
static bool want_scan;        /* recherche demandée : lancée après « device list » */

static void cmd_free(struct cmd *c)
{
	for (int i = 0; i < c->argc; i++) {
		if (c->with_pass)
			memset(c->argv[i], 0, strlen(c->argv[i])); /* le mot de passe ne traîne pas en mémoire */
		free(c->argv[i]);
	}
	c->argc = 0;
}

static void push(enum job j, const char *ssid, bool with_pass, ...)
{
	if (qn >= QLEN)
		return;
	/* pas deux lectures identiques en attente */
	if (j == J_SHOW || j == J_NETS || j == J_KNOWN || j == J_DEVICE)
		for (int i = 0; i < qn; i++)
			if (queue[i].job == j)
				return;
	struct cmd *c = &queue[qn++];
	memset(c, 0, sizeof *c);
	c->job = j;
	c->with_pass = with_pass;
	snprintf(c->ssid, sizeof c->ssid, "%s", ssid ? ssid : "");
	va_list ap;
	va_start(ap, with_pass);
	c->argv[c->argc++] = strdup("iwctl");
	const char *a;
	while (c->argc < MAX_ARGS && (a = va_arg(ap, const char *)))
		c->argv[c->argc++] = strdup(a);
	va_end(ap);
	c->argv[c->argc] = NULL;
}

static void start_next(void)
{
	if (pid > 0 || qn == 0)
		return;
	cur = queue[0];
	memmove(queue, queue + 1, (size_t)(--qn) * sizeof *queue);
	int p[2];
	if (pipe2(p, O_CLOEXEC) < 0) {
		cmd_free(&cur);
		return;
	}
	pid = fork();
	if (pid == 0) {
		int nul = open("/dev/null", O_RDONLY);
		dup2(nul, 0);
		dup2(p[1], 1);
		dup2(p[1], 2);
		setenv("NO_COLOR", "1", 1);
		execvp("iwctl", cur.argv);
		_exit(127);
	}
	close(p[1]);
	if (pid < 0) {
		close(p[0]);
		cmd_free(&cur);
		return;
	}
	pfd = p[0];
	fcntl(pfd, F_SETFL, O_NONBLOCK);
	outn = 0;
}

int iwd_fd(void) { return pfd; }

/* enlève les couleurs ANSI (ESC [ … lettre) et les retours chariot */
static void strip(char *s)
{
	char *w = s;
	for (char *r = s; *r; r++) {
		if (*r == 0x1b) {
			if (r[1] == '[') {
				r += 2;
				while (*r && !isalpha((unsigned char)*r))
					r++;
				if (!*r)
					break;
			}
			continue;
		}
		if (*r != '\r')
			*w++ = *r;
	}
	*w = 0;
}

static void rtrim(char *s)
{
	size_t n = strlen(s);
	while (n && isspace((unsigned char)s[n - 1]))
		s[--n] = 0;
}

static char *ltrim(char *s)
{
	while (isspace((unsigned char)*s))
		s++;
	return s;
}

/* valeur de « clé   valeur » dans « station X show » */
static void prop(const char *text, const char *key, char *dst, size_t n)
{
	const char *p = text;
	size_t kl = strlen(key);
	dst[0] = 0;
	while ((p = strstr(p, key))) {
		/* la clé doit être suivie d'au moins deux espaces (colonne) */
		if ((p == text || isspace((unsigned char)p[-1])) && p[kl] == ' ' && p[kl + 1] == ' ') {
			const char *v = p + kl;
			while (*v == ' ')
				v++;
			size_t l = strcspn(v, "\n");
			if (l >= n)
				l = n - 1;
			memcpy(dst, v, l);
			dst[l] = 0;
			rtrim(dst);
			return;
		}
		p += kl;
	}
}

static bool is_known(const char *name)
{
	char line[512];
	const char *p = known;
	size_t nl = strlen(name);
	while (*p) {
		size_t l = strcspn(p, "\n");
		snprintf(line, sizeof line, "%.*s", (int)(l < sizeof line - 1 ? l : sizeof line - 1), p);
		char *t = ltrim(line);
		if (!strncmp(t, name, nl) && t[nl] == ' ' && t[nl + 1] == ' ')
			return true;
		p += l + (p[l] == '\n');
	}
	return false;
}

static void parse_device(char *s)
{
	A.have_dev = false;
	for (char *l = strtok(s, "\n"); l; l = strtok(NULL, "\n")) {
		char name[32], addr[32], pw[8], adapter[32], mode[16];
		if (sscanf(l, " %31s %31s %7s %31s %15s", name, addr, pw, adapter, mode) == 5 &&
		    !strcmp(mode, "station") && strchr(addr, ':')) {
			snprintf(A.dev, sizeof A.dev, "%s", name);
			A.powered = !strcmp(pw, "on");
			A.have_dev = true;
			return;
		}
	}
}

static int cmp_net(const void *a, const void *b)
{
	const struct net *x = a, *y = b;
	if (x->connected != y->connected)
		return y->connected - x->connected;
	return y->dbm - x->dbm;
}

static void parse_nets(char *s)
{
	char sel[128] = "";
	if (A.sel >= 0 && A.sel < A.nnets)
		snprintf(sel, sizeof sel, "%s", A.nets[A.sel].name);
	A.nnets = 0;
	int seps = 0;
	for (char *l = strtok(s, "\n"); l; l = strtok(NULL, "\n")) {
		if (strstr(l, "-----")) {
			seps++;
			continue;
		}
		if (seps < 2 || A.nnets >= MAX_NETS)
			continue;
		rtrim(l);
		/* dernier mot : signal (dBm), avant-dernier : sécurité, le reste : nom */
		char *sig = strrchr(l, ' ');
		if (!sig)
			continue;
		*sig++ = 0;
		rtrim(l);
		char *sec = strrchr(l, ' ');
		if (!sec)
			continue;
		*sec++ = 0;
		rtrim(l);
		char *name = ltrim(l);
		bool conn = false;
		if (name[0] == '>' && name[1] == ' ') {
			conn = true;
			name = ltrim(name + 1);
		}
		if (!*name)
			continue;
		struct net *n = &A.nets[A.nnets++];
		snprintf(n->name, sizeof n->name, "%s", name);
		snprintf(n->sec, sizeof n->sec, "%s", sec);
		n->dbm = atoi(sig);
		if (n->dbm < -1000)
			n->dbm /= 100; /* anciens iwd : centièmes de dBm */
		n->connected = conn || (A.cur[0] && !strcmp(A.cur, name));
		n->known = is_known(name);
	}
	qsort(A.nets, A.nnets, sizeof *A.nets, cmp_net);
	A.sel = -1;
	for (int i = 0; sel[0] && i < A.nnets; i++)
		if (!strcmp(A.nets[i].name, sel))
			A.sel = i;
	if (A.sel < 0)
		A.ask_pass = false;
	A.loaded = true;
}

/* première ligne utile de la sortie, pour un message d'erreur */
static const char *first_line(char *s)
{
	for (char *l = strtok(s, "\n"); l; l = strtok(NULL, "\n")) {
		l = ltrim(l);
		rtrim(l);
		if (*l)
			return l;
	}
	return "erreur inconnue";
}

static void finish(int rc)
{
	out[outn] = 0;
	strip(out);
	switch (cur.job) {
	case J_DEVICE:
		if (rc == 127) {
			A.no_iwctl = true;
			A.loaded = true;
			break;
		}
		parse_device(out);
		if (!(A.have_dev && A.powered))
			want_scan = false;
		if (A.have_dev && A.powered) {
			push(J_SHOW, NULL, false, "station", A.dev, "show", NULL);
			push(J_KNOWN, NULL, false, "known-networks", "list", NULL);
			push(J_NETS, NULL, false, "station", A.dev, "get-networks", "rssi-dbms", NULL);
			if (want_scan)
				push(J_SCAN, NULL, false, "station", A.dev, "scan", NULL);
		} else {
			A.nnets = 0;
			A.cur[0] = A.ip[0] = 0;
			snprintf(A.state, sizeof A.state, "%s", A.have_dev ? "off" : "");
			A.scanning = false;
			A.loaded = true;
		}
		break;
	case J_SHOW:
		prop(out, "State", A.state, sizeof A.state);
		prop(out, "Connected network", A.cur, sizeof A.cur);
		prop(out, "IPv4 address", A.ip, sizeof A.ip);
		if (strcmp(A.state, "connected"))
			A.cur[0] = 0;
		for (int i = 0; i < A.nnets; i++)
			A.nets[i].connected = A.cur[0] && !strcmp(A.nets[i].name, A.cur);
		break;
	case J_KNOWN:
		snprintf(known, sizeof known, "%s", out);
		for (int i = 0; i < A.nnets; i++)
			A.nets[i].known = is_known(A.nets[i].name);
		break;
	case J_NETS:
		parse_nets(out);
		break;
	case J_SCAN:
		want_scan = false;
		nets_at = now_s() + 3.5;
		break;
	case J_CONNECT:
		A.busy = false;
		if (rc == 0) {
			set_toast(false, "Connecté à %s", cur.ssid);
			A.ask_pass = false;
			memset(A.pass, 0, sizeof A.pass);
			A.sel = -1;
		} else {
			const char *m = first_line(out);
			bool needs = strstr(m, "assphrase") || strstr(m, "not provided") || strstr(m, "Not configured");
			int idx = -1;
			for (int i = 0; i < A.nnets; i++)
				if (!strcmp(A.nets[i].name, cur.ssid))
					idx = i;
			if (!cur.with_pass && needs && idx >= 0) {
				A.sel = idx;
				A.ask_pass = true;
				set_toast(true, "Mot de passe nécessaire pour %s", cur.ssid);
			} else if (cur.with_pass && (strstr(m, "assphrase") || strstr(m, "ailed"))) {
				set_toast(true, "Connexion refusée : mot de passe incorrect ?");
			} else {
				set_toast(true, "Échec : %s", m);
			}
		}
		iwd_refresh(false);
		break;
	case J_DISCONNECT:
		A.busy = false;
		if (rc == 0) {
			set_toast(false, "Déconnecté");
			A.sel = -1;
		}
		else
			set_toast(true, "Échec : %s", first_line(out));
		iwd_refresh(false);
		break;
	case J_FORGET:
		if (rc == 0)
			set_toast(false, "Réseau oublié : %s", cur.ssid);
		else
			set_toast(true, "Échec : %s", first_line(out));
		A.sel = -1;
		iwd_refresh(false);
		break;
	case J_POWER:
		A.busy = false;
		if (rc)
			set_toast(true, "Échec : %s", first_line(out));
		iwd_refresh(true);
		break;
	}
	if (cur.job == J_NETS || (cur.job == J_DEVICE && !(A.have_dev && A.powered)))
		if (nets_at < 0 && !want_scan)
			A.scanning = false;
	if (rc != 0 && rc != 127 && cur.job == J_DEVICE)
		A.have_dev = false;
	cmd_free(&cur);
	A.dirty = true;
}

void iwd_read(void)
{
	for (;;) {
		ssize_t n = read(pfd, out + outn, OUT_MAX - 1 - outn);
		if (n > 0) {
			outn += (size_t)n;
			if (outn >= OUT_MAX - 1)
				outn = 0; /* sortie démesurée : on la jette */
			continue;
		}
		if (n < 0 && (errno == EAGAIN || errno == EINTR))
			return;
		break; /* fin du flux */
	}
	close(pfd);
	pfd = -1;
	int st = 0;
	waitpid(pid, &st, 0);
	pid = -1;
	finish(WIFEXITED(st) ? WEXITSTATUS(st) : 1);
	start_next();
}

void iwd_tick(void)
{
	double t = now_s();
	if (nets_at >= 0 && t >= nets_at) {
		nets_at = -1;
		push(J_NETS, NULL, false, "station", A.dev, "get-networks", "rssi-dbms", NULL);
	}
	if (refresh_at >= 0 && t >= refresh_at && pid < 0 && qn == 0)
		iwd_refresh(false);
	start_next();
}

double iwd_next_timeout(void)
{
	double t = now_s(), w = -1;
	if (nets_at >= 0)
		w = nets_at - t;
	if (refresh_at >= 0 && (w < 0 || refresh_at - t < w))
		w = refresh_at - t;
	return w < 0 && (nets_at >= 0 || refresh_at >= 0) ? 0 : w;
}

void iwd_refresh(bool scan)
{
	refresh_at = now_s() + 8; /* l'état est relu toutes les 8 s */
	push(J_DEVICE, NULL, false, "device", "list", NULL);
	if (scan) {
		want_scan = true;
		A.scanning = true;
	}
	start_next();
}

void iwd_connect(const char *ssid, const char *pass)
{
	A.busy = true;
	if (pass && *pass)
		push(J_CONNECT, ssid, true, "--passphrase", pass, "station", A.dev, "connect", ssid, NULL);
	else
		push(J_CONNECT, ssid, false, "--dont-ask", "station", A.dev, "connect", ssid, NULL);
	start_next();
}

void iwd_disconnect(void)
{
	A.busy = true;
	push(J_DISCONNECT, NULL, false, "station", A.dev, "disconnect", NULL);
	start_next();
}

void iwd_forget(const char *ssid)
{
	push(J_FORGET, ssid, false, "known-networks", ssid, "forget", NULL);
	start_next();
}

void iwd_power(bool on)
{
	A.busy = true;
	push(J_POWER, NULL, false, "device", A.dev, "set-property", "Powered", on ? "on" : "off", NULL);
	start_next();
}
