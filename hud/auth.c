/*
 * plume-hud — authentification, faite dans un processus enfant pour ne jamais
 * figer l'interface (PAM peut attendre plusieurs secondes après un échec).
 *
 *   verrou / test : PAM, service « plume-hud » (/etc/pam.d/plume-hud)
 *   greeter       : protocole IPC de greetd (JSON sur la socket $GREETD_SOCK)
 *
 * Résultat renvoyé au parent par un tube : 1 octet (0 = OK, 1 = mauvais mot de
 * passe, 2 = autre erreur) suivi d'un message facultatif.
 */
#include "hud.h"

#include <errno.h>
#include <fcntl.h>
#include <security/pam_appl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

/* greetd exécute : /bin/sh -c "[ -f /etc/profile ] && . /etc/profile; [ -f ~/.profile ] && . ~/.profile;
 *                   exec <cmd joint par des espaces>"
 * → on envoie la ligne de commande en un seul élément, sans « exec » ni « sh -c ». */
#define DEFAULT_SESSION "dbus-run-session sway"

/* ------------------------------- PAM -------------------------------- */

static int pam_conv_fn(int n, const struct pam_message **msg, struct pam_response **resp, void *data)
{
	struct pam_response *r = calloc(n, sizeof *r);
	if (!r)
		return PAM_BUF_ERR;
	for (int i = 0; i < n; i++) {
		if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF || msg[i]->msg_style == PAM_PROMPT_ECHO_ON)
			r[i].resp = strdup((const char *)data);
	}
	*resp = r;
	return PAM_SUCCESS;
}

static int pam_check(const char *user, const char *pw, char *err, size_t cap)
{
	pam_handle_t *ph = NULL;
	struct pam_conv conv = { pam_conv_fn, (void *)pw };
	int rc = pam_start("plume-hud", user, &conv, &ph);
	if (rc != PAM_SUCCESS) {
		snprintf(err, cap, "PAM indisponible");
		return 2;
	}
	rc = pam_authenticate(ph, 0);
	pam_end(ph, rc);
	return rc == PAM_SUCCESS ? 0 : 1;
}

/* ------------------------------ greetd ------------------------------ */

static bool write_all(int fd, const void *buf, size_t n)
{
	const char *p = buf;
	while (n) {
		ssize_t w = write(fd, p, n);
		if (w < 0 && errno == EINTR)
			continue;
		if (w <= 0)
			return false;
		p += w;
		n -= w;
	}
	return true;
}

static bool read_all(int fd, void *buf, size_t n)
{
	char *p = buf;
	while (n) {
		ssize_t r = read(fd, p, n);
		if (r < 0 && errno == EINTR)
			continue;
		if (r <= 0)
			return false;
		p += r;
		n -= r;
	}
	return true;
}

static bool g_send(int fd, const char *json)
{
	uint32_t len = strlen(json);
	return write_all(fd, &len, sizeof len) && write_all(fd, json, len);
}

static char *g_recv(int fd)
{
	uint32_t len;
	if (!read_all(fd, &len, sizeof len) || len > 65536)
		return NULL;
	char *buf = malloc(len + 1);
	if (!buf)
		return NULL;
	if (!read_all(fd, buf, len)) {
		free(buf);
		return NULL;
	}
	buf[len] = '\0';
	return buf;
}

/* Ajoute s à out sous forme de chaîne JSON échappée. */
static void json_append_str(char *out, size_t cap, const char *s)
{
	size_t n = strlen(out);
	if (n + 2 >= cap)
		return;
	out[n++] = '"';
	for (; *s && n + 8 < cap; s++) {
		unsigned char c = *s;
		if (c == '"' || c == '\\') {
			out[n++] = '\\';
			out[n++] = c;
		} else if (c < 0x20) {
			n += snprintf(out + n, cap - n, "\\u%04x", c);
		} else {
			out[n++] = c;
		}
	}
	out[n++] = '"';
	out[n] = '\0';
}

/* Extrait la valeur texte de "key" dans un objet JSON simple (sans imbrication). */
static bool json_get(const char *json, const char *key, char *out, size_t cap)
{
	char pat[64];
	snprintf(pat, sizeof pat, "\"%s\"", key);
	const char *p = strstr(json, pat);
	if (!p)
		return false;
	p += strlen(pat);
	while (*p == ' ' || *p == ':')
		p++;
	if (*p != '"')
		return false;
	p++;
	size_t n = 0;
	while (*p && *p != '"' && n + 1 < cap) {
		if (*p == '\\' && p[1]) {
			p++;
			out[n++] = *p == 'n' ? ' ' : *p;
		} else {
			out[n++] = *p;
		}
		p++;
	}
	out[n] = '\0';
	return true;
}

static int greetd_login(const char *user, const char *pw, char *err, size_t cap)
{
	const char *sock = getenv("GREETD_SOCK");
	if (!sock) {
		snprintf(err, cap, "GREETD_SOCK absent (greetd ne tourne pas ?)");
		return 2;
	}
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	snprintf(addr.sun_path, sizeof addr.sun_path, "%s", sock);
	if (fd < 0 || connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
		snprintf(err, cap, "Connexion à greetd impossible");
		return 2;
	}

	const char *session = getenv("PLUME_HUD_SESSION");
	if (!session || !*session)
		session = DEFAULT_SESSION;

	char req[2048], type[64], tmp[512];
	snprintf(req, sizeof req, "{\"type\":\"create_session\",\"username\":");
	json_append_str(req, sizeof req, user);
	strcat(req, "}");
	if (!g_send(fd, req)) {
		snprintf(err, cap, "greetd ne répond pas");
		close(fd);
		return 2;
	}

	bool started = false;
	int result = 2;
	for (;;) {
		char *resp = g_recv(fd);
		if (!resp) {
			snprintf(err, cap, "greetd a fermé la connexion");
			break;
		}
		json_get(resp, "type", type, sizeof type);
		if (!strcmp(type, "auth_message")) {
			json_get(resp, "auth_message_type", tmp, sizeof tmp);
			if (!strcmp(tmp, "secret") || !strcmp(tmp, "visible")) {
				snprintf(req, sizeof req, "{\"type\":\"post_auth_message_response\",\"response\":");
				json_append_str(req, sizeof req, pw);
				strcat(req, "}");
			} else {
				snprintf(req, sizeof req, "{\"type\":\"post_auth_message_response\"}");
			}
			explicit_bzero(resp, strlen(resp));
			free(resp);
			if (!g_send(fd, req))
				break;
			explicit_bzero(req, sizeof req);
			continue;
		}
		if (!strcmp(type, "success")) {
			free(resp);
			if (started) {
				result = 0;
				break;
			}
			snprintf(req, sizeof req, "{\"type\":\"start_session\",\"cmd\":[");
			json_append_str(req, sizeof req, session);
			strcat(req, "],\"env\":[\"PLUME_SESSION=1\",\"XDG_SESSION_TYPE=wayland\"]}");
			started = true;
			if (!g_send(fd, req))
				break;
			continue;
		}
		/* erreur */
		char etype[64] = "";
		json_get(resp, "error_type", etype, sizeof etype);
		if (!json_get(resp, "description", err, cap))
			snprintf(err, cap, "erreur greetd");
		free(resp);
		result = !strcmp(etype, "auth_error") ? 1 : 2;
		g_send(fd, "{\"type\":\"cancel_session\"}");
		char *r2 = g_recv(fd);
		free(r2);
		break;
	}
	close(fd);
	return result;
}

/* ------------------------- processus enfant ------------------------- */

int auth_start(struct hud *h)
{
	int p[2];
	if (pipe2(p, O_CLOEXEC) < 0)
		return -1;
	pid_t pid = fork();
	if (pid < 0) {
		close(p[0]);
		close(p[1]);
		return -1;
	}
	if (pid == 0) {
		close(p[0]);
		char msg[200] = "";
		unsigned char r;
		if (h->mode == MODE_GREETER)
			r = greetd_login(h->user, h->pw, msg, sizeof msg);
		else
			r = pam_check(h->user, h->pw, msg, sizeof msg);
		char out[201];
		out[0] = r;
		size_t m = strlen(msg);
		memcpy(out + 1, msg, m);
		write_all(p[1], out, m + 1);
		_exit(0);
	}
	close(p[1]);
	h->auth_pid = pid;
	h->auth_fd = p[0];
	h->authenticating = true;
	return 0;
}

void auth_finish(struct hud *h)
{
	char buf[202] = { 0 };
	ssize_t n = read(h->auth_fd, buf, sizeof buf - 1);
	close(h->auth_fd);
	h->auth_fd = -1;
	waitpid(h->auth_pid, NULL, 0);
	if (n < 1)
		hud_auth_done(h, 2, "erreur interne");
	else
		hud_auth_done(h, (unsigned char)buf[0], buf + 1);
}
