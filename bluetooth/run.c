/* Panneau Plume — lance les commandes système une par une, sans shell (execvp :
 * un nom d'appareil ne peut pas devenir une commande), et lit leur sortie sans
 * bloquer la fenêtre. Le panneau reçoit le résultat dans run_done(). */
#include "panel.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_ARGS 12
#define QLEN 48
#define OUT_MAX 131072

struct cmd {
	int job;
	char tag[64];
	char *argv[MAX_ARGS + 1];
	int argc;
};

static struct cmd queue[QLEN], cur;
static int qn;
static pid_t pid = -1;
static int pfd = -1;
static char out[OUT_MAX];
static size_t outn;

static void cmd_free(struct cmd *c)
{
	for (int i = 0; i < c->argc; i++)
		free(c->argv[i]);
	c->argc = 0;
}

void run_push(int job, const char *tag, int dedupe, ...)
{
	struct cmd *c = NULL;
	for (int i = 0; dedupe && i < qn; i++)
		if (queue[i].job == job && !strcmp(queue[i].tag, tag ? tag : "")) {
			if (dedupe == RUN_SKIP)
				return;
			c = &queue[i]; /* RUN_REPLACE : on garde la place, nouveaux arguments */
			cmd_free(c);
			break;
		}
	if (!c) {
		if (qn >= QLEN)
			return;
		c = &queue[qn++];
	}
	memset(c, 0, sizeof *c);
	c->job = job;
	snprintf(c->tag, sizeof c->tag, "%s", tag ? tag : "");
	va_list ap;
	va_start(ap, dedupe);
	const char *a;
	while (c->argc < MAX_ARGS && (a = va_arg(ap, const char *)))
		c->argv[c->argc++] = strdup(a);
	va_end(ap);
	c->argv[c->argc] = NULL;
}

bool run_idle(void) { return pid < 0 && qn == 0; }
int run_fd(void) { return pfd; }

void run_start(void)
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
		setenv("LC_ALL", "C.UTF-8", 1); /* sorties en anglais : c'est ce qu'on analyse */
		execvp(cur.argv[0], cur.argv);
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

/* enlève les couleurs ANSI et les retours chariot */
static void strip(char *s)
{
	char *w = s;
	for (char *r = s; *r; r++) {
		if (*r == 0x1b) {
			if (r[1] == '[') {
				r += 2;
				while (*r && !((*r >= 'a' && *r <= 'z') || (*r >= 'A' && *r <= 'Z')))
					r++;
				if (!*r)
					break;
			}
			continue;
		}
		if (*r == 0x01 || *r == 0x02 || *r == '\r') /* marqueurs readline de bluetoothctl */
			continue;
		*w++ = *r;
	}
	*w = 0;
}

void run_read(void)
{
	for (;;) {
		ssize_t n = read(pfd, out + outn, OUT_MAX - 1 - outn);
		if (n > 0) {
			outn += (size_t)n;
			if (outn >= OUT_MAX - 1)
				outn = 0;
			continue;
		}
		if (n < 0 && (errno == EAGAIN || errno == EINTR))
			return;
		break;
	}
	close(pfd);
	pfd = -1;
	int st = 0;
	waitpid(pid, &st, 0);
	pid = -1;
	out[outn] = 0;
	strip(out);
	struct cmd c = cur;
	run_done(c.job, c.tag, WIFEXITED(st) ? WEXITSTATUS(st) : 1, out);
	cmd_free(&c);
	A.dirty = true;
	run_start();
}
