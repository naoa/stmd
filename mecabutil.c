/*	$Id: mecabutil.c,v 1.19 2010/11/05 06:51:20 nis Exp $	*/

/*-
 * Copyright (c) 2005 Shingo Nishioka.
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
static char rcsid[] = "$Id: mecabutil.c,v 1.19 2010/11/05 06:51:20 nis Exp $";
#endif

#include <config.h>

#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <mecab.h>

#include "util.h"
#include "jutil.h"
#include "mecabutil.h"

#include <gifnoc.h>

#define	PRIuSIZE_T	"ld"
typedef long pri_size_t;

static size_t m_split(char *, char **, size_t);
static char *m_hinfil(char * [], size_t, char *);
#if defined ENABLE_MECAB_OPTS
static int parse_opts_r(char *, char *[], size_t);
#endif /* ENABLE_MECAB_OPTS */

struct ze {
	char const *nam;
};

static struct ze z[] = {
	{"\345\220\215\350\251\236"}, /* Meishi */
	{"\345\213\225\350\251\236"}, /* Doushi */
	{"\345\275\242\345\256\271\350\251\236"}, /* Keiyoushi */
	{"\350\250\230\345\217\267"}, /* Kigou */
	{"\351\200\243\344\275\223\350\251\236"}, /* Rentaishi */
};

static struct ze mt = {"\346\234\252\347\237\245\350\252\236"}; /* Michigo */

struct mecab_t *
#if defined ENABLE_MECAB_OPTS
mecab_init(opts)
char *opts;
#else /* ENABLE_MECAB_OPTS */
mecab_init()
#endif /* ENABLE_MECAB_OPTS */
{
/*
 * NOTE: do not forget to update MECAB_DEFAULT_ARGC if you modify mecab_argv.
 */
#define	MECAB_DEFAULT_ARGC	3
	char *mecab_argv[32] = {"mecab", "--unk-feature", "\346\234\252\347\237\245\350\252\236", NULL}; /* Michigo */
	int mecab_argc = MECAB_DEFAULT_ARGC;
#if defined ENABLE_MECAB_OPTS
	if (opts) {
		if (parse_opts_r(opts, mecab_argv + MECAB_DEFAULT_ARGC, nelems(mecab_argv) - MECAB_DEFAULT_ARGC) == -1) {
#if defined DBG
			syslog(LOG_DEBUG, "parse_opts_r failed");
#endif
			return NULL;
		}
		for (; mecab_argv[mecab_argc]; mecab_argc++) ;
	}
#endif /* ENABLE_MECAB_OPTS */
#if 0
	{
		int i;
		for (i = 0; i < mecab_argc; i++) {
			fprintf(stderr, "%d: %s\n", i, mecab_argv[i]);
		}
		fprintf(stderr, "%d: %p\n", i, mecab_argv[i]);
	}
#endif
	return mecab_new(mecab_argc, mecab_argv);
}

static size_t
m_split(p, f, size)
char *p;
char **f;
size_t size;
{
	size_t nf;
	char *q;
	for (nf = 0, q = p; ; p++) {
		if (*p == ',' || *p == '\0') {
			if (nf >= size) {
				break;
			}
			f[nf++] = q;
			if (*p == ',') {
				*p = '\0';
				q = p + 1;
			}
			else {
				*p = '\0';
				break;
			}
		}
	}
	return nf;
}

static char *
m_hinfil(f, nf, s)
char *f[];
size_t nf;
char *s;
{
	int i;
	if (nf > MECABKIHONINDEX) {
		char *hinsi = f[0];
		for (i = 0; i < nelems(z); i++) {
			if (!strcmp(hinsi, z[i].nam)) {
				return f[MECABKIHONINDEX];
			}
		}
	}
	else if (nf >= 1 && !strcmp(f[0], mt.nam)) {
		return s;
	}
	return NULL;
}

char *
mecab_linefil(n, sup, su_sizep, fep, fe_sizep)
const mecab_node_t *n;
char **sup, **fep;
size_t *su_sizep, *fe_sizep;
{
	size_t nf;
	char *f[32], *w;
	if (n->length >= *su_sizep) {
		size_t newsize = n->length + 32;
		void *new;
		if (!(new = realloc(*sup, newsize))) {
			return NULL;
		}
		*sup = new;
		*su_sizep = newsize;
	}
	memmove(*sup, n->surface, n->length);
	(*sup)[n->length] = '\0';
	if (strlen(n->feature) >= *fe_sizep) {
		size_t newsize = strlen(n->feature) + 32;
		void *new;
		if (!(new = realloc(*fep, newsize))) {
			return NULL;
		}
		*fep = new;
		*fe_sizep = newsize;
	}
	strcpy(*fep, n->feature);

	nf = m_split(*fep, f, nelems(f));

	if ((w = m_hinfil(f, nf, *sup)) && isasoundword(w)) {
		return w;
	}
	return NULL;
}

#if defined ENABLE_MECAB_OPTS
/*
 * NOTE: this parser is static version.
 *       The automatic allocation version (parse_opts) is in exts.c
 */
static int
parse_opts_r(opts, argv, size)
char *argv[], *opts;
size_t size;
{
	char *p;
	size_t i;

	for (i = 0, p = opts; p && *p; i++) {
		if (i >= size) {
#if defined DBG
			syslog(LOG_DEBUG, "parse_opts_r: too many args");
#endif
			return -1;
		}
		argv[i] = p;
		if (p = index(p, ',')) {
			*p++ = '\0';
		}
#if defined DBG
		syslog(LOG_DEBUG, "parse_opts_r: argv[%"PRIuSIZE_T"]: \"%s\"", (pri_size_t)i, argv[i]);
#endif
	}
	if (i >= size) {
#if defined DBG
		syslog(LOG_DEBUG, "parse_opts_r: too many args");
#endif
		return -1;
	}
	argv[i] = NULL;
#if defined DBG
	syslog(LOG_DEBUG, "parse_opts_r: argv[%"PRIuSIZE_T"]: NULL", (pri_size_t)i);
#endif

	return 0;
}
#endif /* ENABLE_MECAB_OPTS */
