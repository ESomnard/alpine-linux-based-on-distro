/* plume-bluetooth — état de bluez (bluetoothctl) et actions. */
#include "panel.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { J_SHOW = 1, J_DEVICES, J_INFO, J_SCAN, J_POWER, J_UNBLOCK, J_PAIR, J_TRUST, J_CONNECT, J_DISCONNECT, J_REMOVE };

static double refresh_at;
static bool want_power; /* après rfkill unblock : rallumer */

int back_fd(void) { return run_fd(); }
void back_read(void) { run_read(); }

double back_next_timeout(void)
{
	double w = refresh_at - now_s();
	return w < 0 ? 0 : w;
}

void back_refresh(bool full)
{
	refresh_at = now_s() + 4;
	run_push(J_SHOW, "", RUN_SKIP, "bluetoothctl", "show", NULL);
	if (full && A.powered && !A.scanning) {
		A.scanning = true;
		/* recherche pendant 10 s : les appareils trouvés arrivent au fur et à mesure */
		run_push(J_SCAN, "", RUN_SKIP, "bluetoothctl", "--timeout", "10", "scan", "on", NULL);
	}
	run_start();
}

void back_tick(void)
{
	if (now_s() >= refresh_at && run_idle())
		back_refresh(false);
	run_start();
}

static struct dev *find(const char *mac)
{
	for (int i = 0; i < A.ndevs; i++)
		if (!strcmp(A.devs[i].mac, mac))
			return &A.devs[i];
	return NULL;
}

/* « \tPowered: yes » -> valeur */
static bool field(const char *text, const char *key, char *dst, size_t n)
{
	const char *p = text;
	size_t kl = strlen(key);
	while ((p = strstr(p, key))) {
		if ((p == text || p[-1] == '\t' || p[-1] == ' ' || p[-1] == '\n') && p[kl] == ':') {
			const char *v = p + kl + 1;
			while (*v == ' ')
				v++;
			size_t l = strcspn(v, "\n");
			if (l >= n)
				l = n - 1;
			memcpy(dst, v, l);
			dst[l] = 0;
			return true;
		}
		p += kl;
	}
	dst[0] = 0;
	return false;
}

static bool yes(const char *text, const char *key)
{
	char v[16];
	return field(text, key, v, sizeof v) && !strcmp(v, "yes");
}

static bool is_mac(const char *s)
{
	for (int i = 0; i < 17; i++) {
		if (i % 3 == 2) {
			if (s[i] != ':')
				return false;
		} else if (!isxdigit((unsigned char)s[i]))
			return false;
	}
	return true;
}

static int cmp_dev(const void *a, const void *b)
{
	const struct dev *x = a, *y = b;
	if (x->connected != y->connected)
		return y->connected - x->connected;
	if (x->paired != y->paired)
		return y->paired - x->paired;
	return strcasecmp(x->name, y->name);
}

static void parse_devices(char *s)
{
	char sel[18] = "";
	if (A.sel >= 0 && A.sel < A.ndevs)
		snprintf(sel, sizeof sel, "%s", A.devs[A.sel].mac);
	struct dev old[MAX_DEVS];
	int nold = A.ndevs;
	memcpy(old, A.devs, sizeof old);
	A.ndevs = 0;
	for (char *l = strtok(s, "\n"); l && A.ndevs < MAX_DEVS; l = strtok(NULL, "\n")) {
		char *d = strstr(l, "Device ");
		if (!d || strlen(d) < 7 + 17 || !is_mac(d + 7))
			continue;
		struct dev *v = &A.devs[A.ndevs];
		memset(v, 0, sizeof *v);
		snprintf(v->mac, sizeof v->mac, "%.17s", d + 7);
		const char *nm = d + 7 + 17;
		while (*nm == ' ')
			nm++;
		snprintf(v->name, sizeof v->name, "%s", nm);
		/* appareil sans nom (son nom est son adresse) : on ne l'affiche pas */
		char tiret[18];
		snprintf(tiret, sizeof tiret, "%s", v->mac);
		for (char *c = tiret; *c; c++)
			if (*c == ':')
				*c = '-';
		if (!v->name[0] || !strcmp(v->name, tiret))
			continue;
		v->battery = -1;
		for (int i = 0; i < nold; i++)
			if (!strcmp(old[i].mac, v->mac)) {
				*v = old[i]; /* garde l'état connu en attendant « info » */
				snprintf(v->name, sizeof v->name, "%s", nm);
			}
		A.ndevs++;
		run_push(J_INFO, v->mac, RUN_SKIP, "bluetoothctl", "info", v->mac, NULL);
	}
	qsort(A.devs, A.ndevs, sizeof *A.devs, cmp_dev);
	A.sel = -1;
	for (int i = 0; sel[0] && i < A.ndevs; i++)
		if (!strcmp(A.devs[i].mac, sel))
			A.sel = i;
	A.loaded = true;
}

static const char *first_line(char *s)
{
	for (char *l = strtok(s, "\n"); l; l = strtok(NULL, "\n")) {
		while (isspace((unsigned char)*l))
			l++;
		if (*l && !strstr(l, "Agent registered") && !strstr(l, "Attempting"))
			return l;
	}
	return "erreur inconnue";
}

void run_done(int job, const char *tag, int rc, char *out)
{
	struct dev *d = tag[0] ? find(tag) : NULL;
	char nm[96] = "";
	if (d)
		snprintf(nm, sizeof nm, "%s", d->name);
	switch (job) {
	case J_SHOW:
		if (rc == 127) {
			A.no_ctl = A.loaded = true;
			break;
		}
		A.no_ctl = false;
		A.no_adapter = strstr(out, "No default controller") || !strstr(out, "Controller ");
		if (A.no_adapter) {
			A.ndevs = 0;
			A.loaded = true;
			A.scanning = false;
			break;
		}
		A.powered = yes(out, "Powered");
		{
			char ps[32];
			field(out, "PowerState", ps, sizeof ps);
			A.blocked = strstr(ps, "blocked") != NULL;
		}
		field(out, "Alias", A.adapter, sizeof A.adapter);
		if (A.powered)
			run_push(J_DEVICES, "", RUN_SKIP, "bluetoothctl", "devices", NULL);
		else {
			A.ndevs = 0;
			A.loaded = true;
			A.scanning = false;
		}
		break;
	case J_DEVICES:
		parse_devices(out);
		break;
	case J_INFO:
		if (d) {
			d->paired = yes(out, "Paired");
			d->connected = yes(out, "Connected");
			d->trusted = yes(out, "Trusted");
			field(out, "Icon", d->icon, sizeof d->icon);
			char b[32];
			d->battery = field(out, "Battery Percentage", b, sizeof b) && strchr(b, '(') ? atoi(strchr(b, '(') + 1) : -1;
			qsort(A.devs, A.ndevs, sizeof *A.devs, cmp_dev);
		}
		break;
	case J_SCAN:
		A.scanning = false;
		back_refresh(false);
		break;
	case J_UNBLOCK:
		if (rc == 0 && want_power)
			run_push(J_POWER, "", RUN_NONE, "bluetoothctl", "power", "on", NULL);
		else {
			A.busy = false;
			set_toast(true, "Bluetooth bloqué (rfkill) : sudo rfkill unblock bluetooth");
		}
		want_power = false;
		break;
	case J_POWER:
		A.busy = false;
		if (rc || strstr(out, "Failed")) {
			if (strstr(out, "Blocked") || strstr(out, "rfkill")) {
				/* bloqué par rfkill : déblocage autorisé sans mot de passe (sudoers) */
				A.busy = true;
				want_power = true;
				run_push(J_UNBLOCK, "", RUN_NONE, "sudo", "-n", "/usr/sbin/rfkill", "unblock", "bluetooth", NULL);
			} else
				set_toast(true, "Échec : %s", first_line(out));
		}
		back_refresh(false);
		break;
	case J_PAIR:
		if (rc || !strstr(out, "successful")) {
			A.busy = false;
			A.busy_mac[0] = 0;
			set_toast(true, "Association refusée : appareil en mode association ? (%s)", first_line(out));
			break;
		}
		/* associé : on lui fait confiance (reconnexion automatique) puis on connecte */
		run_push(J_TRUST, tag, RUN_NONE, "bluetoothctl", "trust", tag, NULL);
		run_push(J_CONNECT, tag, RUN_NONE, "bluetoothctl", "--timeout", "15", "connect", tag, NULL);
		break;
	case J_TRUST:
		break;
	case J_CONNECT:
		A.busy = false;
		A.busy_mac[0] = 0;
		if (rc == 0 && strstr(out, "successful"))
			set_toast(false, "Connecté : %s", nm);
		else
			set_toast(true, "Connexion impossible : %s", first_line(out));
		A.sel = -1;
		back_refresh(false);
		break;
	case J_DISCONNECT:
		A.busy = false;
		A.busy_mac[0] = 0;
		if (rc == 0)
			set_toast(false, "Déconnecté : %s", nm);
		else
			set_toast(true, "Échec : %s", first_line(out));
		A.sel = -1;
		back_refresh(false);
		break;
	case J_REMOVE:
		if (rc == 0)
			set_toast(false, "Appareil oublié : %s", nm);
		else
			set_toast(true, "Échec : %s", first_line(out));
		A.sel = -1;
		back_refresh(false);
		break;
	}
}

static void act(int job, const char *mac, const char *verb, bool timeout)
{
	A.busy = true;
	snprintf(A.busy_mac, sizeof A.busy_mac, "%s", mac);
	if (timeout)
		run_push(job, mac, RUN_NONE, "bluetoothctl", "--timeout", "25", verb, mac, NULL);
	else
		run_push(job, mac, RUN_NONE, "bluetoothctl", verb, mac, NULL);
	run_start();
}

void bt_connect(struct dev *d) { act(J_CONNECT, d->mac, "connect", true); }
void bt_disconnect(struct dev *d) { act(J_DISCONNECT, d->mac, "disconnect", false); }
void bt_pair(struct dev *d) { act(J_PAIR, d->mac, "pair", true); }
void bt_forget(struct dev *d)
{
	run_push(J_REMOVE, d->mac, RUN_NONE, "bluetoothctl", "remove", d->mac, NULL);
	run_start();
}

void bt_power(bool on)
{
	A.busy = true;
	run_push(J_POWER, "", RUN_NONE, "bluetoothctl", "power", on ? "on" : "off", NULL);
	run_start();
}
