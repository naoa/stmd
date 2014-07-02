/*	$Id: chelate.c,v 1.2 2010/12/10 09:29:08 nis Exp $	*/

/*-
 * Copyright (c) 2010 Shingo Nishioka.
 * All rights reserved.
 */

#ifndef lint
static char rcsid[] = "$Id: chelate.c,v 1.2 2010/12/10 09:29:08 nis Exp $";
#endif

#include <sys/types.h>
#include <sys/select.h>

#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct sendbuf {
	char const *p;
	size_t n, offs;
};

struct recvbuf {
	char *p;
	size_t n, size;
};

struct total {
	size_t sent, recv;
};

struct chelate_t {
	int fds[2];
	pid_t cpid;
	struct recvbuf recvbuf;
	struct total total;
};

struct chelate_t *
chelate_create(spawn_ext, cookie)
void (*spawn_ext)(void *);
void *cookie;
{
	struct chelate_t *st;

	struct fds {
		int up[2];
		int down[2];
	};

	struct fds fds;

	if (!(st = calloc(1, sizeof *st))) {
		perror("calloc");
		return NULL;
	}

	st->total.sent = st->total.recv = 0;

	st->recvbuf.p = NULL;
	st->recvbuf.n = st->recvbuf.size = 0;

	if (pipe(fds.up) == -1) {
		perror("pipe");
		return NULL;
	}
	if (pipe(fds.down) == -1) {
		perror("pipe");
		return NULL;
	}
	switch (st->cpid = fork()) {
	case -1:
		perror("fork");
		return NULL;
	case 0:
		close(fds.up[1]);
		close(fds.down[0]);
		if (dup2(fds.up[0], 0) == -1) {
			perror("dup2");
			return NULL;
		}
		close(fds.up[0]);
		if (dup2(fds.down[1], 1) == -1) {
			perror("dup2");
			return NULL;
		}
		close(fds.down[1]);
		(*spawn_ext)(cookie);
		_exit(1);
		break;
	default:
		break;
	}

	close(fds.up[0]);
	close(fds.down[1]);

	st->fds[0] = fds.down[0];
	st->fds[1] = fds.up[1];

	fcntl(st->fds[1], F_SETFL, fcntl(st->fds[1], F_GETFL) | O_NONBLOCK);
	return st;
}

int
chelate_sendrecv(st, p, n, readfn, cookie)
struct chelate_t *st;
char const *p;
size_t n;
int (*readfn)(char *, size_t, void *);
void *cookie;
{
	int rdf, wrf;

	struct sendbuf sendbuf;

	sendbuf.p = p;
	sendbuf.n = n;
	sendbuf.offs = 0;

	rdf = st->fds[0];
	wrf = st->fds[1];

	for (;;) {
		fd_set readfds, writefds;
		int nfds;

		nfds = 0;
		FD_ZERO(&readfds);
		FD_ZERO(&writefds);
		FD_SET(rdf, &readfds);
		if (nfds <= rdf) {
			nfds = rdf + 1;
		}
		if (sendbuf.offs < sendbuf.n) {
			FD_SET(wrf, &writefds);
			if (nfds <= wrf) {
				nfds = wrf + 1;
			}
		}

		if ((nfds = select(nfds, &readfds, &writefds, NULL, NULL)) == -1) {
			if (errno == EINTR) {
				continue;
			}
			perror("select");
			return -1;
		}
		if (nfds == 0) {
			break;
		}

		if (FD_ISSET(wrf, &writefds)) {
			char const *p0;
			ssize_t ss, c;

			ss = sendbuf.n - sendbuf.offs;
			p0 = sendbuf.p + sendbuf.offs;
			if ((c = write(wrf, p0, ss)) == -1) {
				if (errno == EAGAIN) {
					goto cont;
				}
				perror("write");
				return -1;
			}
			st->total.sent += c;
			sendbuf.offs += c;
		}

	cont:

		if (FD_ISSET(rdf, &readfds)) {
			char *p0;
			ssize_t rs, c;
			int endp = 0;

			rs = st->recvbuf.size - st->recvbuf.n;
			if (rs <= 8192) {
				void *new;
				size_t newsize;
				newsize = st->recvbuf.size + 8192;
				if (!(new = realloc(st->recvbuf.p, newsize))) {
					perror("realloc");
					return -1;
				}
				st->recvbuf.p = new;
				st->recvbuf.size = newsize;
				rs = st->recvbuf.size - st->recvbuf.n;
			}
			p0 = st->recvbuf.p + st->recvbuf.n;
			if ((c = read(rdf, p0, rs)) == -1) {
				perror("read");
				return -1;
			}
			if (c == 0) {
				break;
			}
			st->recvbuf.n += c;

			for (p0 = st->recvbuf.p;;) {
				char *p1;
				rs = st->recvbuf.n - (p0 - st->recvbuf.p);
				if (!(p1 = memchr(p0, '\n', rs))) {
					break;
				}
				p1++;

				st->total.recv += p1 - p0;
				if ((endp = (*readfn)(p0, p1 - p0, cookie)) == -1) {
					return -1;
				}
				p0 = p1;
				if (endp == 1) {
					break;
				}
			}

			if (p0 != st->recvbuf.p) {
				rs = st->recvbuf.n - (p0 - st->recvbuf.p);
				memmove(st->recvbuf.p, p0, rs);
				st->recvbuf.n = rs;
			}
			if (endp) {
				break;
			}
		}
	}
	return 0;
}

int
chelate_destroy(st, verbose)
struct chelate_t *st;
{
	int status;

	close(st->fds[0]);
	close(st->fds[1]);

	free(st->recvbuf.p);

	if (waitpid(st->cpid, &status, 0) == -1) {
		perror("waitpid");
	}
	if (verbose) {
		printf("%d\n", status);
		fprintf(stderr, "total: %d %d\n", (int)st->total.sent, (int)st->total.recv);
	}
	return 0;
}
