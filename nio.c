/*	$Id: nio.c,v 1.12 2009/08/10 23:36:24 nis Exp $	*/

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
static char rcsid[] = "$Id: nio.c,v 1.12 2009/08/10 23:36:24 nis Exp $";
#endif

#include <config.h>

#include <sys/param.h>
#include <sys/uio.h>
#include <sys/socket.h>

#include <assert.h>
#include <limits.h>
#include <unistd.h>

#include "nio.h"

#include <gifnoc.h>

#if ! defined IOV_MAX && defined UIO_MAXIOV
#define IOV_MAX UIO_MAXIOV
#endif

#if ! defined IOV_MAX
#define IOV_MAX 1024
#endif

#if ! defined MIN
#define	MIN(a, b)	((a) < (b) ? (a) : (b))
#endif

/*
readvn() perform the same functions as readv,
but read until complete all areas.

writevn() perform the same functions as writev,
but write until complete all areas.

in comparison with original readv and writev,
note that the 2nd argument is NOT qualified with `const'.
*/

ssize_t
readn(d, buf, nbytes)
void *buf;
size_t nbytes;
{
	size_t total = 0;
	while (nbytes > 0) {
		ssize_t size;
		if ((size = read(d, buf, nbytes)) < 0) {
			return -1;
		}
		if (size == 0) break;
		total += size;
		buf = (char *)buf + size;
		nbytes -= size;
	}
	return total;
}

ssize_t
recvn(d, buf, nbytes, flags)
void *buf;
size_t nbytes;
{
	size_t total = 0;
	while (nbytes > 0) {
		ssize_t size;
		if ((size = recv(d, buf, nbytes, flags)) < 0) {
			return -1;
		}
		if (size == 0) break;
		total += size;
		buf = (char *)buf + size;
		nbytes -= size;
	}
	return total;
}

ssize_t
writen(d, buf, nbytes)
const void *buf;
size_t nbytes;
{
	size_t total = 0;
	while (nbytes > 0) {
		ssize_t size;
		if ((size = write(d, buf, nbytes)) == -1) {
			return -1;
		}
		if (size == 0) break;
		total += size;
		buf = (const char *)buf + size;
		nbytes -= size;
	}
	return total;
}

ssize_t
sendn(d, buf, nbytes, flags)
const void *buf;
size_t nbytes;
{
	size_t total = 0;
	while (nbytes > 0) {
		ssize_t size;
		if ((size = send(d, buf, nbytes, flags)) == -1) {
			return -1;
		}
		if (size == 0) break;
		total += size;
		buf = (const char *)buf + size;
		nbytes -= size;
	}
	return total;
}

#if 0
ssize_t
readvn(d, iov, iovcnt)
int d;
struct iovec *iov;
int iovcnt;
{
	size_t total = 0, adj = 0;
	while (iovcnt > 0) {
		int cnt;
		struct iovec save;
		ssize_t size;
		cnt = MIN(IOV_MAX, iovcnt);
		save = *iov;
		iov->iov_base = (char *)iov->iov_base + adj;
		iov->iov_len -= adj;
		if ((size = readv(d, iov, cnt)) < 0) {
			*iov = save;
			return -1;
		}
		*iov = save;
		if (size == 0) break;

		total += size;
		adj += size;

		while (adj >= iov->iov_len) {
			adj -= iov->iov_len;
			iov++;
			iovcnt--;
			assert(adj == 0 || iovcnt > 0);
		}
	}
	return total;
}

ssize_t
writevn(d, iov, iovcnt)
int d;
struct iovec *iov;
int iovcnt;
{
	size_t total = 0, adj = 0;
	while (iovcnt > 0) {
		int cnt;
		struct iovec save;
		ssize_t size;
		cnt = MIN(IOV_MAX, iovcnt);
		save = *iov;
		iov->iov_base = (char *)iov->iov_base + adj;
		iov->iov_len -= adj;
		if ((size = writev(d, iov, cnt)) == -1) {
			*iov = save;
			return -1;
		}
		*iov = save;
		if (size == 0) break;

		total += size;
		adj += size;

		while (adj >= iov->iov_len) {
			adj -= iov->iov_len;
			iov++;
			iovcnt--;
			assert(adj == 0 || iovcnt > 0);
		}
	}
	return total;
}
#endif
