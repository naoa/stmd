/*	$Id: normalizer.c,v 1.8 2010/01/16 00:31:30 nis Exp $	*/

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
static char rcsid[] = "$Id: normalizer.c,v 1.8 2010/01/16 00:31:30 nis Exp $";
#endif

#include <config.h>

#include <sys/types.h>

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <unicode/utf.h>
#include <unicode/utf8.h>
#include <unicode/ucnv.h>
#include <unicode/utypes.h>
#include <unicode/uchar.h>
#include <unicode/usearch.h>

#include "util.h"
#include "normalizer.h"

#include <gifnoc.h>

static ssize_t utf8toutf16(uint8_t const *, UChar *, size_t);
static ssize_t utf16toutf8(UChar const *, size_t, uint8_t *, size_t);
static void dump_string(char const *, FILE *);

struct normalizer_t {
	size_t result_size, in_size, out_size;
	uint8_t *result;
	UChar *in, *out;
};

struct normalizer_t *
normalizer_init()
{
	struct normalizer_t *n;
	if (!(n = calloc(1, sizeof *n))) {
		return NULL;
	}
	n->result = NULL;
	n->in = NULL;
	n->result_size = n->in_size = 0;
	n->out_size = 8192;
	if (!(n->out = malloc(sizeof *n->out * n->out_size))) {
		free(n);
		return NULL;
	}
	return n;
}

void
normalizer_destroy(n)
struct normalizer_t *n;
{
	free(n);
}

#define	UTF8_NORMALIZE_OPTIONS	UNORM_UNICODE_3_2

char *
normalizer_sparse_tostr(n, s)
struct normalizer_t *n;
char const *s;
{
	UNormalizationMode mode = UNORM_NFKC;
	size_t length;
	UErrorCode status;
	ssize_t in_size, result_size;

	if ((in_size = utf8toutf16((uint8_t *)s, NULL, 0)) == -1) {
		fprintf(stderr, "utf8toutf16(N) failed\n");
dump_string(s, stderr);
		return NULL;
	}
	in_size++;
	if (n->in_size < in_size) {
		void *new;
		size_t newsize = in_size + 1024;
		if (!(new = realloc(n->in, sizeof *n->in * newsize))) {
			perror("realloc");
			return NULL;
		}
		n->in = new;
		n->in_size = newsize;
	}
	if (utf8toutf16((uint8_t *)s, n->in, n->in_size) == -1) {
		fprintf(stderr, "utf8toutf16 failed\n");
		return NULL;
	}
	status = U_ZERO_ERROR;
	length = unorm_normalize(n->in, -1, mode, UTF8_NORMALIZE_OPTIONS, NULL, 0, &status);
	length++;
	if (n->out_size < length) {
		void *new;
		size_t newsize = length + 1024;
		if (!(new = realloc(n->out, sizeof *n->out * newsize))) {
			perror("realloc");
			return NULL;
		}
		n->out = new;
		n->out_size = newsize;
	}
	status = U_ZERO_ERROR;
	length = unorm_normalize(n->in, -1, mode, UTF8_NORMALIZE_OPTIONS, n->out, n->out_size, &status);
	if (status != U_ZERO_ERROR) {
		fprintf(stderr, "unorm_normalize: %s\n", u_errorName(status));
		return NULL;
	}
	if ((result_size = utf16toutf8(n->out, length, NULL, 0)) == -1) {
		fprintf(stderr, "utf16toutf8(N) failed\n");
		return NULL;
	}
	result_size++;
	if (n->result_size < result_size) {
		void *new;
		size_t newsize = result_size + 1024;
		if (!(new = realloc(n->result, sizeof *n->result * newsize))) {
			perror("realloc");
			return NULL;
		}
		n->result = new;
		n->result_size = newsize;
	}
	if (utf16toutf8(n->out, length, n->result, n->result_size) == -1) {
		fprintf(stderr, "utf16toutf8 failed\n");
		return NULL;
	}
	return (char *)n->result;
}

static ssize_t
utf8toutf16(src, dst, size)
uint8_t const *src;
UChar *dst;
size_t size;
{
	int i, j;
	UChar tmp[2];
	size_t length = strlen((char *)src);
	for (i = j = 0; i < length; ) {
		UBool isError = 0;
		UChar32 u;
		U8_NEXT(src, i, length, u);
		if (u < 0) {
			fprintf(stderr, "!u8_next\n");
			return -1;
		}
		if (dst) {
			U16_APPEND(dst, j, size, u, isError);
		}
		else {
			int k = 0;
			U16_APPEND(tmp, k, nelems(tmp), u, isError);
			j += k;
		}
		if (isError) {
			fprintf(stderr, "error u16_append: %s\n", u_errorName(isError));
			return -1;
		}
	}
	if (dst) {
		if (j >= size) {
			fprintf(stderr, "!size\n");
			return -1;
		}
		dst[j] = 0;
	}
	return j;
}

static ssize_t
utf16toutf8(src, length, dst, size)
UChar const *src;
size_t length;
uint8_t *dst;
size_t size;
{
	int i, j;
	uint8_t tmp[8];
	for (i = j = 0; i < length; ) {
		UBool isError = 0;
		UChar32 u;
		U16_NEXT(src, i, length, u);
		if (dst) {
			U8_APPEND(dst, j, size, u, isError);
		}
		else {
			int k = 0;
			U8_APPEND(tmp, k, sizeof tmp, u, isError);
			j += k;
		}
		if (isError) {
			fprintf(stderr, "error u8_append: %s\n", u_errorName(isError));
			return -1;
		}
	}
	if (dst) {
		if (j >= size) {
			fprintf(stderr, "!size\n");
			return -1;
		}
		dst[j] = '\0';
	}
	return j;
}

static void
dump_string(s, stream)
char const *s;
FILE *stream;
{
	size_t i, j;

	for (i = 0; s[i]; i = j) {
		char buf[81];
		snprintf(buf, sizeof buf, "%07lo ", (long)i);
		for (j = i; j < i + 16 && s[j]; j++) {
			char tmp[16];
			unsigned char c = s[j] & 0xff;
			if (c == '\n') strcpy(tmp, "  \\n");
			else if (c == '\r') strcpy(tmp, "  \\r");
			else snprintf(tmp, sizeof tmp, (' '<= c && c <= '~') ? "   %c" : " %03o", c);
			assert(strlen(buf) + strlen(tmp) + 1 <= sizeof buf);
			strcat(buf, tmp);
		}
		assert(strlen(buf) + strlen("\n") + 1 <= sizeof buf);
		strcat(buf, "\n");
		fputs(buf, stream);
	}
}
