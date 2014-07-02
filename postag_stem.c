/*	$Id: postag_stem.c,v 1.13 2009/08/10 23:32:51 nis Exp $	*/

/*-
 * Copyright (c) 2006 Shingo Nishioka.
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
static char rcsid[] = "$Id: postag_stem.c,v 1.13 2009/08/10 23:32:51 nis Exp $";
#endif

#include <config.h>

#include <sys/types.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "util.h"
#include "postag.h"

#include "postagutil.h"
#include "postagstem.h"

#include <gifnoc.h>

struct obuf {
	char *ptr;
	size_t size, len;
};

struct entry {
	char const *base, *inf;
};

struct table {
	int size;
	struct entry *e;
};

static void killtab(char *);
static int iscapital(char const *);
static int eject_word(struct obuf *, char *, char *);
static int append(struct obuf *, char const *);
static char const *lookup(struct table *, char const *);
static struct table *read_inf(char const *);
static int ecompar(const void *, const void *);

struct {
	struct table *nns;	/* noun: plural */
	struct table *vbd;	/* verb: past tense */
	struct table *vbg;	/* verb: gerund */
	struct table *vbn;	/* verb: past participle */
	struct table *vbz;	/* verb: 3rd singular present */
} inf_tables;

int
postag_stem_init()
{
	if (!(inf_tables.nns = read_inf(INFDIR "nns.inf"))) {
		return -1;
	}
	if (!(inf_tables.vbd = read_inf(INFDIR "vbd.inf"))) {
		return -1;
	}
	if (!(inf_tables.vbg = read_inf(INFDIR "vbg.inf"))) {
		return -1;
	}
	if (!(inf_tables.vbn = read_inf(INFDIR "vbn.inf"))) {
		return -1;
	}
	if (!(inf_tables.vbz = read_inf(INFDIR "vbz.inf"))) {
		return -1;
	}
	return 0;
}

char *
postag_sparse_tostr(msg)
char const *msg;
{
	char *p, *q;
	struct obuf o0, *o = &o0;

	bzero(o, sizeof *o);
	o->ptr = NULL;

/* XXX koko wo kaizou suruto kousokuka dekiru? */
	p = postag_sparse(msg);
	for (; p && *p; p = q) {
		char *r;
		q = index(p, ' ');
		if (q) {
			*q++ = '\0';
		}
		r = rindex(p, '/');
		if (r) {
			*r++ = '\0';
		}
		if (eject_word(o, p, r) == -1) {
			return NULL;
		}
	}
	return o->ptr;
}

static int
eject_word(o, orig, ps)
struct obuf *o;
char *orig, *ps;
{
	char *inf;
	char const *base = NULL;

	killtab(orig);
	killtab(ps);

	if (!(inf = strdup(orig))) {
		return -1;
	}
/*
if ((!strcmp(ps, "NNP") || !strcmp(ps, "PN")) && iscapital(inf)) {
	syslog(LOG_DEBUG, "%s %s", ps, inf);
}
*/
	if (strcmp(ps, "NNP") && strcmp(ps, "PN") && iscapital(inf)) {
		*inf = tolower(*inf & 0xff);
	}

	if (!strcmp(ps, "NNS")) {
		base = lookup(inf_tables.nns, inf);
	}
	else if (!strcmp(ps, "NNPS")) {
		/* ??? */
	}
	else if (!strcmp(ps, "VBD")) {
		base = lookup(inf_tables.vbd, inf);
	}
	else if (!strcmp(ps, "VBG")) {
		base = lookup(inf_tables.vbg, inf);
	}
	else if (!strcmp(ps, "VBN")) {
		base = lookup(inf_tables.vbn, inf);
	}
	else if (!strcmp(ps, "VBZ")) {
		base = lookup(inf_tables.vbz, inf);
	}
	if (!base) {
		base = inf;
	}

	if (append(o, orig) == -1) {
		return -1;
	}
	if (append(o, "\t") == -1) {
		return -1;
	}
	if (append(o, base) == -1) {
		return -1;
	}
	if (append(o, "\t") == -1) {
		return -1;
	}
	if (append(o, ps) == -1) {
		return -1;
	}
	if (append(o, "\n") == -1) {
		return -1;
	}

	free(inf);
	/* free(base) */
	return 0;
}

static void
killtab(s)
char *s;
{
	for (; *s; s++) {
		if (*s == '\t') {
			*s = ' ';
		}
	}
}

static int
iscapital(s)
char const *s;
{
	if (!isupper(*s & 0xff)) {
		return 0;
	}
	for (s++; *s; s++) {
		if (isupper(*s & 0xff) || isdigit(*s & 0xff)) {
			return 0;
		}
	}
	return 1;
}

static int
append(a, s)
struct obuf *a;
char const *s;
{
	size_t l = strlen(s);

	if (a->len + l >= a->size) {
		size_t newsize = a->len + l + 1024;
		void *new;
		if (!(new = realloc(a->ptr, newsize))) {
			syslog(LOG_DEBUG, "realloc: %s", strerror(errno));
			return -1;
		}
		a->ptr = new;
		a->size = newsize;
	}
	memmove(a->ptr + a->len, s, l + 1);
	a->len += l;
	return 0;
}

static char const *
lookup(table, inf)
struct table *table;
char const *inf;
{
	struct entry key, *e;

	key.base = NULL;
	key.inf = inf;
	if (e = bsearch(&key, table->e, table->size, sizeof *table->e, ecompar)) {
		return e->base;
	}

	return NULL;
}

static struct table *
read_inf(path)
char const *path;
{
	char buf[8192];
	size_t n;
	struct table *t = NULL;
	FILE *f = NULL;

	if (!(t = calloc(1, sizeof *t))) {
		syslog(LOG_DEBUG, "calloc: %s", strerror(errno));
		goto error;
	}
	t->size = 0;
	t->e = NULL;
	if (!(f = fopen(path, "r"))) {
		syslog(LOG_DEBUG, "%s: %s", path, strerror(errno));
		goto error;
	}

	for (n = 0; fgets(buf, sizeof buf, f); ) {
		if (!index(buf, '\n')) {
			syslog(LOG_DEBUG, "too long line in %s", path);
			goto error;
		}
		if (index(buf, '\t')) {
			n++;
		}
	}

	t->size = n;
	if (!(t->e = calloc(n, sizeof *t->e))) {
		syslog(LOG_DEBUG, "calloc: %s", strerror(errno));
		goto error;
	}

	rewind(f);

	for (n = 0; fgets(buf, sizeof buf, f); ) {
		char *inf;
		if (inf = index(buf, '\t')) {
			struct entry *e = &t->e[n++];
			char *p;
			*inf++ = '\0';
			if (!(p = index(inf, '\n'))) {
				syslog(LOG_DEBUG, "internal error");
				goto error;
			}
			*p = '\0';
			if (!(e->base = strdup(buf))) {
				syslog(LOG_DEBUG, "strdup: %s", strerror(errno));
				goto error;
			}
			if (!(e->inf = strdup(inf))) {
				syslog(LOG_DEBUG, "strdup: %s", strerror(errno));
				goto error;
			}
		}
	}
	fclose(f);

	qsort(t->e, t->size, sizeof *t->e, ecompar);

	return t;
error:
	if (f) {
		fclose(f);
	}
	if (t) {
		free(t->e);
	}
	free(t);
	return NULL;
}

static int
ecompar(va, vb)
const void *va;
const void *vb;
{
	const struct entry *a = va;
	const struct entry *b = vb;

	return strcasecmp(a->inf, b->inf);
}
