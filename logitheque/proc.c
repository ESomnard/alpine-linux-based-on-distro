/* Logithèque Plume — processus en arrière-plan (apk, flatpak, sudo) sans bloquer la fenêtre. */
#include "logitheque.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* Lance argv ; sortie standard et erreurs réunies. stdin_data (mot de passe
 * pour sudo -S) est écrit puis l'entrée est fermée. */
struct proc *proc_run(char *const argv[], const char *stdin_data, proc_done_fn done, int tag, bool is_job)
{
	struct proc *p = NULL;
	for (int i = 0; i < MAX_PROCS; i++)
		if (!A.procs[i].used) {
			p = &A.procs[i];
			break;
		}
	if (!p)
		return NULL;
	int out[2], in[2];
	if (pipe2(out, O_CLOEXEC) < 0)
		return NULL;
	if (pipe2(in, O_CLOEXEC) < 0) {
		close(out[0]);
		close(out[1]);
		return NULL;
	}
	pid_t pid = fork();
	if (pid < 0) {
		close(out[0]); close(out[1]); close(in[0]); close(in[1]);
		return NULL;
	}
	if (pid == 0) {
		for (int s = 1; s < NSIG; s++)
			signal(s, SIG_DFL);
		dup2(in[0], 0);
		dup2(out[1], 1);
		dup2(out[1], 2);
		setenv("LC_ALL", "C", 1); /* messages d'apk/flatpak stables à analyser */
		execvp(argv[0], argv);
		fprintf(stderr, "impossible de lancer %s : %s\n", argv[0], strerror(errno));
		_exit(127);
	}
	close(out[1]);
	close(in[0]);
	if (stdin_data) {
		ssize_t n = write(in[1], stdin_data, strlen(stdin_data));
		(void)n;
	}
	close(in[1]);
	fcntl(out[0], F_SETFL, O_NONBLOCK);
	memset(p, 0, sizeof *p);
	p->used = true;
	p->pid = pid;
	p->fd = out[0];
	p->done = done;
	p->tag = tag;
	p->is_job = is_job;
	return p;
}

void proc_poll_fds(struct pollfd *fds, int *nfds, int base)
{
	(void)base;
	for (int i = 0; i < MAX_PROCS; i++)
		if (A.procs[i].used && A.procs[i].fd >= 0)
			fds[(*nfds)++] = (struct pollfd){ .fd = A.procs[i].fd, .events = POLLIN };
}

/* dernière ligne utile de la sortie d'une tâche (progression affichée) */
static void job_progress_from(struct proc *p)
{
	if (!p->is_job || !p->len)
		return;
	/* fin du texte, en coupant sur \n et \r (barres de progression) */
	size_t end = p->len;
	while (end > 0 && (p->out[end - 1] == '\n' || p->out[end - 1] == '\r' || p->out[end - 1] == ' '))
		end--;
	size_t start = end;
	while (start > 0 && p->out[start - 1] != '\n' && p->out[start - 1] != '\r')
		start--;
	size_t n = end - start;
	if (n >= sizeof A.job_line)
		n = sizeof A.job_line - 1;
	memcpy(A.job_line, p->out + start, n);
	A.job_line[n] = '\0';
	/* « (3/12) Installing … » (apk) ou « … 45% » (flatpak) */
	int a, b;
	const char *par = strrchr(A.job_line, '(');
	char *pct = strrchr(A.job_line, '%');
	if (A.job_line[0] == '(' && sscanf(A.job_line, "(%d/%d)", &a, &b) == 2 && b > 0)
		A.job_progress = (double)a / b;
	else if (pct && pct > A.job_line) {
		const char *q = pct - 1;
		while (q > A.job_line && q[-1] >= '0' && q[-1] <= '9')
			q--;
		A.job_progress = atoi(q) / 100.0;
	}
	(void)par;
	request_redraw();
}

void proc_handle(struct pollfd *fds, int nfds)
{
	for (int k = 0; k < nfds; k++) {
		if (!(fds[k].revents & (POLLIN | POLLHUP | POLLERR)))
			continue;
		for (int i = 0; i < MAX_PROCS; i++) {
			struct proc *p = &A.procs[i];
			if (!p->used || p->fd != fds[k].fd)
				continue;
			for (;;) {
				if (p->len + 4096 + 1 > p->cap) {
					size_t nc = p->cap ? p->cap * 2 : 16384;
					char *no = realloc(p->out, nc);
					if (!no)
						break;
					p->out = no;
					p->cap = nc;
				}
				ssize_t n = read(p->fd, p->out + p->len, p->cap - p->len - 1);
				if (n > 0) {
					p->len += n;
					p->out[p->len] = '\0';
					continue;
				}
				if (n < 0 && errno == EINTR)
					continue;
				if (n < 0 && errno == EAGAIN)
					break;
				/* fin de sortie */
				close(p->fd);
				p->fd = -1;
				break;
			}
			job_progress_from(p);
		}
	}
}

/* processus terminés : on appelle leur suite */
void proc_reap(void)
{
	for (int i = 0; i < MAX_PROCS; i++) {
		struct proc *p = &A.procs[i];
		if (!p->used || p->fd >= 0)
			continue;
		int st = 0;
		pid_t r = waitpid(p->pid, &st, WNOHANG);
		if (r == 0)
			continue;
		int code = r == p->pid && WIFEXITED(st) ? WEXITSTATUS(st) : -1;
		if (!p->out) {
			p->out = calloc(1, 1);
			p->len = 0;
		}
		proc_done_fn fn = p->done;
		struct proc copy = *p;
		p->used = false;
		if (fn)
			fn(&copy, code);
		free(copy.out);
		request_redraw();
	}
}
