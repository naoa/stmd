/*	$Id: postagutil.c,v 1.12 2009/08/10 23:32:51 nis Exp $	*/

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
static char rcsid[] = "$Id: postagutil.c,v 1.12 2009/08/10 23:32:51 nis Exp $";
#endif

#include <config.h>

#include <sys/types.h>

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"
#include "postag.h"

#include "postagutil.h"

#include <gifnoc.h>

struct postag_t {
	int c;
};

static int issound(char const *);

static char *good_poss[] = {
	"CD",
	"JJ", "JJR", "JJS",
	"NN", "NNS", "NNP", "NNPS",
	"RB", "RBR", "RBS",
	"UH",
	"VB", "VBD", "VBG", "VBN", "VBP", "VBZ",
};

static char *stopwords[] = {
	"be", "am", "are", "is", "was", "were", "being", "been",
	"do", "does", "did", "done",
	"have", "has", "had",
	"more", "most",
	"much", "many",
};

static int scompar(const void *, const void *);

struct postag_t *
postag_init()
{
	static struct postag_t p;
	qsort(good_poss, nelems(good_poss), sizeof *good_poss, scompar);
	qsort(stopwords, nelems(stopwords), sizeof *stopwords, scompar);
	postag_init("/usr/local/share/postagger");
	if (postag_stem_init() == -1) {
		return NULL;
	}
	return &p;
}

void
postag_destroy(p)
struct postag_t *p;
{
}

char *
postagstem_linefil(q)
char *q;
{
	size_t nf;
	char *f[32], *pos, *w;

	if ((nf = chasplit(q, f, nelems(f))) != 3) {
/* syslog(LOG_DEBUG, "!chasplit: [%s]", q); */
		return NULL;
	}
	pos = f[2];

	if (!bsearch(&pos, good_poss, nelems(good_poss), sizeof pos, scompar)) {
/* syslog(LOG_DEBUG, "!pos: %s", pos); */
		return NULL;
	}

	w = f[1];

	if (!issound(w)) {
/* syslog(LOG_DEBUG, "!sound: %s", w); */
		return NULL;
	}

	if (bsearch(&w, stopwords, nelems(stopwords), sizeof w, scompar)) {
/* syslog(LOG_DEBUG, "stopword: %s", w); */
		return NULL;
	}

	return w;
}

static int
issound(w)
char const *w;
{
	if (isalnum(*w) && *(w + 1) == '\0') {
		return 0;
	}
	for (; *w; w++) {
		if (!isalnum(*w & 0xff) && *w != ' ' && *w != '-') {
			return 0;
		}
	}
	return 1;
}

static int
scompar(va, vb)
const void *va;
const void *vb;
{
	char * const *a = va;
	char * const *b = vb;
	return strcmp(*a, *b);
}
