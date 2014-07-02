/*	$Id: ystem.c,v 1.39 2010/11/05 06:51:20 nis Exp $	*/

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
static char rcsid[] = "$Id: ystem.c,v 1.39 2010/11/05 06:51:20 nis Exp $";
#endif

#include <config.h>

#include <sys/param.h>
#include <sys/uio.h>

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <syslog.h>
#include <unistd.h>

#include <unicode/utf.h>
#include <unicode/utf8.h>

#include "util.h"
#include "jutil.h"

#include "normalizer.h"
#include "ngram.h"

#if defined USE_CHASEN
#include <chasen.h>
#include "chasenutil.h"
#endif

#if defined USE_SNOWBALL
#include <libstemmer.h>
#include "snowballutil.h"
#endif

#if defined USE_MECAB
#include <mecab.h>
#include "mecabutil.h"
#endif

#if defined USE_NSTMS
#include <nstms.h>
#include "nstmsutil.h"
#endif

#if defined USE_EXTS
#include "extsutil.h"
#endif

#if defined USE_POSTAG
#include "postag.h"
#include "postagutil.h"
#include "postagstem.h"
#endif

#include "ystem.h"

#include <gifnoc.h>

struct ystems {
	struct {
		struct normalizer_t *s;
	} normalizer;
	struct {
		struct ngram_t *s;
	} ngram;
#if defined USE_CHASEN
	struct {
		struct chasen_t *s;
	} chasen;
#endif
#if defined USE_SNOWBALL
	struct {
		struct sb_stemmer *s;
	} snowball;
#endif
#if defined USE_MECAB
	struct {
		struct mecab_t *s;
		char *su, *fe;
		size_t su_size, fe_size;
	} mecab;
#endif
#if defined USE_NSTMS
	struct {
		struct nstms *s;
	} nstms;
#endif
#if defined USE_EXTS
	struct {
		struct exts_t *s;
	} exts;
#endif
#if defined USE_POSTAG
	struct {
		struct postag_t *s;
	} postag;
#endif
};

struct x {
	int (*pl)(char const *, void *);
	void *u;
};

static int normalizer(struct ystems *, char const *, int (*)(char const *, void *), void *);
static int ngram(struct ystems *, char const *, int (*)(char const *, void *), void *);
#if defined USE_CHASEN
static int chasen(struct ystems *, char *, int (*)(char const *, void *), void *);
static int proc_chasenline(char *, void *);
#endif
#if defined USE_SNOWBALL
static int snowball(struct ystems *, char *, int (*)(char const *, void *), void *);
static int proc_snowballline(char *, void *);
#endif
#if defined USE_MECAB
static int mecab(struct ystems *, char const *, int (*)(char const *, void *), void *);
#endif
#if defined USE_NSTMS
static int nstms(struct ystems *, char const *, int (*)(char const *, void *), void *);
static int proc_nstmsline(char *, void *);
#endif
#if defined USE_EXTS
static int exts(struct ystems *, char const *, int (*)(char const *, void *), void *);
#endif
#if defined USE_POSTAG
static int postag(struct ystems *, char const *, int (*)(char const *, void *), void *);
static int proc_postagline(char *, void *);
#endif

#if defined KILL3NUMBER
static int is3num(char const *);
#endif

struct ystems *
ystem_create()
{
	struct ystems *yp;
	if (!(yp = calloc(1, sizeof *yp))) {
		return NULL;
	}
	yp->normalizer.s = NULL;
	yp->ngram.s = NULL;
#if defined USE_CHASEN
	yp->chasen.s = NULL;
#endif
#if defined USE_SNOWBALL
	yp->snowball.s = NULL;
#endif
#if defined USE_MECAB
	yp->mecab.s = NULL;
#endif
#if defined USE_NSTMS
	yp->nstms.s = NULL;
#endif
#if defined USE_EXTS
	yp->exts.s = NULL;
#endif
#if defined USE_POSTAG
	yp->postag.s = NULL;
#endif
	return yp;
}

void
ystem_destroy(yp)
struct ystems *yp;
{
	if (yp->normalizer.s) {
		normalizer_destroy(yp->normalizer.s);
	}
	if (yp->ngram.s) {
		ngram_destroy(yp->ngram.s);
	}
#if defined USE_CHASEN
	if (yp->chasen.s) {
		chasen_destroy(yp->chasen.s);
	}
#endif
#if defined USE_SNOWBALL
	if (yp->snowball.s) {
		/* snowball_destroy(yp->snowball.s); */
	}
#endif
#if defined USE_MECAB
	if (yp->mecab.s) {
		mecab_destroy(yp->mecab.s);
	}
#endif
#if defined USE_NSTMS
	if (yp->nstms.s) {
		nstms_close(yp->nstms.s);
	}
#endif
#if defined USE_EXTS
	if (yp->exts.s) {
		exts_close(yp->exts.s);
	}
#endif
#if defined USE_POSTAG
	if (yp->postag.s) {
		postag_destroy(yp->postag.s);
	}
#endif
	free(yp);
}

int
ystem_init_stemmer(yp, stemmer, opts)
struct ystems *yp;
char *opts;
{
	switch (stemmer) {
	case NORMALIZER:
		if (!yp->normalizer.s && !(yp->normalizer.s = normalizer_init())) {
			syslog(LOG_DEBUG, "normalizer_init failed");
			return -1;
		}
		break;
	case NGRAM:
		if (!yp->ngram.s && !(yp->ngram.s = ngram_init(opts))) {
			syslog(LOG_DEBUG, "ngram_init failed");
			return -1;
		}
		break;
#if defined USE_CHASEN
	case CHASEN:
		if (!yp->chasen.s && !(yp->chasen.s = chasen_init())) {
			syslog(LOG_DEBUG, "chasen_init failed");
			return -1;
		}
		break;
#endif
#if defined USE_SNOWBALL
	case SNOWBALL:
		if (!yp->snowball.s && !(yp->snowball.s = snowball_init())) {
			syslog(LOG_DEBUG, "snowball_init failed");
			return -1;
		}
		break;
#endif
#if defined USE_MECAB
	case MECAB:
		if (!yp->mecab.s && !(yp->mecab.s = mecab_init(
#if defined ENABLE_MECAB_OPTS
			opts
#endif /* ENABLE_MECAB_OPTS */
			))) {
			syslog(LOG_DEBUG, "mecab_init failed");
			return -1;
		}
		yp->mecab.su = yp->mecab.fe = NULL;
		yp->mecab.su_size = yp->mecab.fe_size = 0;
		break;
#endif
#if defined USE_NSTMS
	case NSTMS:
		if (!yp->nstms.s && !(yp->nstms.s = nstms_init())) {
			syslog(LOG_DEBUG, "nstms_init failed");
			return -1;
		}
		break;
#endif
#if defined USE_EXTS
	case EXTS:
		if (!yp->exts.s && !(yp->exts.s = exts_init(opts))) {
			syslog(LOG_DEBUG, "exts_init failed");
			return -1;
		}
		break;
#endif
#if defined USE_POSTAG
	case POSTAG:
		if (!yp->postag.s && !(yp->postag.s = postag_init())) {
			syslog(LOG_DEBUG, "postag_init failed");
			return -1;
		}
		break;
#endif
	default:
		syslog(LOG_DEBUG, "unknown stemmer: %d", stemmer);
		return -1;
	}
	return 0;
}

int
decode_stemmer_name(stemmer_name, opts)
char *stemmer_name, **opts;
{
	size_t i;
	char *p;
	struct stmnam {
		char *name;
		int val;
	};
	struct stmnam stemmers[] = {
		{"auto", DEFAULT_STEMMER},
		{"default", DEFAULT_STEMMER},
#if defined USE_CHASEN
		{"chasen", CHASEN},
#endif
#if defined USE_MECAB
		{"mecab", MECAB},
#endif
		{"ngram", NGRAM},
		{"normalizer", NORMALIZER},
#if defined USE_NSTMS
		{"nstms", NSTMS},
#endif
#if defined USE_EXTS
		{"exts", EXTS},
#endif
#if defined USE_POSTAG
		{"postag", POSTAG},
#endif
#if defined USE_SNOWBALL
		{"snowball", SNOWBALL},
#endif
	};
	if (p = index(stemmer_name, ',')) {
		*p++ = '\0';
		*opts = p;
	}
	else {
		*opts = NULL;
	}
	for (i = 0; i < nelems(stemmers); i++) {
		struct stmnam *e = &stemmers[i];
		if (!strcmp(stemmer_name, e->name)) {
			return e->val;
		}
	}
	return 0;
}

int
ystem(yp, s, pl, u, stemmer, opts)
struct ystems *yp;
char const *s;
int (*pl)(char const *, void *);
void *u;
char *opts;
{
	char *t;
	if (!yp->normalizer.s && !(yp->normalizer.s = normalizer_init())) {
		return -1;
	}
	if (stemmer == NORMALIZER) {
		if (normalizer(yp, s, pl, u) == -1) {
			return -1;
		}
		return 0;
	}
	if (!(t = normalizer_sparse_tostr(yp->normalizer.s, s))) {
		return -1;
	}
#if defined DBG
syslog(LOG_DEBUG, "ystem: stemmer = %d, opts = %s", stemmer, opts ? opts : "(null)");
#endif
	switch (stemmer) {
/*
	case NORMALIZER: break;
*/
	case NGRAM:
		if (!yp->ngram.s && ystem_init_stemmer(yp, stemmer, opts)) {
#if defined DBG
syslog(LOG_DEBUG, "ystem_init_stemmer ngram failed");
#endif
			return -1;
		}
		if (ngram(yp, t, pl, u) == -1) {
#if defined DBG
syslog(LOG_DEBUG, "ngram failed");
#endif
			return -1;
		}
		break;
#if defined USE_CHASEN
	case CHASEN:
#if defined DBG
syslog(LOG_DEBUG, "C0>%s", t);
#endif
		if (!yp->chasen.s && ystem_init_stemmer(yp, stemmer, opts)) {
#if defined DBG
syslog(LOG_DEBUG, "ystem_init_stemmer chasen failed");
#endif
			return -1;
		}
		/* fs2hs(t); */
		if (chasen(yp, t, pl, u) == -1) {
#if defined DBG
syslog(LOG_DEBUG, "chasen failed");
#endif
			return -1;
		}
		break;
#endif
#if defined USE_SNOWBALL
	case SNOWBALL:
		if (!yp->snowball.s && ystem_init_stemmer(yp, stemmer, opts)) {
#if defined DBG
syslog(LOG_DEBUG, "ystem_init_stemmer snowball failed");
#endif
			return -1;
		}
		if (snowball(yp, t, pl, u) == -1) {
#if defined DBG
syslog(LOG_DEBUG, "snowball failed");
#endif
			return -1;
		}
		break;
#endif
#if defined USE_MECAB
	case MECAB:
		if (!yp->mecab.s && ystem_init_stemmer(yp, stemmer, opts)) {
#if defined DBG
syslog(LOG_DEBUG, "ystem_init_stemmer mecab failed");
#endif
			return -1;
		}
		if (mecab(yp, t, pl, u) == -1) {
#if defined DBG
syslog(LOG_DEBUG, "mecab failed");
#endif
			return -1;
		}
		break;
#endif
#if defined USE_NSTMS
	case NSTMS:
		if (!yp->nstms.s && ystem_init_stemmer(yp, stemmer, opts)) {
#if defined DBG
syslog(LOG_DEBUG, "ystem_init_stemmer nstms failed");
#endif
			return -1;
		}
		if (nstms(yp, t, pl, u) == -1) {
#if defined DBG
syslog(LOG_DEBUG, "nstms failed");
#endif
			return -1;
		}
		break;
#endif
#if defined USE_EXTS
	case EXTS:
		if (!yp->exts.s && ystem_init_stemmer(yp, stemmer, opts)) {
#if defined DBG
syslog(LOG_DEBUG, "ystem_init_stemmer exts failed");
#endif
			return -1;
		}
		if (exts(yp, t, pl, u) == -1) {
#if defined DBG
syslog(LOG_DEBUG, "exts failed");
#endif
			return -1;
		}
		break;
#endif
#if defined USE_POSTAG
	case POSTAG:
#if defined DBG
syslog(LOG_DEBUG, "P0>%s", t);
#endif
		if (!yp->postag.s && ystem_init_stemmer(yp, stemmer, opts)) {
#if defined DBG
syslog(LOG_DEBUG, "ystem_init_stemmer postag failed");
#endif
			return -1;
		}
		if (postag(yp, t, pl, u) == -1) {
#if defined DBG
syslog(LOG_DEBUG, "postag failed");
#endif
			return -1;
		}
		break;
#endif
	default:
#if defined DBG
syslog(LOG_DEBUG, "ystem: internal error");
#endif
		return -1;
	}
	return 0;
}

static int
normalizer(yp, s, pl, u)
struct ystems *yp;
char const *s;
int (*pl)(char const *, void *);
void *u;
{
	char const *p;
	if (!(p = normalizer_sparse_tostr(yp->normalizer.s, s))) {
		return -1;
	}
	if ((*pl)(p, u) == -1) {
		return -1;
	}
	return 0;
}

static int
ngram(yp, s, pl, u)
struct ystems *yp;
char const *s;
int (*pl)(char const *, void *);
void *u;
{
	struct ngram_node_t const *p;
	p = ngram_sparse_tonode(yp->ngram.s, s);
	for (; p; p = p->next) {
		if ((*pl)(p->surface, u) == -1) {
#if defined DBG
syslog(LOG_DEBUG, "ngram pl failed");
#endif
			return -1;
		}
	}
	return 0;
}

#if defined USE_CHASEN
static int
chasen(yp, s, pl, u)
struct ystems *yp;
char *s;
int (*pl)(char const *, void *);
void *u;
{
	char *p;
	struct x x0, *x = &x0;
#define BYPASS_GK 1
#if defined BYPASS_GK
	char gkword[1024];
	size_t gkwlen;
	uint8_t *t;
	UChar32 c;
	int i;
	size_t length;
#define	ISGK(c)	((0x0370 <= (c) && (c) < 0x0530) || \
		 (0x1E00 <= (c) && (c) < 0x2000))
#endif

	x->pl = pl;
	x->u = u;
#if defined BYPASS_GK
#define	GWEJECT() do { \
		if (gkwlen >= sizeof gkword) return -1; \
		gkword[gkwlen] = '\0'; \
		if ((*pl)(gkword, u) == -1) { \
			return -1; \
		} \
		gkwlen = 0; \
	} while (0)
	/* fs2hs(s); */
	gkwlen = 0;
	length = strlen(s);
	t = (uint8_t *)s;
	for (i = 0; i < length; ) {
		size_t l;
		int i0 = i;
		U8_NEXT(t, i, length, c);
		if (c < 0) return -1;
		l = i - i0;
		if (ISGK(c)) {
			if (gkwlen + l + 1 < sizeof gkword) {
				memmove(gkword + gkwlen, t + i0, l);
				gkwlen += l;
			}
			memset(t + i0, ' ' , l);
		}
		else if (gkwlen > 0) {
			GWEJECT();
		}
	}
	if (gkwlen > 0) {
		GWEJECT();
	}
#endif
	p = chasen_sparse_tostr(s);
	if (mapline(p, proc_chasenline, x) == -1) {
		return -1;
	}
	return 0;
}

static int
proc_chasenline(ln, x0)
char *ln;
void *x0;
{
	struct x *x = x0;
	char *w;
	if (!(w = chasen_linefil(ln))) {
		return 0;
	}
#if defined KILL3NUMBER
	if (is3num(w)) {
		return 0;
	}
#endif
	return (*x->pl)(w, x->u);
}
#endif

#if defined USE_SNOWBALL
static int
snowball(yp, s, pl, u)
struct ystems *yp;
char *s;
int (*pl)(char const *, void *);
void *u;
{
	char *p;
	struct x x0, *x = &x0;
	x->pl = pl;
	x->u = u;
	p = snowball_sparse_tostr(yp->snowball.s, s);
	if (mapline(p, proc_snowballline, x) == -1) {
		return -1;
	}
	free(p);
	return 0;
}

static int
proc_snowballline(ln, x0)
char *ln;
void *x0;
{
	struct x *x = x0;
	char *w;
	if (!(w = snowball_linefil(ln))) {
		return 0;
	}
#if defined KILL3NUMBER
	if (is3num(w)) {
		return 0;
	}
#endif
	return (*x->pl)(w, x->u);
}
#endif

#if defined USE_MECAB
static int
mecab(yp, s, pl, u)
struct ystems *yp;
char const *s;
int (*pl)(char const *, void *);
void *u;
{
	const mecab_node_t *p;
	char const *w;
	p = mecab_sparse_tonode(yp->mecab.s, s);
	for (; p; p = p->next) {
		if (!(w = mecab_linefil(p, &yp->mecab.su, &yp->mecab.su_size, &yp->mecab.fe, &yp->mecab.fe_size))) {
			continue;
		}
#if defined KILL3NUMBER
		if (is3num(w)) {
			continue;
		}
#endif
		if ((*pl)(w, u) == -1) {
			return -1;
		}
	}
	return 0;
}
#endif

#if defined USE_NSTMS
static int
nstms(yp, s, pl, u)
struct ystems *yp;
char const *s;
int (*pl)(char const *, void *);
void *u;
{
	char *p;
	struct x x0, *x = &x0;
	x->pl = pl;
	x->u = u;
	p = nstms_sparse_tostr(yp->nstms.s, s);
	if (mapline(p, proc_nstmsline, x) == -1) {
		return -1;
	}
	return 0;
}

static int
proc_nstmsline(ln, x0)
char *ln;
void *x0;
{
	struct x *x = x0;
	char *w;
	if (!(w = nstms_linefil(ln))) {
		return 0;
	}
#if defined KILL3NUMBER
	if (is3num(w)) {
		return 0;
	}
#endif
	return (*x->pl)(w, x->u);
}
#endif

#if defined USE_EXTS
static int
exts(yp, s, pl, u)
struct ystems *yp;
char const *s;
int (*pl)(char const *, void *);
void *u;
{
	struct exts_node_t *p;
	char const *w;
#if defined DBG
syslog(LOG_DEBUG, "start exts");
#endif
	p = exts_sparse_tonode(yp->exts.s, s);
#if defined DBG
syslog(LOG_DEBUG, "exts_sparse_tonode done");
#endif
	for (; p; p = p->next) {
		if (!(w = exts_linefil(p))) {
			continue;
		}
#if defined DBG
syslog(LOG_DEBUG, "exts: w = %s", w);
#endif
#if defined KILL3NUMBER
		if (is3num(w)) {
			continue;
		}
#endif
		if ((*pl)(w, u) == -1) {
			return -1;
		}
	}
#if defined DBG
syslog(LOG_DEBUG, "end exts");
#endif
	return 0;
}
#endif

#if defined USE_POSTAG
static int
postag(yp, s, pl, u)
struct ystems *yp;
char const *s;
int (*pl)(char const *, void *);
void *u;
{
	char *p;
	struct x x0, *x = &x0;
	x->pl = pl;
	x->u = u;
	p = postag_sparse_tostr(s);
	if (mapline(p, proc_postagline, x) == -1) {
		return -1;
	}
	free(p);
	return 0;
}

static int
proc_postagline(ln, x0)
char *ln;
void *x0;
{
	struct x *x = x0;
	char *w;
	if (!(w = postagstem_linefil(ln))) {
		return 0;
	}
#if defined KILL3NUMBER
	if (is3num(w)) {
		return 0;
	}
#endif
	return (*x->pl)(w, x->u);
}
#endif

#if defined KILL3NUMBER
static int
is3num(w)
char const *w;
{
	char const *p;
	for (p = w; *p; p++) {
		if (!isdigit(*p & 0xff)) {
			return 0;
		}
	}
	return w < p && p - w <= 3;
}
#endif
