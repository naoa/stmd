/*	$Id: extsutil_sel.c,v 1.4 2011/10/24 06:30:26 nis Exp $	*/

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
static char rcsid[] = "$Id: extsutil_sel.c,v 1.4 2011/10/24 06:30:26 nis Exp $";
#endif

#include <config.h>

#include <sys/types.h>

#include <getopt.h>
#if defined STANDALONE
#include <libgen.h>
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <syslog.h>
#include <unistd.h>

#include <unicode/utf.h>
#include <unicode/utf8.h>

#include "extsutil.h"
#include "chelate.h"

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
	struct chelate_t *st;
	struct exts_node_t *n, *pool;
	struct exts_node_t *sentinel;
	char *opts;
	struct {
		char *buf;
		size_t size;
	} b;
};

static struct exts_node_t *alloc_a_node(struct exts_t *);
static void reclaim_nodes(struct exts_t *, struct exts_node_t *);
static void free_nodes(struct exts_node_t *);
static int exts_start(struct exts_t *);
static void start_ext(void *);
static int rcv_init(struct exts_t *);
static int rcv_line(struct exts_t *, char *);
static int consumer(char *, size_t, void *);
static int prepare_write(struct exts_t *, char const *);
static int verify_utf8_string(char const *, size_t *);
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
	if (!c->initialized && exts_start(c) == -1) {
		return NULL;
	}

	if (rcv_init(c) == -1) {
		return NULL;
	}

	if (prepare_write(c, buf) == -1) {
		return NULL;
	}

	if (chelate_sendrecv(c->st, c->b.buf, strlen(c->b.buf), consumer, c) == -1) {
		(void)chelate_destroy(c->st, 1);
		return NULL;
	}

	if (!c->n) {
		return NULL;
	}
	return c->n->next;
}

static int
exts_start(c)
struct exts_t *c;
{
	if (!(c->st = chelate_create(start_ext, c))) {
		return -1;
	}

	c->initialized = 1;
	return 0;
}

static void
start_ext(cookie)
void *cookie;
{
	struct exts_t *c;
	char **argv = exts_default_argv;
	char *cmd;
	cmd = EXTS_CMD;

	c = cookie;

	if (c->opts && !(argv = parse_opts(cmd, c->opts))) {
		fprintf(stderr, "parse_opts failed\n");
		_exit(1);
	}
	if (execve(cmd, argv, environ)) {
		perror(cmd);
		_exit(1);
	}
}

static int
rcv_init(c)
struct exts_t *c;
{
	if (!c->n) {
		if (!(c->n = calloc(1, sizeof *c->n))) {
			return -1;
		}
		c->n->next = NULL;
		c->n->surface = "sentinel's surface. do not free.";
		c->n->size = 0;
	}

	c->sentinel = c->n;
	reclaim_nodes(c, c->sentinel->next);
	c->sentinel->next = NULL;
	return 0;
}

static int
rcv_line(c, rcvbuf)
struct exts_t *c;
char *rcvbuf;
{
	size_t erroffs;
	size_t l;
	if (*rcvbuf == SEPARATOR_C) {
		return 1;	/* END */
	}
	if (!(c->sentinel->next = alloc_a_node(c))) {
		return -1;
	}
	c->sentinel = c->sentinel->next;
	l = strlen(rcvbuf);
	if (!verify_utf8_string(rcvbuf, &erroffs)) {
		fprintf(stderr, "malformed UTF-8 encoding: offset = %ld\n", (long)erroffs);
		return -1;
	}
	if (l >= c->sentinel->size) {
		size_t newsize = l + 10;
		void *new;
		if (!(new = realloc(c->sentinel->surface, newsize))) {
			return -1;
		}
		c->sentinel->size = newsize;
		c->sentinel->surface = new;
	}
	strcpy(c->sentinel->surface, rcvbuf);
	return 0;	/* continue */
}

static int
consumer(p, n, cookie)
char *p;
size_t n;
void *cookie;
{
	if (n > 0) {
		struct exts_t *c = cookie;
		p[n - 1] = '\0';
		return rcv_line(c, p);
	}
	return 0;
}

#define XS0(a)	#a
#define XS1(a)	XS0(\a)
#define	SEPARATOR_S	XS1(SEPARATOR_C)

static int
prepare_write(c, buf)
struct exts_t *c;
char const *buf;
{
	char *p, *q;
	size_t l;

	l = strlen(buf);
	if (l + 4 > c->b.size) {
		size_t newsize;
		void *new;
		newsize = l + 4;
		if (!(new = realloc(c->b.buf, newsize))) {
			return -1;
		}
		c->b.buf = new;
		c->b.size = newsize;
	}
	memcpy(c->b.buf, buf, l + 1);

	for (p = c->b.buf; q = strchr(p, SEPARATOR_C); p = q) {
		*q = ' ';
	}

	memcpy(c->b.buf + l, "\n" SEPARATOR_S "\n", 4);
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
	c->opts = opts;

	c->b.buf = NULL;
	c->b.size = 0;
	return c;
}

int
exts_close(c)
struct exts_t *c;
{
	if (!c->initialized) {
		free(c);
		return 0;
	}

	if (chelate_destroy(c->st, 0) == -1) {
		return -1;
	}

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

	argv = calloc(n + 1, sizeof *argv);
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
