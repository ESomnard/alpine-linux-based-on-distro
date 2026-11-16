/* plume-son — état de PipeWire (wpctl status / get-volume) et actions. */
#include "panel.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { J_STATUS = 1, J_GETVOL, J_SETVOL, J_MUTE, J_DEFAULT };

static double refresh_at;
static struct node fresh[MAX_NODES];
static int nfresh;

int back_fd(void) { return run_fd(); }
void back_read(void) { run_read(); }

double back_next_timeout(void)
{
	double w = refresh_at - now_s();
	return w < 0 ? 0 : w;
}

void back_refresh(bool full)
{
	static bool init;
	if (!init) {
		init = true;
		A.drag = -1;
	}
	(void)full;
	refresh_at = now_s() + 1.5; /* le volume peut changer ailleurs (touches, barre) */
	run_push(J_STATUS, "", RUN_SKIP, "wpctl", "status", NULL);
	run_start();
}

void back_tick(void)
{
	if (now_s() >= refresh_at && run_idle() && A.drag < 0)
		back_refresh(false);
	run_start();
}

static struct node *find(struct node *t, int n, int id)
{
	for (int i = 0; i < n; i++)
		if (t[i].id == id)
			return &t[i];
	return NULL;
}

/* « │  *   43. Haut-parleurs intégrés     [vol: 1.00 MUTED] » */
static bool parse_entry(const char *l, struct node *n)
{
	const char *p = l;
	bool star = false;
	while (*p && !isdigit((unsigned char)*p)) {
		if (*p == '*')
			star = true;
		p++;
	}
	if (!*p)
		return false;
	char *e;
	long id = strtol(p, &e, 10);
	if (e[0] != '.' || e[1] != ' ')
		return false;
	e += 2;
	while (*e == ' ')
		e++;
	const char *end = strstr(e, "  ");
	const char *vb = strstr(e, "[vol:");
	if (!end || (vb && vb < end))
		end = vb ? vb : e + strlen(e);
	size_t len = (size_t)(end - e);
	while (len && isspace((unsigned char)e[len - 1]))
		len--;
	if (!len)
		return false;
	memset(n, 0, sizeof *n);
	n->id = (int)id;
	n->def = star;
	snprintf(n->name, sizeof n->name, "%.*s", (int)len, e);
	n->vol = vb ? atof(vb + 5) : -1;
	n->muted = vb && strstr(vb, "MUTED");
	return true;
}

static void parse_status(char *s)
{
	nfresh = 0;
	int sect = -1; /* -1 : ailleurs ; sinon N_SINK… ; -2 : section ignorée */
	bool audio = false;
	for (char *l = strtok(s, "\n"); l; l = strtok(NULL, "\n")) {
		if (!strncmp(l, "Audio", 5)) { audio = true; continue; }
		if (isalpha((unsigned char)l[0])) { audio = false; continue; } /* Video, Settings… */
		if (!audio)
			continue;
		if (strstr(l, "Sinks:")) { sect = N_SINK; continue; }
		if (strstr(l, "Sources:")) { sect = N_SOURCE; continue; }
		if (strstr(l, "Streams:")) { sect = N_STREAM; continue; }
		if (strstr(l, "Devices:") || strstr(l, "Filters:")) { sect = -2; continue; }
		if (sect < 0 || nfresh >= MAX_NODES)
			continue;
		/* sous un flux : ses ports (« 61. output_FL > … ») */
		if (sect == N_STREAM && (strstr(l, " > ") || strstr(l, " < ")))
			continue;
		struct node n;
		if (parse_entry(l, &n)) {
			n.kind = sect;
			fresh[nfresh++] = n;
		}
	}
	/* volume des applications : wpctl get-volume pour chacune */
	for (int i = 0; i < nfresh; i++)
		if (fresh[i].vol < 0) {
			char id[16], tagv[16];
			snprintf(id, sizeof id, "%d", fresh[i].id);
			snprintf(tagv, sizeof tagv, "%d", fresh[i].id);
			run_push(J_GETVOL, tagv, RUN_SKIP, "wpctl", "get-volume", id, NULL);
		}
	/* on garde ce qui vient d'être réglé ici (curseur en cours) */
	double t = now_s();
	for (int i = 0; i < nfresh; i++) {
		struct node *old = find(A.nodes, A.nnodes, fresh[i].id);
		if (old && (t < old->hold_until || A.drag >= 0)) {
			fresh[i].vol = old->vol;
			fresh[i].muted = old->muted;
			fresh[i].hold_until = old->hold_until;
		} else if (old && fresh[i].vol < 0) {
			fresh[i].vol = old->vol; /* en attendant get-volume */
			fresh[i].muted = old->muted;
		}
	}
	if (A.drag < 0) {
		memcpy(A.nodes, fresh, sizeof(struct node) * (size_t)nfresh);
		A.nnodes = nfresh;
	}
	A.loaded = true;
}

void run_done(int job, const char *tag, int rc, char *out)
{
	switch (job) {
	case J_STATUS:
		if (rc == 127) {
			A.no_wpctl = true;
			A.loaded = true;
			break;
		}
		A.no_wpctl = false;
		parse_status(out);
		break;
	case J_GETVOL: {
		struct node *n = find(A.nodes, A.nnodes, atoi(tag));
		char *v = strstr(out, "Volume:");
		if (n && v && now_s() >= n->hold_until && A.drag < 0) {
			n->vol = atof(v + 7);
			n->muted = strstr(v, "MUTED") != NULL;
		}
		break;
	}
	case J_SETVOL:
	case J_MUTE:
		break;
	case J_DEFAULT:
		if (rc)
			set_toast(true, "Impossible de changer d'appareil");
		back_refresh(false);
		break;
	}
}

void son_set_volume(struct node *n, double v)
{
	v = clampd(v, 0, 1);
	n->vol = v;
	n->hold_until = now_s() + 1.5;
	char id[16], val[16];
	snprintf(id, sizeof id, "%d", n->id);
	snprintf(val, sizeof val, "%.2f", v);
	/* RUN_REPLACE : en glissant, seule la dernière valeur part */
	run_push(J_SETVOL, id, RUN_REPLACE, "wpctl", "set-volume", id, val, NULL);
	run_start();
}

void son_toggle_mute(struct node *n)
{
	n->muted = !n->muted;
	n->hold_until = now_s() + 1.5;
	char id[16];
	snprintf(id, sizeof id, "%d", n->id);
	run_push(J_MUTE, id, RUN_NONE, "wpctl", "set-mute", id, n->muted ? "1" : "0", NULL);
	run_start();
}

void son_set_default(struct node *n)
{
	char id[16];
	snprintf(id, sizeof id, "%d", n->id);
	for (int i = 0; i < A.nnodes; i++)
		if (A.nodes[i].kind == n->kind)
			A.nodes[i].def = false;
	n->def = true;
	run_push(J_DEFAULT, id, RUN_NONE, "wpctl", "set-default", id, NULL);
	run_start();
	set_toast(false, "%s : %s", n->kind == N_SINK ? "Sortie" : "Entrée", n->name);
}
