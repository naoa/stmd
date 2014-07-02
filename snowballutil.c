/*	$Id: snowballutil.c,v 1.9 2009/07/30 00:09:29 nis Exp $	*/

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
static char rcsid[] = "$Id: snowballutil.c,v 1.9 2009/07/30 00:09:29 nis Exp $";
#endif

#include <config.h>

#include <sys/types.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <syslog.h>

#include "libstemmer.h"
#include "snowballutil.h"

#include <gifnoc.h>

struct obuf {
	char *ptr;
	size_t size, len;
};

static int eject_word(struct obuf *, char *, struct sb_stemmer *);
static int append(struct obuf *, char const *);

char *
snowball_sparse_tostr(stemmer, s)
struct sb_stemmer *stemmer;
char *s;
{
	char *p, *q;
	struct obuf o0, *o = &o0;

	bzero(o, sizeof *o);
	o->ptr = NULL;

	for (p = s; *p; p++) {
		int c = *p & 0xff;
		if (c & 0x80 || !isalnum(c)) {
			*p = ' ';
		}
		else if (isupper(c)) {
			*p = tolower(c);
		}
	}

	for (p = s; p && *p; p = q) {
		q = index(p, ' ');
		if (q) {
			*q++ = '\0';
		}
		if (*p && eject_word(o, p, stemmer) == -1) {
			return NULL;
		}
	}
	return o->ptr;
}

static int
eject_word(o, segm, stemmer)
struct obuf *o;
char *segm;
struct sb_stemmer *stemmer;
{
	sb_symbol *b = (sb_symbol *)segm;
	const sb_symbol *stemmed;

	stemmed = sb_stemmer_stem(stemmer, b, strlen((char *)b));
	if (stemmed == NULL) {
		syslog(LOG_DEBUG, "Out of memory");
		return -1;
	}
	if (append(o, (char *)stemmed) == -1) {
		return -1;
	}
	if (append(o, "\n") == -1) {
		return -1;
	}
	return 0;
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

struct sb_stemmer *
snowball_init()
{
	struct sb_stemmer *stemmer;
	char *language = "english";
	char *charenc = NULL;

	/* do the stemming process: */
	if (!(stemmer = sb_stemmer_new(language, charenc))) {
		if (!charenc) {
			syslog(LOG_DEBUG, "language `%s' not available for stemming", language);
			return NULL;
		}
		else {
			syslog(LOG_DEBUG, "language `%s' not available for stemming in encoding `%s'", language, charenc);
			return NULL;
		}
	}
	return stemmer;
}

void
snowball_delete(stemmer)
struct sb_stemmer *stemmer;
{
	sb_stemmer_delete(stemmer);
}

char *
snowball_linefil(q)
char *q;
{
	return q;
}
