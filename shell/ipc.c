/* plume-shell — dialogue avec sway (protocole IPC « i3-ipc »). */
#include "shell.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define IPC_MAGIC "i3-ipc"
#define IPC_HDR 14

int ipc_connect(void)
{
	const char *path = getenv("SWAYSOCK");
	if (!path)
		return -1;
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return -1;
	struct sockaddr_un a = { .sun_family = AF_UNIX };
	snprintf(a.sun_path, sizeof a.sun_path, "%s", path);
	if (connect(fd, (struct sockaddr *)&a, sizeof a) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static bool io_all(int fd, void *buf, size_t n, bool wr)
{
	char *p = buf;
	while (n) {
		ssize_t r = wr ? write(fd, p, n) : read(fd, p, n);
		if (r < 0 && errno == EINTR)
			continue;
		if (r <= 0)
			return false;
		p += r;
		n -= r;
	}
	return true;
}

bool ipc_send(int fd, uint32_t type, const char *payload)
{
	if (fd < 0)
		return false;
	uint32_t len = payload ? strlen(payload) : 0;
	char hdr[IPC_HDR];
	memcpy(hdr, IPC_MAGIC, 6);
	memcpy(hdr + 6, &len, 4);
	memcpy(hdr + 10, &type, 4);
	return io_all(fd, hdr, IPC_HDR, true) && (!len || io_all(fd, (void *)payload, len, true));
}

char *ipc_recv(int fd, uint32_t *type)
{
	char hdr[IPC_HDR];
	if (fd < 0 || !io_all(fd, hdr, IPC_HDR, false) || memcmp(hdr, IPC_MAGIC, 6))
		return NULL;
	uint32_t len, t;
	memcpy(&len, hdr + 6, 4);
	memcpy(&t, hdr + 10, 4);
	if (len > 64 * 1024 * 1024)
		return NULL;
	char *buf = malloc(len + 1);
	if (!buf || !io_all(fd, buf, len, false)) {
		free(buf);
		return NULL;
	}
	buf[len] = '\0';
	if (type)
		*type = t;
	return buf;
}

char *ipc_query(uint32_t type, const char *payload)
{
	if (!ipc_send(S.ipc_cmd, type, payload))
		return NULL;
	return ipc_recv(S.ipc_cmd, NULL);
}

void ipc_command(const char *fmt, ...)
{
	char cmd[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(cmd, sizeof cmd, fmt, ap);
	va_end(ap);
	free(ipc_query(0, cmd)); /* RUN_COMMAND */
}

void ipc_refresh_workspaces(void)
{
	char *r = ipc_query(1, NULL); /* GET_WORKSPACES */
	struct json *j = json_parse(r);
	free(r);
	if (!j)
		return;
	S.nws = 0;
	for (struct json *w = j->child; w && S.nws < MAX_WS; w = w->next) {
		struct workspace *ws = &S.ws[S.nws++];
		ws->num = (int)json_num(w, "num");
		snprintf(ws->name, sizeof ws->name, "%s", json_str(w, "name"));
		snprintf(ws->output, sizeof ws->output, "%s", json_str(w, "output"));
		ws->focused = json_bool(w, "focused");
		ws->visible = json_bool(w, "visible");
		ws->urgent = json_bool(w, "urgent");
	}
	json_free(j);
	mark_dirty(SURF_BAR);
}

static void add_running(const char *id)
{
	if (!id || !*id || S.nrunning >= MAX_RUNNING)
		return;
	for (int i = 0; i < S.nrunning; i++)
		if (!strcmp(S.running[i], id))
			return;
	snprintf(S.running[S.nrunning++], sizeof S.running[0], "%s", id);
}

static void walk(struct json *n)
{
	if (!n)
		return;
	const char *type = json_str(n, "type");
	if (!strcmp(type, "con") || !strcmp(type, "floating_con")) {
		const char *id = json_str(n, "app_id");
		if (!*id) {
			struct json *wp = json_get(n, "window_properties");
			id = json_str(wp, "class"); /* applis X11 */
		}
		if (*id || json_get(n, "pid")) {
			add_running(id);
			if (json_bool(n, "focused")) {
				snprintf(S.title, sizeof S.title, "%s", json_str(n, "name"));
				snprintf(S.focused_app, sizeof S.focused_app, "%s", id);
			}
		}
	}
	struct json *kids = json_get(n, "nodes");
	for (struct json *c = kids ? kids->child : NULL; c; c = c->next)
		walk(c);
	kids = json_get(n, "floating_nodes");
	for (struct json *c = kids ? kids->child : NULL; c; c = c->next)
		walk(c);
}

void ipc_refresh_tree(void)
{
	char *r = ipc_query(4, NULL); /* GET_TREE */
	struct json *j = json_parse(r);
	free(r);
	if (!j)
		return;
	S.nrunning = 0;
	S.title[0] = S.focused_app[0] = '\0';
	walk(j);
	json_free(j);
	mark_dirty(SURF_BAR);
	mark_dirty(SURF_DOCK);
}

void ipc_refresh_inputs(void)
{
	char *r = ipc_query(100, NULL); /* GET_INPUTS */
	struct json *j = json_parse(r);
	free(r);
	if (!j)
		return;
	for (struct json *in = j->child; in; in = in->next) {
		if (strcmp(json_str(in, "type"), "keyboard"))
			continue;
		const char *l = json_str(in, "xkb_active_layout_name");
		if (!*l)
			continue;
		if (!strncmp(l, "French", 6))
			snprintf(S.kb, sizeof S.kb, "AZERTY");
		else if (!strncmp(l, "English", 7))
			snprintf(S.kb, sizeof S.kb, "QWERTY");
		else
			snprintf(S.kb, sizeof S.kb, "%.6s", l);
		break;
	}
	json_free(j);
	mark_dirty(SURF_BAR);
}
