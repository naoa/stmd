/*	$Id: check.c,v 1.2 2009/07/30 00:09:30 nis Exp $	*/

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
static char rcsid[] = "$Id: check.c,v 1.2 2009/07/30 00:09:30 nis Exp $";
#endif

//#include <config.h>

#include <sys/types.h>
#include <sys/stat.h>

//#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//#include <strings.h>

int
main(argc, argv)
char *argv[];
{
	FILE *f, *g;
	char *path;
	char buf[1024];
	struct stat sb;
	char envs[128], *es;
	char cmd[1024];
	size_t l;

	if (argc != 2) {
		fprintf(stderr, "usage: %s path\n", argv[0]);
		return 1;
	}
	path = argv[1];

	if (stat(path, &sb) == -1) {
		perror(path);
		return 1;
	}

	sprintf(envs, "REQUEST_METHOD=POST");
	if (!(es = strdup(envs))) {
		perror("strdup");
		return 1;
	}
	if (putenv(es) != 0) {
		perror("putenv");
		return 1;
	}

	sprintf(envs, "PATH_INFO=/");
	if (!(es = strdup(envs))) {
		perror("strdup");
		return 1;
	}
	if (putenv(es) != 0) {
		perror("putenv");
		return 1;
	}

	sprintf(envs, "CONTENT_LENGTH=%ld", (long)sb.st_size);
	if (!(es = strdup(envs))) {
		perror("strdup");
		return 1;
	}
	if (putenv(es) != 0) {
		perror("putenv");
		return 1;
	}

	if (!(f = fopen(path, "rb"))) {
		perror(path);
		return 1;
	}
	sprintf(cmd, "stmd -c");
	if (!(g = _popen(cmd, "wb"))) {
		perror(cmd);
		return 1;
	}
	while (l = fread(buf, 1, sizeof buf, f)) {
		fwrite(buf, 1, l, g);
	}
	fclose(f);
	fclose(g);
	return 0;
}
