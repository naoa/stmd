/*	$Id: extsutil.c,v 1.14 2011/10/24 06:30:26 nis Exp $	*/

/*-
 * Copyright (c) 2009 Shingo Nishioka.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#if ! defined lint
static char rcsid[] = "$Id: extsutil.c,v 1.14 2011/10/24 06:30:26 nis Exp $";
#endif

#include <config.h>

#include <sys/types.h>

#include <getopt.h>
#if defined STANDALONE
#include <libgen.h>
#endif
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <syslog.h>
#include <unistd.h>

#include <unicode/utf.h>
#include <unicode/utf8.h>

#include "extsutil.h"

#include <gifnoc.h>

#define	CHOP(s) do { \
	char *p; \
	if (p = index((s), '\n')) { \
		*p = '\0'; \
		if (p > (s) && *--p == '\r') { \
			*p = '\0'; \
		} \
	} \
} while (0)

#define	PRIuSIZE_T	"ld"
typedef long pri_size_t;

extern char **environ;

struct exts_t {
	int initialized;
	pthread_t thread;
	pthread_barrier_t barrier;
	char const *buf;
	int fds[2];
	FILE *g;
	struct exts_node_t *n, *pool;
	char *rcvbuf;
	size_t size;
	char *opts;
};

static struct exts_node_t *alloc_a_node(struct exts_t *);
static void reclaim_nodes(struct exts_t *, struct exts_node_t *);
static void free_nodes(struct exts_node_t *);
static int exts_start(struct exts_t *, char *, char **);
static int snd(struct exts_t *, char const *);
static int rcv(struct exts_t *);
static void *start_routine(void *);
static int writer(struct exts_t *);
static int verify_utf8_string(char const *, size_t *);
static char *fgets_r(char **, size_t *, FILE *);
static char **parse_opts(char *, char *);
#if defined STANDALONE
static void usage(void);

static char *progname;
static int vflag = 0;
#endif

#define	EXTS_CMD STMD_MYSTEMMER_DIR "/stmd-exts.sh"
static char *exts_default_argv[] = {EXTS_CMD, "-b", "65536", NULL};
#define	SEPARATOR_C 037

char *
exts_linefil(struct exts_node_t *q)
{
	CHOP(q->surface);		/* remove trailing newline characters */
	if (strcmp(q->surface, "EOS") == 0) {	/* if the line is "EOS", discard it. */
		return NULL;
	}
	return q->surface;		/* return the whole line as a word. */

#if 0	/* THE FOLLOWING IS A SAMPLE CODE FOR MECAB+UNIDIC */
 {
	char *p;
	if ((p = strchr(q->surface, '\t'))) {
		*p = '\0';
		return q->surface;
	}
	return NULL;
 }
#endif
}

static struct exts_node_t *
alloc_a_node(c)
struct exts_t *c;
{
	struct exts_node_t *n;
	if (c->pool) {
		n = c->pool;
		c->pool = c->pool->next;
		n->next = NULL;
		return n;
	}
	if (!(n = calloc(1, sizeof *n))) {
		return NULL;
	}
	n->next = NULL;
	n->surface = NULL;
	n->size = 0;
	return n;
}

static void
reclaim_nodes(c, n)
struct exts_t *c;
struct exts_node_t *n;
{
	struct exts_node_t *p;
	if (!n) return;
	for (p = n; p->next; p = p->next) ;
	p->next = c->pool;
	c->pool = n;
}

static void
free_nodes(n)
struct exts_node_t *n;
{
	struct exts_node_t *nx;
	for (; n; n = nx) {
		nx = n->next;
		free(n->surface);
		free(n);
	}
}

struct exts_node_t *
exts_sparse_tonode(c, buf)
struct exts_t *c;
char const *buf;
{
	if (!c->initialized && exts_start(c, EXTS_CMD, exts_default_argv) == -1) {
		return NULL;
	}
	if (snd(c, buf) == -1) {
		return NULL;
	}
	if (rcv(c) == -1) {
		return NULL;
	}
	if (!c->n) {
		return NULL;
	}
	return c->n->next;
}

static int
exts_start(c, cmd, argv)
struct exts_t *c;
char *cmd, **argv;
{
	int fdsA[2], fdsB[2];

	if (pipe(fdsA) == -1) {
		perror("pipe");
		return -1;
	}
	if (pipe(fdsB) == -1) {
		perror("pipe");
		return -1;
	}

	switch (fork()) {
	case -1:
		perror("fork");
		return -1;
	case 0:
		if (c->opts && !(argv = parse_opts(cmd, c->opts))) {
			fprintf(stderr, "parse_opts failed\n");
			_exit(1);
		}
		dup2(fdsA[0], 0);
		close(fdsA[1]);
		dup2(fdsB[1], 1);
		close(fdsB[0]);
		if (execve(cmd, argv, environ)) {
			perror(cmd);
			_exit(1);
		}
		_exit(0);
	default:
		close(fdsA[0]);
		close(fdsB[1]);
		c->fds[0] = fdsB[0];
		c->fds[1] = fdsA[1];
	}

	if (pthread_barrier_init(&c->barrier, NULL, 2)) {
		perror("pthread_barrier_init");
		return -1;
	}

        if (pthread_create(&c->thread, NULL, start_routine, c)) {
		perror("pthread_create");
		return -1;
	}

	if (!(c->g = fdopen(c->fds[0], "r"))) {
		perror("fdopen");
		return -1;
	}
	c->initialized = 1;
	return 0;
}

static int
snd(c, buf)
struct exts_t *c;
char const *buf;
{
	c->buf = buf;
#if defined DBG
syslog(LOG_DEBUG, "ext: snd: barrier A");
#endif
	switch (pthread_barrier_wait(&c->barrier)) {
	case 0:
	case PTHREAD_BARRIER_SERIAL_THREAD:
		break;
	default:
		perror("pthread_barrier_signal");
		return -1;
	}
#if defined DBG
syslog(LOG_DEBUG, "ext: snd: barrier B");
#endif
	return 0;
}

static int
rcv(c)
struct exts_t *c;
{
	struct exts_node_t *t;
	if (!c->n) {
		if (!(c->n = calloc(1, sizeof *c->n))) {
			return -1;
		}
		c->n->next = NULL;
		c->n->surface = "sentinel's surface. do not free.";
		c->n->size = 0;
	}

	t = c->n;	/* t == sentinel */
	reclaim_nodes(c, t->next);
	t->next = NULL;
	while (fgets_r(&c->rcvbuf, &c->size, c->g)) {
		size_t erroffs;
		size_t l;
		if (*c->rcvbuf == SEPARATOR_C) {
			break;
		}
		if (!(t->next = alloc_a_node(c))) {
			return -1;
		}
		t = t->next;
		l = strlen(c->rcvbuf);
		if (!verify_utf8_string(c->rcvbuf, &erroffs)) {
			fprintf(stderr, "malformed UTF-8 encoding: offset = %ld\n", (long)erroffs);
			return -1;
		}
		if (l >= t->size) {
			size_t newsize = l + 10;
			void *new;
			if (!(new = realloc(t->surface, newsize))) {
				return -1;
			}
			t->size = newsize;
			t->surface = new;
		}
		strcpy(t->surface, c->rcvbuf);
	}
#if defined DBG
syslog(LOG_DEBUG, "ext: rcv: barrier B");
#endif
	switch (pthread_barrier_wait(&c->barrier)) {
	case 0:
	case PTHREAD_BARRIER_SERIAL_THREAD:
		break;
	default:
		perror("pthread_barrier_signal");
		return -1;
	}
#if defined DBG
syslog(LOG_DEBUG, "ext: rcv: barrier B+");
#endif
	return 0;
}

static void *
start_routine(cookie)
void *cookie;
{
	struct exts_t *c = cookie;
	if (writer(c) == -1) {
		return "error";
	}
	return NULL;
}

#define XS0(a)	#a
#define XS1(a)	XS0(\a)
#define	SEPARATOR_S	XS1(SEPARATOR_C)

static int
writer(c)
struct exts_t *c;
{
	ssize_t l;
	FILE *f;

#if defined DBG
syslog(LOG_DEBUG, "ext: writer: start");
#endif
	if (!(f = fdopen(c->fds[1], "w"))) {
		perror("fdopen");
		return -1;
	}

	for (;;) {
		char const *p, *q;
#if defined DBG
syslog(LOG_DEBUG, "ext: writer: barrier A");
#endif
		switch (pthread_barrier_wait(&c->barrier)) {
		case 0:
		case PTHREAD_BARRIER_SERIAL_THREAD:
			break;
		default:
			perror("pthread_barrier_wait");
			return -1;
		}
#if defined DBG
syslog(LOG_DEBUG, "ext: writer: barrier A+");
#endif
		if (!c->buf) {
			break;
		}

		for (p = c->buf; p; p = q) {
			if (q = index(p, SEPARATOR_C)) {
				l = q - p;
				q = q + 1;
			}
			else {
				l = strlen(p);
			}
			if (l > 0) {
				if (fwrite(p, 1, l, f) != l) {
					perror("fwrite");
					return -1;
				}
			}
		}

		if (fwrite("\n" SEPARATOR_S "\n", 1, 3, f) != 3) {
			perror("fwrite");
			return -1;
		}
		fflush(f);
#if defined DBG
syslog(LOG_DEBUG, "ext: writer: barrier B");
#endif
		switch (pthread_barrier_wait(&c->barrier)) {
		case 0:
		case PTHREAD_BARRIER_SERIAL_THREAD:
			break;
		default:
			perror("pthread_barrier_wait");
			return -1;
		}
#if defined DBG
syslog(LOG_DEBUG, "ext: writer: barrier B+");
#endif
	}
	fclose(f);
	fprintf(stderr, "bye.\n");
	return 0;
}

struct exts_t *
exts_init(opts)
char *opts;
{
	struct exts_t *c;

	if (!(c = calloc(1, sizeof *c))) {
		return NULL;
	}

	c->initialized = 0;
	c->n = NULL;
	c->pool = NULL;
	c->rcvbuf = NULL;
	c->size = 0;
	c->opts = opts;

	return c;
}

int
exts_close(c)
struct exts_t *c;
{
	void *value;

	if (!c->initialized) {
		free(c);
		return 0;
	}

	(void)snd(c, NULL);

	fclose(c->g);

        if (pthread_join(c->thread, &value)) {
                perror("pthread_join");
                return -1;
        }
        if (value) {
                fprintf(stderr, "error: %s\n", (char *)value);
                return -1;
        }
        if (pthread_barrier_destroy(&c->barrier)) {
                perror("pthread_barrier_destroy");
                return -1;
        }

	free(c->rcvbuf);

	if (c->n) {	/* have sentinel */
		free_nodes(c->n->next);
		/* do not free(c->n->surface); */
		free(c->n);
	}
	free_nodes(c->pool);

	free(c);

	return 0;
}

static int
verify_utf8_string(s, erroffs)
char const *s;
size_t *erroffs;
{
	size_t length = strlen(s);
	int32_t i;
	for (i = 0; i < length; ) {
		UChar32 u;
		U8_NEXT(s, i, length, u);
		if (u < 0) {
			*erroffs = i;
			return 0;
		}
	}

	return 1;
}

/* NOTE: almost same as readln */
static char *
fgets_r(bufp, sizep, stream)
char **bufp;
size_t *sizep;
FILE *stream;
{
	size_t offs, size;
	char *buf;

	size = *sizep;
	buf = *bufp;
	offs = 0;
	do {
		size_t rest;
		rest = size - offs;
		if (rest <= 1) {
			size_t newsize = size + 2;
			void *new;
			if (!(new = realloc(buf, newsize))) {
				return NULL;
			}
			buf = new;
			size = newsize;
			rest = size - offs;
		}
		assert(rest > 0);
		if (!fgets(buf + offs, rest, stream)) {
			break;
		}
		offs = strlen(buf);
	} while (!index(buf, '\n'));
	*bufp = buf;
	*sizep = size;
	return buf;
}

/*
 * NOTE: this parser automatically allocates area for argv.
 *       The static version (parse_opts_r) is in mecabutil.c
 */
static char **
parse_opts(cmd, opts)
char *cmd, *opts;
{
	char **argv;
	char *p;
	size_t i, n;

	for (n = 1, p = opts; p && *p; n++) {
		if (p = index(p, ',')) {
			p++;
		}
	}

	if (!(argv = calloc(n + 1, sizeof *argv))) {
		return NULL;
	}
	argv[0] = cmd;
#if defined STANDALONE
	if (vflag) {
		fprintf(stderr, "argv[%d]: \"%s\"\n", 0, argv[0]);
	}
#endif
#if defined DBG
	syslog(LOG_DEBUG, "argv[%d]: \"%s\"", 0, argv[0]);
#endif
	for (i = 1, p = opts; p && *p; i++) {
		argv[i] = p;
		if (p = index(p, ',')) {
			*p++ = '\0';
		}
#if defined STANDALONE
		if (vflag) {
			fprintf(stderr, "argv[%"PRIuSIZE_T"]: \"%s\"\n", (pri_size_t)i, argv[i]);
		}
#endif
#if defined DBG
		syslog(LOG_DEBUG, "argv[%"PRIuSIZE_T"]: \"%s\"", (pri_size_t)i, argv[i]);
#endif
	}
	argv[i] = NULL;
#if defined STANDALONE
	if (vflag) {
		fprintf(stderr, "argv[%"PRIuSIZE_T"]: NULL\n", (pri_size_t)i);
	}
#endif
#if defined DBG
	syslog(LOG_DEBUG, "argv[%"PRIuSIZE_T"]: NULL", (pri_size_t)i);
#endif
	assert(i == n);

	return argv;
}

#if defined STANDALONE
static void usage(void);

static void
usage()
{
	fprintf(stderr, "usage: %s [-v] [-o opts]\n", progname);
	exit(1);
}

int
main(argc, argv)
char *argv[];
{
	static char buf[65536];
	struct exts_t *c;
	char *opts = NULL;
	int ch;

	progname = basename(argv[0]);

	while ((ch = getopt(argc, argv, "vo:")) != -1) {
		switch (ch) {
		case 'v':
			vflag = 1;
			break;
		case 'o':
			opts = optarg;
			break;
		default:
			usage();
		}
	}

	if (!(c = exts_init(opts))) {
		return 1;
	}

	for (; fgets(buf, sizeof buf, stdin); ) {
		struct exts_node_t *n;
		if (!(n = exts_sparse_tonode(c, buf))) {
			break;
		}
		for (; n; n = n->next) {
			char const *w;
			if (!(w = exts_linefil(n))) {
				continue;
			}
			printf("[%s]\n", w);
		}
	}
	fprintf(stderr, "end.");
	if (exts_close(c) == -1) {
		return 1;
	}

	return 0;
}
#endif
