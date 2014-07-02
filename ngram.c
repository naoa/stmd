/*	$Id: ngram.c,v 1.15 2010/01/25 23:08:52 nis Exp $	*/

/*-
 * Copyright (c) 2008 Shingo Nishioka.
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
static char rcsid[] = "$Id: ngram.c,v 1.15 2010/01/25 23:08:52 nis Exp $";
#endif

#include <config.h>

#include <sys/types.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <syslog.h>

#include <unicode/utf.h>
#include <unicode/utf8.h>

#include "util.h"
#include "ngram.h"

#include <gifnoc.h>

static unsigned int nextchar(struct ngram_t *);
static void ng_output(struct ngram_t *, unsigned int *, int, int);

struct ngram_t {
	struct ngram_node_t *pool, *result;
	uint8_t *s;
	size_t length;
	int i;
	int last;
	char *opts;
	int ndig;
};

#define	BFS	6

struct ngram_t *
ngram_init(opts)
char *opts;
{
	struct ngram_t *n;
	if (!(n = calloc(1, sizeof *n))) {
		return NULL;
	}
	n->ndig = 3;
/*syslog(LOG_DEBUG, "ngram_init: opts = %s", opts ? opts : "(null)");*/
	if (n->opts = opts) {
		char *end;
		int nd;
		nd = strtol(opts, &end, 10);
		if (*opts && !*end && nd <= BFS) {
			n->ndig = nd;
/*syslog(LOG_DEBUG, "ngram_init: nd = %d", nd);*/
		}
	}
	n->pool = n->result = NULL;
	return n;
}

void
ngram_destroy(n)
struct ngram_t *n;
{
	free(n);
}

struct ngram_node_t const *
ngram_sparse_tonode(n, s)
struct ngram_t *n;
char const *s;
{
	unsigned int buf[BFS];
	int i;

	n->length = strlen(s);
	n->s = (uint8_t *)s;
	n->i = 0;
	n->last = '.';

	if (n->result) {
		struct ngram_node_t *p;
		for (p = n->result; free((void *)p->surface), p->next; p = (struct ngram_node_t *)p->next) ;
		p->next = n->pool;
		n->pool = n->result;
		n->result = NULL;
	}

#define	shift(b)	(memmove(&b[0], &b[1], sizeof (b) - sizeof (b)[0]))

	bzero(buf, sizeof buf);

	for (i = 0; i < nelems(buf) && (buf[i] = nextchar(n)); i++) ;

	if (i == 0) {
		return NULL;
	}

	do {
		ng_output(n, buf, i, n->ndig);
		shift(buf);
		
	} while (buf[nelems(buf) - 1] = nextchar(n));

	for (i--; i > 0; i--) {
		ng_output(n, buf, i, n->ndig);
		shift(buf);
	}

	return n->result;
}

static unsigned int
nextchar(n)
struct ngram_t *n;
{
	UChar32 u;
	if (n->last == 'S') {
		for (; n->i < n->length; ) {
			U8_NEXT(n->s, n->i, n->length, u);
			if (u < 0) return 0;
			if (!isspace(u & 0xff)) goto brk;
		}
		return 0;
	}
	else {
		if (n->i >= n->length) return 0;
		U8_NEXT(n->s, n->i, n->length, u);
		if (u < 0) return 0;
	}
brk:
	n->last = isspace(u & 0xff) ? 'S' : '.';
	return u;
}

static void
ng_output(n, b, size, ndig)
struct ngram_t *n;
unsigned int *b;
{
	uint8_t s[BFS * 8];
	int i;
	struct ngram_node_t *p;
	for (i = 0; i < size; i++) {
		int j;
		size_t o;
		int u3 = 0;
		for (j = 0, o = 0, u3 = 0; j <= i; j++) {
			UBool isError;
			unsigned int u = b[j];
			isError = 0;
			U8_APPEND(s, o, sizeof s, u, isError);
			if (isError) goto brk;
#if 1
			u3++;
#else
			if (u >= 0x3000) {
				u3++;
			}
#endif
		}
	brk:
		if (o >= sizeof s) return;
		s[o] = '\0';

		if (u3 == ndig) {

			if (n->pool) {
				p = n->pool;
				n->pool = (struct ngram_node_t *)p->next;
			}
			if (!(p = calloc(1, sizeof *p))) {
				break;
			}
			if (!(p->surface = strdup((char *)s))) {
				p->next = n->pool;
				n->pool = p;
				break;
			}
			p->next = n->result;
			n->result = p;
		}
	}
}
