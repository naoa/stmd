/*	$Id: chelate.h,v 1.2 2010/12/10 09:29:08 nis Exp $	*/

/*-
 * Copyright (c) 2010 Shingo Nishioka.
 * All rights reserved.
 */

struct chelate_t;

struct chelate_t *chelate_create(void (*)(void *), void *);
int chelate_sendrecv(struct chelate_t *, char const *, size_t, int (*)(char *, size_t, void *), void *);
int chelate_destroy(struct chelate_t *, int);
