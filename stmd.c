/*	$Id: stmd.c,v 1.66 2011/10/24 06:30:26 nis Exp $	*/

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
static char rcsid[] = "$Id: stmd.c,v 1.66 2011/10/24 06:30:26 nis Exp $";
#endif

#include <config.h>

#include <sys/param.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/wait.h>

#if defined NO_FORK
#include <pthread.h>
#include <semaphore.h>
#endif

#include <netinet/in.h>
#include <netinet/tcp.h>

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <libgen.h>
#include <netdb.h>
#include <pwd.h>
#include <libgen.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <syslog.h>
#include <unistd.h>

#include "util.h"
#include "ystem.h"
#include "nio.h"

#include <gifnoc.h>

#define DBGxx	1 /* */

#define	BACKLOG_DFL	128

#define	XSTEM_HDR_LEN	16

#define	XSTEM_SOCKET	1

#if ! defined PF_LOCAL
#define PF_LOCAL	AF_UNIX
#endif
#if ! defined AF_LOCAL
#define AF_LOCAL	AF_UNIX
#endif

struct o {
	size_t size, len;
	char *ptr;
};

struct udata {
	struct o *o;
	int need_newline;
};

static void usage(void);

static int xstem_server(int, int, struct ystems *, char *, int);
static int procline(char const *, void *);
static int append(struct o *, char const *);	/* XXX: duplicated func. */

static int stmd_srv(struct ystems *, char const *, char const *, char const *, int);
static void *start_server0(void *);
static int sockets(char const *, int [], size_t, const char **, char const *, char const *, int);
#if 0
static void sigpipe(int);
#endif
#if defined SIGCHLD
static void sigchld(int);
#endif
static void sigint(int);

char const *progname = NULL;

static void
usage()
{
	syslog(LOG_DEBUG, "usage: %s "
#if ! defined NO_FORK
		"[-d] "
#endif
#if ! defined NO_LOCALSOCKET
		"[-u localsocket] "
#endif
		"[-e username] "
		"[-p port] [-b bind_address] [-B backlog]\n", progname);
	syslog(LOG_DEBUG, "       %s -i", progname);
	syslog(LOG_DEBUG, "       in.%s [-i]", progname);
	syslog(LOG_DEBUG, "       %s.cgi [-c]", progname);
	syslog(LOG_DEBUG, "       %s -q [-l stemmer]", progname);
	exit(1);
}

int
main(argc, argv)
char *argv[];
{
#if ! defined NO_LOCALSOCKET
	char const *localsocket = NULL;
#endif
	char const *bind_address = NULL;
	char const *serv = NULL;
	int cnt;
#if ! defined NO_FORK
	int dflag = 0;
#endif
	int iflag = 0;
	int qflag = 0;
	int cflag = 0;
	int ch;
	char *cl_stemmer_name = NULL;
	struct ystems *yp;
	size_t l;
#if defined TCP_NODELAY
	int one = 1;
#endif
	int backlog = BACKLOG_DFL;
	char *username = NULL;
	struct passwd *pw;

#if defined HAVE_WSASTARTUP
	if (wsastartup() == -1) {
		perror("wsastartup");
		return 1;
	}
#endif

	progname = basename(argv[0]);

	l = strlen(progname);
	if (l >= 3 && !strncmp(progname, "in.", 3)) {
		iflag = 1;
	}
	if (l >= 4 && !strncmp(progname + l - 4, ".cgi", 4)) {
		cflag = 1;
	}
#if defined CRLFTEXT
	if (l >= 8 && !strncmp(progname + l - 8, "-cgi.exe", 8)) {
		cflag = 1;
	}
#endif

	openlog(progname, LOG_PID, LOG_LOCAL0);
#if defined DBG
	syslog(LOG_DEBUG, "started");
#endif

	while ((ch = getopt(argc, argv, "iqcp:u:b:B:l:"
#if ! defined NO_FORK
		"d"
#endif
#if ! defined NO_LOCALSOCKET
		"u:"
#endif
		"e:")) != -1) {
		switch (ch) {
#if ! defined NO_FORK
		case 'd':
			dflag = 1;
			break;
#endif
		case 'B':
			backlog = atoi(optarg);
			break;
		case 'i':
			iflag = 1;
			break;
		case 'q':
			qflag = 1;
			break;
		case 'c':
			cflag = 1;
			break;
		case 'p':
			serv = optarg;
			break;
#if ! defined NO_LOCALSOCKET
		case 'u':
			localsocket = optarg;
			break;
#endif
		case 'b':
			bind_address = optarg;
			break;
		case 'l':
			cl_stemmer_name = optarg;
			break;
		case 'e':
			username = optarg;
			break;
		default:
			usage();
		}
	}
	argc -= optind, argv += optind;

	cnt = 0;
#if ! defined NO_FORK
	if (dflag) cnt++;
#endif
	if (iflag) cnt++;
	if (qflag) cnt++;
	if (cflag) cnt++;
	if (cnt > 1) {
		usage();
	}

	if (!(yp = ystem_create())) {
		return 1;
	}

#if ! defined NO_FORK	/* if !NO_FORK then pre-init each modules */
	if (ystem_init_stemmer(yp, NORMALIZER, NULL) == -1) {
		return 1;
	}
#endif

	/* invoked from inetd */
	if (iflag) {
		if (argc != 0) {
			usage();
		}
#if defined TCP_NODELAY
#if defined DBG
syslog(LOG_DEBUG, "tcp_nodelay");
#endif
		(void)setsockopt(0, IPPROTO_TCP, TCP_NODELAY, (char *)&one, sizeof one);
#endif
		xstem_server(0, 0, yp, NULL, 0);
		ystem_destroy(yp);
		_exit(0);
	}

	/* invoked from command-line */
	if (qflag) {
		if (argc != 0) {
			usage();
		}
		xstem_server(0, 1, yp, cl_stemmer_name, 0);
		ystem_destroy(yp);
		_exit(0);
	}

	/* called as a cgi-bin */
	if (cflag) {
		if (argc != 0) {
			usage();
		}
		stmc(yp);
		ystem_destroy(yp);
		_exit(0);
	}

	if (argc != 0) {
		usage();
	}

	if (!serv
#if ! defined NO_LOCALSOCKET
		 && !localsocket
#endif
		) {
		usage();
	}

	/*
	 * no flags (but -d) passed. start stmd_srv.
	 */

#if ! defined NO_LOCALSOCKET
	if (localsocket && access(localsocket, 0) != -1) {
		fprintf(stderr, "error: %s exists\n", localsocket);
		syslog(LOG_DEBUG, "error: %s exists", localsocket);
		return 1;
	}
#endif

#if ! defined NO_FORK
	/* detache tty */
	if (dflag) {
#if defined HAVE_DAEMON
		daemon(0, 0);
#else
		switch (fork()) {
			int d;
		case 0:
			if ((d = open("/dev/null", O_RDONLY)) == -1) {
				syslog(LOG_DEBUG, "/dev/null: %s", strerror(errno));
				exit(1);
			}
			dup2(d, 0);
			if ((d = open("/dev/null", O_WRONLY)) == -1) {
				syslog(LOG_DEBUG, "/dev/null: %s", strerror(errno));
				exit(1);
			}
			dup2(d, 1);
			dup2(d, 2);
			break;
		default:
			_exit(0);
		}
#endif
	}
#endif

	if (username && (pw = getpwnam(username))) {
		setgid(pw->pw_gid);
		setuid(pw->pw_uid);
	}

#if 0
	signal(SIGPIPE, sigpipe);
	sigflg(SIGPIPE, SA_RESTART, SA_RESETHAND);
#endif
#if defined SIGCHLD
	signal(SIGCHLD, sigchld);
	sigflg(SIGCHLD, SA_RESTART, SA_RESETHAND);
#endif
#if defined SIGHUP
	signal(SIGHUP, sigint);
#endif
	signal(SIGINT, sigint);
#if defined SIGQUIT
	signal(SIGQUIT, sigint);
#endif
	signal(SIGTERM, sigint);

#if ! defined NO_FORK	/* if ! NO_FORK then pre-initialize modules */
/*	cannot pre-initialize a stemmer which receives opts
	if (ystem_init_stemmer(yp, NGRAM, NULL) == -1) {
		return 1;
	}
*/
#if defined USE_CHASEN
	if (ystem_init_stemmer(yp, CHASEN, NULL) == -1) {
		return 1;
	}
#endif
#if defined USE_SNOWBALL
	if (ystem_init_stemmer(yp, SNOWBALL, NULL) == -1) {
		return 1;
	}
#endif
#if defined USE_MECAB
#if ! defined ENABLE_MECAB_OPTS
	if (ystem_init_stemmer(yp, MECAB, NULL) == -1) {
		return 1;
	}
#endif /* ! ENABLE_MECAB_OPTS */
#endif
#if defined USE_NSTMS
	if (ystem_init_stemmer(yp, NSTMS, NULL) == -1) {
		return 1;
	}
#endif
#if defined USE_EXTS
/*	cannot pre-initialize a stemmer which receives opts
	if (ystem_init_stemmer(yp, EXTS, NULL) == -1) {
		return 1;
	}
*/
#endif
#if defined USE_POSTAG
	if (ystem_init_stemmer(yp, POSTAG, NULL) == -1) {
		return 1;
	}
#endif
#endif	/* ! NO_FORK (pre-initialize modules) */

	if (stmd_srv(yp, serv, 
#if ! defined NO_LOCALSOCKET
		localsocket, 
#else
		NULL,
#endif
		bind_address, backlog) == -1) {
		syslog(LOG_DEBUG, "Bye...");
		return 1;
	}
	syslog(LOG_DEBUG, "bye...");

	return 0;
}

static int
xstem_server(s, t, yp, cl_stemmer_name, flags)
struct ystems *yp;
char *cl_stemmer_name;
{
#define	READN(s, b, l) (flags & XSTEM_SOCKET ? recvn((s), (b), (l), 0) : readn((s), (b), (l)))
#define	WRITEN(s, b, l) (flags & XSTEM_SOCKET ? sendn((s), (b), (l), 0) : writen((s), (b), (l)))
	int stemmer;
	char *opts;
	char *stemmer_name;
	char length[XSTEM_HDR_LEN];
	char *msg = NULL;
	size_t size = 0;
	ssize_t l;
	struct o o0, *o = &o0;
	struct udata u0, *u = &u0;

#if defined DBG
syslog(LOG_DEBUG, "xstem_server started");
#endif
	if (cl_stemmer_name) {
		stemmer_name = cl_stemmer_name;
	}
	else {
		if (READN(s, length, sizeof length) != sizeof length) {
			syslog(LOG_DEBUG, "readn: %s", strerror(errno));
			return -1;
		}
		length[sizeof length - 1] = '\0';
#if defined DBG
syslog(LOG_DEBUG, "xstem_server length = %s", length);
#endif
		l = strtol(length, NULL, 10);
		if (!(stemmer_name = malloc(l + 1))) {
			return -1;
		}
		if (READN(s, stemmer_name, l) != l) {
			syslog(LOG_DEBUG, "readn: %s", strerror(errno));
			return -1;
		}
		stemmer_name[l] = '\0';
	}
#if defined DBG
syslog(LOG_DEBUG, "xstem_server lang = %s", stemmer_name);
#endif

	if (!(stemmer = decode_stemmer_name(stemmer_name, &opts))) {
		syslog(LOG_DEBUG, "decode_stemmer_name failed: %s", stemmer_name);
		return -1;
	}

	o->ptr = NULL;
	o->size = o->len = 0;
	u->o = o;

	if (ystem_init_stemmer(yp, stemmer, opts) == -1) {
		syslog(LOG_DEBUG, "ystem_init_stemmer failed");
		return -1;
	}

	switch (stemmer) {
	case NORMALIZER:
		u->need_newline = 0;
		break;
	default:
		u->need_newline = 1;
		break;
	}

	for (;;) {
#if defined DBG
syslog(LOG_DEBUG, "xstem_server accepting next segment");
#endif
		if (READN(s, length, sizeof length) != sizeof length) {
			syslog(LOG_DEBUG, "readn: %s", strerror(errno));
			return -1;
		}
		length[sizeof length - 1] = '\0';
#if defined DBG
syslog(LOG_DEBUG, "xstem_server length = %s", length);
#endif
		if ((l = strtol(length, NULL, 10)) == -1) {
			break;
		}
		if (size <= l) {
			size_t newsize = l + 1;
			void *new;
			if (!(new = realloc(msg, l + 1))) {
				free(msg);
				size = 0;
				syslog(LOG_DEBUG, "realloc: %s", strerror(errno));
				return -1;
			}
			msg = new;
			size = newsize;
		}
		if (READN(s, msg, l) != l) {
			syslog(LOG_DEBUG, "readn: %s", strerror(errno));
			return -1;
		}
		msg[l] = '\0';

		o->len = 0;

#if defined DBG
syslog(LOG_DEBUG, "xstem_server call ystem: %p", yp);
#endif
		if (ystem(yp, msg, procline, u, stemmer, opts) == -1) {
			return -1;
		}

		bzero(length, sizeof length);
		snprintf(length, sizeof length, "%ld", (long)o->len);
#if defined DBG
syslog(LOG_DEBUG, "xstem_server reply length = %s", length);
#endif
		if (WRITEN(t, length, sizeof length) != sizeof length) {
			syslog(LOG_DEBUG, "writen: %s", strerror(errno));
			return -1;
		}
#if defined DBG
syslog(LOG_DEBUG, "xstem_server send reply");
#endif
		if (o->len && WRITEN(t, o->ptr, o->len) != o->len) {
			syslog(LOG_DEBUG, "writen: %s", strerror(errno));
			return -1;
		}
#if defined DBG
syslog(LOG_DEBUG, "xstem_server sent %ld", (long)o->len);
#endif
	}
	return 0;
#undef	READN
#undef	WRITEN
}

static int
procline(ln, u0)
char const *ln;
void *u0;
{
	struct udata *u = u0;

	if (append(u->o, ln) == -1) {
		return -1;
	}
	if (u->need_newline && append(u->o, "\n") == -1) {
		return -1;
	}
	return 0;
}

static int
append(a, s)
struct o *a;
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

#if defined HAVE_WSASTARTUP
#define	close(d)	closesocket((d))
#endif

int signaled = 0;
int sig_n;

#if defined NO_FORK
struct qe {
	int d;	/* socket to be served */
	struct qe *next;
};

struct pst {
	sem_t sem_w, sem_r;
	struct qe *q;
};
#endif

struct server_arg {
#if ! defined NO_FORK
	int ss;
	struct ystems *yp;
#else
	struct pst *pstp;
#ifdef DBGxx
	int i;
#endif
#endif
};

static int
stmd_srv(yp, serv, localsocket, bind_address, backlog)
struct ystems *yp;
char const *serv, *localsocket, *bind_address;
{
	int s[16];
	int ns;
	const char *cause;
#if defined TCP_NODELAY
	int one = 1;
#endif
#if defined NO_FORK
#define	MAXTHREADS	32
	struct {
		pthread_t thread;
		struct server_arg server_arg;
	} threads[MAXTHREADS];
	int i;

	struct pst pst;

	sem_init(&pst.sem_w, 0, 1);	/* -- write lock of the queue */
	sem_init(&pst.sem_r, 0, 0);	/* -- length of the queue */
	pst.q = NULL;

	for (i = 0; i < nelems(threads); i++) {
#ifdef DBGxx
		threads[i].server_arg.i = i;
#endif
		threads[i].server_arg.pstp = &pst;
		if (pthread_create(&threads[i].thread, NULL, start_server0, &threads[i].server_arg)) {
			perror("pthread_create");
		}
	}

#endif

	if ((ns = sockets(serv, s, nelems(s), &cause, localsocket, bind_address, backlog)) == 0) {
		syslog(LOG_DEBUG, "%s: %s", cause, strerror(errno));
		return -1;
	}

	syslog(LOG_DEBUG, "Server ready");

	for (; !signaled; ) {
		int i;
		fd_set readfds;
		int nfds;

		FD_ZERO(&readfds);

		nfds = 0;
		for (i = 0; i < ns; i++) {
			FD_SET(s[i], &readfds);
			if (s[i] > nfds) {
				nfds = s[i];
			}
		}

		nfds++;

		if (select(nfds, &readfds, NULL, NULL, NULL) == -1) {
			if (errno != EINTR) {
				syslog(LOG_DEBUG, "select: %s", strerror(errno));
			}
			continue;
		}

		for (i = 0; i < ns; i++) {
#if ! defined NO_FORK
			struct server_arg server_arg;
#else
			struct qe *e;
#endif
			struct sockaddr_storage addr;
			socklen_t len;
			int ss;
			if (!FD_ISSET(s[i], &readfds)) {
				continue;
			}
			len = sizeof addr;
			if ((ss = accept(s[i], (struct sockaddr *)&addr, &len)) < 0) {
				syslog(LOG_DEBUG, "accept: %s", strerror(errno));
				continue;
			}
#if ! defined NO_FORK
			server_arg.ss = ss;
			server_arg.yp = yp;
			switch (fork()) {
				int e;
			case -1:
				syslog(LOG_DEBUG, "fork: %s", strerror(errno));
				exit(1);
			case 0:
				for (i = 0; i < ns; i++) {
					close(s[i]);
				}
#if defined TCP_NODELAY
#if defined DBG
syslog(LOG_DEBUG, "tcp_nodelay");
#endif
				(void)setsockopt(ss, IPPROTO_TCP, TCP_NODELAY, (char *)&one, sizeof one);
#endif
				e = start_server0(&server_arg) != NULL;
				_exit(e);
			default:
				close(ss);
				break;
			}
#else	/* NO_FORK */
	e = calloc(1, sizeof *e);
	e->d = ss;
	e->next = NULL;
	sem_wait(&pst.sem_w);
	if (!pst.q) {
		pst.q = e;
	}
	else {
		struct qe *r;
		for (r = pst.q; r->next; r = r->next) ;
		r->next = e;
	}
	sem_post(&pst.sem_w);
	sem_post(&pst.sem_r);
#endif	/* NO_FORK */
#if 0
	again:
		if (pthread_create(&threads[i].thread, NULL, start_server0, &server_arg)) {
			perror("pthread_create");
		}
		/* renew yp */
		if (!(yp = ystem_create())) {
			return 1;
		}
#endif /* 0 */
		}
	}
#if ! defined NO_LOCALSOCKET
	if (localsocket) {
		unlink(localsocket);
	}
#endif
	if (signaled) {
		syslog(LOG_DEBUG, "SIGNAL: %d", sig_n);
	}
	return 0;
}

static void *
start_server0(cookie)
void *cookie;
{
	struct server_arg *a = cookie;
#if ! defined NO_FORK
	xstem_server(a->ss, a->ss, a->yp, NULL, XSTEM_SOCKET);
	ystem_destroy(a->yp);
	close(a->ss);
	return NULL;
#else
#ifdef DBGxx
	fprintf(stderr, "R%02d ", a->i);
#endif
	for (;;) {
		struct qe *e;
		struct ystems *yp;
		if (!(yp = ystem_create())) {
			fprintf(stderr, "ystem_crate failed\n");
			return "error";
		}
		sem_wait(&a->pstp->sem_r);
		sem_wait(&a->pstp->sem_w);
		if (!a->pstp->q) {
			fprintf(stderr, "assertion failed: a->pstp->q");
			return "error";
		}
		e = a->pstp->q;
		a->pstp->q = a->pstp->q->next;
		sem_post(&a->pstp->sem_w);
#ifdef DBGxx
		fprintf(stderr, "S%02d ", a->i);
#endif
		xstem_server(e->d, e->d, yp, NULL, XSTEM_SOCKET);
		ystem_destroy(yp);
#ifdef DBGxx
		fprintf(stderr, "E%02d ", a->i);
#endif
		close(e->d);
		free(e);
	}
#endif
}

static int
sockets(servname, s, size, cause, localsocket, bind_address, backlog)
char const *servname;
int s[];
size_t size;
const char **cause;
char const *localsocket;
char const *bind_address;
{
	int nsock = 0;

	*cause = "";

#if ! defined NO_LOCALSOCKET
	do {
		struct sockaddr_un myaddr;
		int sun_len;

		if (!localsocket) {
			continue;
		}

		sun_len = sizeof myaddr.sun_family + strlen(localsocket) + 1;
		s[nsock] = socket(PF_LOCAL, SOCK_STREAM, 0);
		if (s[nsock] < 0) {
			*cause = "socket";
			continue;
		}
		memset(&myaddr, 0, sizeof myaddr);
		myaddr.sun_family = PF_LOCAL;
#if HAVE_SUN_LEN
		myaddr.sun_len = sun_len;
#endif
		if (strlen(localsocket) >= sizeof myaddr.sun_path) {
			close(s[nsock]);
			continue;
		}
		strcpy(myaddr.sun_path, localsocket);
		if (bind(s[nsock], (struct sockaddr *)&myaddr, sun_len)==-1) {
			close(s[nsock]);
			*cause = "bind";
			continue;
		}
		(void) listen(s[nsock++], backlog);
		chmod(localsocket, 0777);
	} while (0);
#endif

	if (servname) {
		struct addrinfo hints, *res, *res0;
		int e;
		hints.ai_family = PF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = AI_PASSIVE;
		hints.ai_protocol = 0;
		hints.ai_addrlen = 0;
		hints.ai_canonname = NULL;
		hints.ai_addr = NULL;
		hints.ai_next = NULL;
		if (e = getaddrinfo(bind_address, servname, &hints, &res0)) {
			syslog(LOG_DEBUG, "%s", gai_strerror(e));
			*cause = "getaddrinfo";
			return 0;
		}

		for (res = res0; res && nsock < size; res = res->ai_next) {
			s[nsock] = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
			if (s[nsock] < 0) {
				*cause = "socket";
				continue;
			}

			if (bind(s[nsock], res->ai_addr, res->ai_addrlen) < 0) {
				close(s[nsock]);
				*cause = "bind";
				continue;
			}
			(void) listen(s[nsock++], backlog);
		}
		freeaddrinfo(res0);
	}
	return nsock;
}

#if 0
static void
sigpipe(n)
{
	syslog(LOG_DEBUG, "sigpipe");
}
#endif

#if defined SIGCHLD
static void
sigchld(n)
{
	int status;

	for (; wait3(&status, WNOHANG, NULL) > 0; ) {
#if defined DBG
		syslog(LOG_DEBUG, "# wait3: status = %d", status);
#endif
	}
}
#endif

static void
sigint(n)
{
	signaled = 1;
	sig_n = n;
}
