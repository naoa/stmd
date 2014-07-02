/*	$Id: chasenutil.c,v 1.13 2009/08/10 23:32:51 nis Exp $	*/

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
static char rcsid[] = "$Id: chasenutil.c,v 1.13 2009/08/10 23:32:51 nis Exp $";
#endif

#include <sys/types.h>

#include <config.h>

#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>

#include <chasen.h>

#include "util.h"
#include "jutil.h"
#include "chasenutil.h"

#include <gifnoc.h>

struct chasen_t {
	int c;
};

static size_t c_split(char *, char **, size_t);
static char *c_hinfil(char *[], size_t);

struct ze {
	char const *nam;
	int len;
};

static struct ze z[] = {
	{"\345\220\215\350\251\236", 0},	/* Meishi */
	{"\345\213\225\350\251\236", 0},	/* Doushi */
	{"\345\275\242\345\256\271\350\251\236", 0},	/* Keiyoushi */
	{"\350\250\230\345\217\267", 0},	/* Kigou */
	{"\351\200\243\344\275\223\350\251\236", 0},	/* Rentaishi */
};

static struct ze mt = {"\346\234\252\347\237\245\350\252\236", 0};	/* Michigo */

struct chasen_t *
chasen_init()
{
	int i;
	char *option[] = {"chasen", "-i", "w", NULL};
	static struct chasen_t c;

	chasen_getopt_argv(option, stderr);
	for (i = 0; i < nelems(z); i++) {
		z[i].len = strlen(z[i].nam);
	}
	mt.len = strlen(mt.nam);
	return &c;
}

void
chasen_destroy(c)
struct chasen_t *c;
{
}

static size_t
c_split(p, f, size)
char *p;
char **f;
size_t size;
{
	size_t nf;
	char *q;
	for (nf = 0, q = p; ; p++) {
		if (*p == '\n' || *p == '\t' || *p == '\0') {
			if (nf >= size) {
				break;
			}
			f[nf++] = q;
			if (*p == '\t') {
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
c_hinfil(f, nf)
char *f[];
size_t nf;
{
	int i;
	if (nf >= 4) {
		char *hinsi = f[3];
		for (i = 0; i < nelems(z); i++) {
			if (!strncmp(hinsi, z[i].nam, z[i].len)) {
				return f[2];
			}
		}
		if (!strncmp(hinsi, mt.nam, mt.len)) {
			return f[0];
		}
	}
	return NULL;
}

char *
chasen_linefil(q)
char *q;
{
	size_t nf;
	char *f[32], *w;

	nf = c_split(q, f, nelems(f));

	if ((w = c_hinfil(f, nf)) && isasoundword(w)) {
		return w;
	}
	return NULL;
}
