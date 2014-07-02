/*	$Id: stmc.c,v 1.27 2010/01/15 23:37:08 nis Exp $	*/

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
static char rcsid[] = "$Id: stmc.c,v 1.27 2010/01/15 23:37:08 nis Exp $";
#endif

#include <config.h>

#include <sys/types.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <syslog.h>
#include <unistd.h>

#include <expat.h>

#include "util.h"
#include "ystem.h"
#include "nio.h"

#include <gifnoc.h>

/* #define DBG_CDATA_INJECTION	/* */

struct v {
	char const *ptr;
	size_t n;
};

struct udata {
	int status, line;
	int recording;
	char *ptr, *stemmer;
	size_t size, len;
	int rtype;

	ssize_t n, l;
	struct v *v;
};

#define	FAIL	0
#define	OK	1

#define	OFF	0
#define	ON	1

#define	JSON	0
#define	CDATA	1

static int process_request(int, char const *, size_t, struct ystems *, int);

static int pabuf(struct udata *, XML_Parser, char *, size_t, char const *);
static int pafile(XML_Parser, FILE *, size_t, char const *);

static void start(void *, const XML_Char *, const XML_Char **);
static void end(void *, const XML_Char *);
static void cdatahndl(void *, const XML_Char *, int);

static void cmnthndl(void *, const XML_Char *);
static int extehndl(XML_Parser, const XML_Char *, const XML_Char *, const XML_Char *, const XML_Char *);
static void prcihndl(void *, const XML_Char *, const XML_Char *);
static void dflthndl(void *, const XML_Char *, int);
#if defined XXDEBUG
static void fputdbgs(FILE *, char const *, char const *, int, int);
#else
#define fputdbgs(stream, tag, s, len, nl)
#endif

static size_t yputs(char const *, int, char *);
static size_t yputc(int, int, char *);
static size_t lfputs(char const *, char *);
static size_t cputs(char const *, char *);

static int procline(char const *, void *);
static int jsout(struct udata *, int);
static void vcount(void *, size_t);
static int vcompar(const void *, const void *);
static size_t uniq(void *, size_t, size_t, int (*)(const void *, const void *), void (*)(void *, size_t));
static int cdataout(struct udata *, int);

static int send_binary_content(int, char const *, size_t, int);
#if ! defined CRLFTEXT
#define	send_text_content	send_binary_content
#else
static int send_text_content(int, char const *, size_t, int);
#endif

int
stmc(yp)
struct ystems *yp;
{
	size_t clen;
	char const *request_method;
	char const *path_info;
	int in, out;
#if 0
#if defined TCP_NODELAY
	int one = 1;
#endif
#endif

	in = 0;		/* stdin */
	out = 1;	/* stdout */

#if defined CRLFTEXT
	_setmode(in, _O_BINARY);
	_setmode(out, _O_BINARY);
#endif

#if 0
#if defined TCP_NODELAY
	(void)setsockopt(0, IPPROTO_TCP, TCP_NODELAY, (char *)&one, sizeof one);
	(void)setsockopt(1, IPPROTO_TCP, TCP_NODELAY, (char *)&one, sizeof one);
#endif
#endif

	if (!(request_method = getenv("REQUEST_METHOD"))) {
		fprintf(stderr, "!REQUEST_METHOD\n");
		return 1;
	}

	if (!(path_info = getenv("PATH_INFO"))) {
		fprintf(stderr, "!PATH_INFO\n");
		return 1;
	}

	if (!strcmp(request_method, "POST")) {
		/* POST */
		char *content_length, *end;
		if (!(content_length = getenv("CONTENT_LENGTH"))) {
			fprintf(stderr, "!CONTENT_LENGTH\n");
			return 1;
		}
		clen = strtoul(content_length, &end, 10);
		if (!(*content_length && !*end)) {
			fprintf(stderr, "!invalid number: %s\n", content_length);
			return 1;
		}

		if (process_request(in, path_info, clen, yp, out) == -1) {
			return 1;
		}
		return 0;
	}
	fprintf(stderr, "!PERROR\n");
	return 1;
}

static int
process_request(in, path, clen, yp, out)
char const *path;
size_t clen;
struct ystems *yp;
{
	int e;
	struct udata u0, *u = &u0;
	XML_Parser p = NULL;
	int stemmer;
	char *opts;
	FILE *fin;

	if (!(fin = fdopen(in, "rb"))) {
		syslog(LOG_DEBUG, "fdopen: %s", strerror(errno));
		return -1;
	}

	u->recording = OFF;
	u->ptr = NULL;
	u->size = u->len = 0;
	u->stemmer = NULL;
	u->rtype = JSON;

	u->n = u->l = 0;
	u->v = NULL;

/*
	if (!(p = XML_ParserCreateNS("UTF-8", '\1'))) {
		fprintf(stderr, "XML_ParserCreate failed\n");
		goto error;
	}
*/
	if (!(p = XML_ParserCreate("UTF-8"))) {
		fprintf(stderr, "XML_ParserCreate failed\n");
		goto error;
	}

	XML_SetElementHandler(p, start, end);
	XML_SetCharacterDataHandler(p, cdatahndl);

	XML_SetCommentHandler(p, cmnthndl);
	XML_SetExternalEntityRefHandler(p, extehndl);
	XML_SetProcessingInstructionHandler(p, prcihndl);
	XML_SetDefaultHandler(p, dflthndl);

/*
XML_SetCdataSectionHandler
*XML_SetCharacterDataHandler
*XML_SetCommentHandler
+XML_SetDefaultHandler
XML_SetDefaultHandlerExpand
*XML_SetElementHandler
*XML_SetExternalEntityRefHandler
XML_SetNamespaceDeclHandler
XML_SetNotationDeclHandler
XML_SetNotStandaloneHandler
*XML_SetProcessingInstructionHandler
-XML_SetUnknownEncodingHandler
-XML_SetUnparsedEntityDeclHandler
*/

	XML_SetUserData(p, u);
	u->status = OK;
	u->line = 1;
	if (pafile(p, fin, clen, path) == -1) {
		goto error;
	}

	XML_ParserFree(p), p = NULL;

	if (u->status == FAIL || !u->ptr) {
		goto error;
	}
	if (!u->stemmer) {
		if (!(u->stemmer = strdup("auto"))) {
			goto error;
		}
	}

	if (!(stemmer = decode_stemmer_name(u->stemmer, &opts))) {
		syslog(LOG_DEBUG, "decode_stemmer_name failed: %s", u->stemmer);
	}

	if (ystem_init_stemmer(yp, stemmer, opts) == -1) {
		syslog(LOG_DEBUG, "ystem_init_stemmer failed");
		goto error;
	}

	if (ystem(yp, u->ptr, procline, u, stemmer, opts) == -1) {
		goto error;
	}

	switch (u->rtype) {
	case JSON:
		e = jsout(u, out);
		break;
	case CDATA:
		e = cdataout(u, out);
		break;
	default:
		e = -1;
		break;
	}
	return e;

error:
	if (p) XML_ParserFree(p);
	return -1;
}

static int
pabuf(u, p, b, size, path)
struct udata *u;
XML_Parser p;
char *b;
size_t size;
char const *path;
{
	size_t i, j;

	for (i = 0; i < size; i = j) {
		void *buf;
		int nl = 0;
		for (j = i; j < size && b[j] != '\n'; j++) ;
		if (j < size) {
			nl = 1;
			j++;
		}
		if (!(buf = XML_GetBuffer(p, j - i))) {
			char const *msg = XML_ErrorString(XML_GetErrorCode(p));
			fprintf(stderr, "XML_GetBuffer failed: %s at line %d\n", msg, u->line);
			return -1;
		}
		memmove(buf, b + i, j - i);
		if (!XML_ParseBuffer(p, j - i, 0)) {
			char const *msg = XML_ErrorString(XML_GetErrorCode(p));
			fprintf(stderr, "XML_ParseBuffer failed:%s: %s at line %d\n", path, msg, u->line);
/*				stacktrace(XML_GetUserData(p), stderr); */
			return -1;
		}
		if (u->status == FAIL) {
			return -1;
		}
		if (nl) {
			u->line++;
		}
	}
	return 0;
}

static int
pafile(p, f, clen, path)
XML_Parser p;
FILE *f;
size_t clen;
char const *path;
{
	ssize_t len;
	char b[8192];
	struct udata *u = XML_GetUserData(p);
	for (; clen; clen -= len) {
		size_t size = MIN(sizeof b, clen);
		if (!(len = fread(b, 1, size, f))) {
			break;
		}
		if (pabuf(u, p, b, len, path) == -1) {
			return -1;
		}
	}
	XML_Parse(p, NULL, 0, 1);

	return 0;
}

static void
start(data, el, attr)
void *data;
const XML_Char *el;
const XML_Char **attr;
{
	struct udata *u = data;
/*** ***/
	if (u->status == FAIL) {
		return;
	}
	if (!strcmp(el, "text")) {
		int i;
		if (u->recording) {
			u->status = FAIL;
			return;
		}
		u->recording = ON;
		for (i = 0; attr[i]; i += 2) {
			if (!strcmp(attr[i], "stemmer")) {
				u->stemmer = strdup(attr[i + 1]);
			}
			if (!strcmp(attr[i], "rtype")) {
				if (!strcmp(attr[i + 1], "cdata")) {
					u->rtype = CDATA;
				}
				else if (!strcmp(attr[i + 1], "json")) {
					u->rtype = JSON;
				}
			}
		}
	}
	return;
}

static void
end(data, el)
void *data;
const XML_Char *el;
{
	struct udata *u = data;
/*** ***/
	if (u->status == FAIL) {
		return;
	}
	if (!strcmp(el, "text")) {
		u->recording = OFF;
	}
}

static void
cdatahndl(data, s, len)
void *data;
const XML_Char *s;
int len;
{
	struct udata *u = data;
/*** ***/
	if (u->status == FAIL) {
		return;
	}
	if (u->recording == ON) {
		if (u->len + len >= u->size) {
			size_t newsize = u->len + len + 1024;
			void *new;
			if (!(new = realloc(u->ptr, newsize))) {
				perror("realloc");
				u->status = FAIL;
				return;
			}
			u->ptr = new;
			u->size = newsize;
		}
		memmove(u->ptr + u->len, s, len);
		u->len += len;
		u->ptr[u->len] = '\0';
	}
}

static void
cmnthndl(data, s)
void *data;
const XML_Char *s;
{
/* XXX */
	/*fputdbgs(stdout, "cmnthndl", s, strlen(s), 1);*/
}

static int
extehndl(parser, context, base, systemId, publicId)
XML_Parser parser;
const XML_Char *context, *base, *systemId, *publicId;
{
	/*fputdbgs(stdout, "extehndl", systemId, strlen(systemId), 1);*/
#if 0
	XML_Parser p;
	char path[MAXPATHLEN];
	FILE *f;
	struct stat sb;

	strlcpy(path, systemId, sizeof path);
	if (stat(path, &sb) == -1) {
		return 0;
	}

	if (!(p = XML_ExternalEntityParserCreate(parser, context, "UTF-8"))) {
		fprintf(stderr, "XML_ExternalEntityParserCreate failed\n");
		return 0;
	}
	if (!(f = fopen(path, "r"))) {
		return 0;
	}
	if (pafile(p, f, sb.st_size, path) == -1) {
		fclose(f);
		XML_ParserFree(p);
		return 0;
	}
	fclose(f);

	XML_ParserFree(p);
#endif
	return 1;
}

static void
prcihndl(data, target, s)
void *data;
const XML_Char *target, *s;
{
	/*fputdbgs(stdout, "prcihndl", s, strlen(s), 1);*/
}

static void
dflthndl(data, s, len)
void *data;
const XML_Char *s;
int len;
{
	/*fputdbgs(stdout, "dflthndl", s, len, 1);*/
}

#if ! defined fputdbgs
static void
fputdbgs(stream, tag, s, len, nl)
FILE *stream;
char const *tag, *s;
{
	size_t i;
	fputs(tag, stream);
	fputs(": ", stream);
	for (i = 0; i < len; i++) {
		fputc(s[i], stream);
	}
	if (nl) {
		fputc('\n', stream);
	}
}
#endif

static size_t
yputs(s, qc, out)
char const *s;
char *out;
{
	size_t l;
	for (l = 0; *s; s++) {
		l += yputc(*s & 0xff, qc, out ? out + l : NULL);
	}
	return l;
}

static size_t
yputc(c, qc, out)
char *out;
{
	switch (c) {
	case '<':
		return lfputs("&lt;", out);
	case '>':
		return lfputs("&gt;", out);
	case '&':
		return lfputs("&amp;", out);
	case '"':
		if (qc != '"') goto noescape;
		return lfputs("&quot;", out);
	case '\'':
		if (qc != '\'') goto noescape;
		return lfputs("&apos;", out);
	noescape:
	default:
		if (out) {
			*out = c;
		}
		return 1;
	}
}

static size_t
lfputs(s, out)
char const *s;
char *out;
{
	size_t l;
	l = strlen(s);
	if (out) {
		memcpy(out, s, l);
	}
	return l;
}

static size_t
cputs(s, out)
char const *s;
char *out;
{
	size_t l;
	for (l = 0; *s; s++) {
		if (out) {
			*out++ = *s;
		}
		l++;
		if (!strncmp("]]>", s, 3)) {
#define	CDATA_END_START	"]]><![CDATA["
			if (out) {
				memcpy(out, CDATA_END_START, sizeof CDATA_END_START - 1);
				out += sizeof CDATA_END_START - 1;
			}
			l += sizeof CDATA_END_START - 1;
		}
	}
	return l;
}

static int
procline(ln, u0)
char const *ln;
void *u0;
{
	struct udata *u = u0;

	if (u->n >= u->l) {
		size_t newsize = u->l + 32;
		void *new;
		if (!(new = realloc(u->v, sizeof *u->v * newsize))) {
			free(u->v);
			u->v = NULL;
			u->n = u->l = 0;
			return -1;
		}
		u->l = newsize;
		u->v = new;
	}

	if (!(u->v[u->n].ptr = strdup(ln))) {
		free(u->v);
		u->v = NULL;
		u->n = u->l = 0;
		return -1;
	}
	u->v[u->n].n = 0;
	u->n++;

	return 0;
}

#define	JSON_PROLOG	"<?xml version=\"1.0\"?>\n<vector>["
#define	JSON_TRAILER	"]</vector>\n"

static int
jsout(u, out)
struct udata *u;
{
	ssize_t l, o;
	size_t i;
	char ibuf[32], *s;

	qsort(u->v, u->n, sizeof *u->v, vcompar);

	u->n = uniq(u->v, u->n, sizeof *u->v, vcompar, vcount);

	l = 0;
	l += sizeof JSON_PROLOG - 1;
	for (i = 0; i < u->n; i++) {
		/* ["%s",%d] */
		l += 5;
		l += yputs(u->v[i].ptr, '"', NULL);
		snprintf(ibuf, sizeof ibuf, "%ld", (long)u->v[i].n);
		l += strlen(ibuf);
		if (i + 1 < u->n) l++;
	}
	l += sizeof JSON_TRAILER - 1;

	if (!(s = malloc(l))) {
		return -1;
	}

	o = 0;
	memcpy(s + o, JSON_PROLOG, sizeof JSON_PROLOG - 1), o += sizeof JSON_PROLOG - 1;
	for (i = 0; i < u->n; i++) {
		size_t ll;
		memcpy(s + o, "[\"", 2), o += 2;
		o += yputs(u->v[i].ptr, '"', s + o);
		memcpy(s + o, "\",", 2), o += 2;
		snprintf(ibuf, sizeof ibuf, "%ld", (long)u->v[i].n);
		ll = strlen(ibuf);
		memcpy(s + o, ibuf, ll), o += ll; 
		memcpy(s + o, "]", 1), o++;
		if (i + 1 < u->n) memcpy(s + o, ",", 1), o++;
	}
	memcpy(s + o, JSON_TRAILER, sizeof JSON_TRAILER - 1), o += sizeof JSON_TRAILER - 1;
	assert(l == o);
	send_text_content(out, s, l, 1);
	free(s);

	return 0;
}

static void
vcount(va, n)
void *va;
size_t n;
{
	struct v *a = va;
	a->n = n;
}

static int
vcompar(va, vb)
const void *va;
const void *vb;
{
	struct v const *a = va;
	struct v const *b = vb;

	return strcmp(a->ptr, b->ptr);
}

static size_t
uniq(base, nmemb, size, compar, count)
void *base;
size_t nmemb, size;
int (*compar)(const void *, const void *);
void (*count)(void *, size_t);
{
	size_t i, j, k;
#define	v(o)	(void *)((char *)base + (o) * size)
	for (i = k = 0; i < nmemb; i = j, k++) {
		for (j = i + 1; j < nmemb && !(*compar)(v(i), v(j)); j++) ;
		memmove(v(k), v(i), size);
		if (count) {
			count(v(k), j - i);
		}
	}
	return k;
#undef v
}

#define	CDATA_PROLOG	"<?xml version=\"1.0\"?>\n<words><![CDATA["
#define	CDATA_TRAILER	"]]></words>\n"

static int
cdataout(u, out)
struct udata *u;
{
	ssize_t l, o;
	size_t i;
	char *s;

/*	qsort(u->v, u->n, sizeof *u->v, vcompar);*/
#if defined DBG_CDATA_INJECTION
u->v[0].ptr = "xx]]>yy";
#endif

	l = 0;
	l += sizeof CDATA_PROLOG - 1;
	for (i = 0; i < u->n; i++) {
		/* %s\n */
		l += cputs(u->v[i].ptr, NULL);
		l++;
	}
	l += sizeof CDATA_TRAILER - 1;

	if (!(s = malloc(l))) {
		return -1;
	}

	o = 0;
	memcpy(s + o, CDATA_PROLOG, sizeof CDATA_PROLOG - 1), o += sizeof CDATA_PROLOG - 1;
	for (i = 0; i < u->n; i++) {
		o += cputs(u->v[i].ptr, s + o);
		memcpy(s + o, "\n", 1), o++;
	}
	memcpy(s + o, CDATA_TRAILER, sizeof CDATA_TRAILER - 1), o += sizeof CDATA_TRAILER - 1;
	assert(l == o);
	send_text_content(out, s, l, 1);
	free(s);

	return 0;
}

#define	PRIuSIZE_T	"ld"
typedef long pri_size_t;

static int
send_binary_content(out, ptr, size, hdrp)
char const *ptr;
size_t size;
{
	if (hdrp) {
		char buf[1024];	/** XXX **/
		snprintf(buf, sizeof buf, "Content-Length: %"PRIuSIZE_T"\r\n"
#if defined CRLFTEXT
			"Content-Transfer-Encoding: binary\r\n"
#endif
			"\r\n", (pri_size_t)size);
		nputs(buf, out);
	}
	writen(out, ptr, size);
	return 0;
}

#if defined CRLFTEXT
static int
send_text_content(out, ptr, size, hdrp)
char const *ptr;
size_t size;
{
	char *p, *q, *end;
	size_t l;
	end = ptr + size;
	if (hdrp) {
		char buf[1024];	/** XXX **/
		size = 0;
		for (p = ptr; p < end; p = q) {
			for (q = p; q < end && *q != '\r' && *q != '\n'; q++) ;
			l = q - p;
			size += l + 2;
			if (*q == '\r' && q + 1 < end && *(q + 1) == '\n') q++;
			if (q < end) q++;
		}
		snprintf(buf, sizeof buf, "Content-Length: %"PRIuSIZE_T"\r\n\r\n", (pri_size_t)size);
		nputs(buf, out);
	}
	for (p = ptr; p < end; p = q) {
		for (q = p; q < end && *q != '\r' && *q != '\n'; q++) ;
		l = q - p;
		if (l) writen(out, p, l);
		nputs("\r\n", out);
		if (*q == '\r' && q + 1 < end && *(q + 1) == '\n') q++;
		if (q < end) q++;
	}
	return 0;
}
#endif
