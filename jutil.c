/*	$Id: jutil.c,v 1.12 2010/01/16 00:31:30 nis Exp $	*/

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
static char rcsid[] = "$Id: jutil.c,v 1.12 2010/01/16 00:31:30 nis Exp $";
#endif

#include <config.h>

#include <sys/param.h>
#include <sys/time.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <syslog.h>

#include <unicode/utf.h>
#include <unicode/utf8.h>

#include "jutil.h"

#include <gifnoc.h>

static int uctype(unsigned int);

#define VALID	0
#define ALLOW	1
#define INVAL	2
#define KANA	3
#define	EXTENDER	4

static int vtbl[0x0080] = {
/*  00 nul   01 soh   02 stx   03 etx   04 eot   05 enq   06 ack   07 bel */
    INVAL,   INVAL,   INVAL,   INVAL,   INVAL,   INVAL,   INVAL,   INVAL, 
/*  08 bs    09 ht    0a nl    0b vt    0c np    0d cr    0e so    0f si  */
    INVAL,   INVAL,   INVAL,   INVAL,   INVAL,   INVAL,   INVAL,   INVAL, 
/*  10 dle   11 dc1   12 dc2   13 dc3   14 dc4   15 nak   16 syn   17 etb */
    INVAL,   INVAL,   INVAL,   INVAL,   INVAL,   INVAL,   INVAL,   INVAL, 
/*  18 can   19 em    1a sub   1b esc   1c fs    1d gs    1e rs    1f us  */
    INVAL,   INVAL,   INVAL,   INVAL,   INVAL,   INVAL,   INVAL,   INVAL, 
/*  20 sp    21  !    22  "    23  #    24  $    25  %    26  &    27  '  */
    INVAL,   ALLOW,   INVAL,   ALLOW,   ALLOW,   ALLOW,   ALLOW,   ALLOW, 
/*  28  (    29  )    2a  *    2b  +    2c  ,    2d  -    2e  .    2f  /  */
    INVAL,   INVAL,   INVAL,   ALLOW,   ALLOW,   ALLOW,   ALLOW,   ALLOW, 
/*  30  0    31  1    32  2    33  3    34  4    35  5    36  6    37  7  */
    VALID,   VALID,   VALID,   VALID,   VALID,   VALID,   VALID,   VALID, 
/*  38  8    39  9    3a  :    3b  ;    3c  <    3d  =    3e  >    3f  ?  */
    VALID,   VALID,   ALLOW,   INVAL,   INVAL,   INVAL,   INVAL,   ALLOW, 
/*  40  @    41  A    42  B    43  C    44  D    45  E    46  F    47  G  */
    ALLOW,   VALID,   VALID,   VALID,   VALID,   VALID,   VALID,   VALID, 
/*  48  H    49  I    4a  J    4b  K    4c  L    4d  M    4e  N    4f  O  */
    VALID,   VALID,   VALID,   VALID,   VALID,   VALID,   VALID,   VALID, 
/*  50  P    51  Q    52  R    53  S    54  T    55  U    56  V    57  W  */
    VALID,   VALID,   VALID,   VALID,   VALID,   VALID,   VALID,   VALID, 
/*  58  X    59  Y    5a  Z    5b  [    5c  \    5d  ]    5e  ^    5f  _  */
    VALID,   VALID,   VALID,   INVAL,   INVAL,   INVAL,   INVAL,   ALLOW, 
/*  60  `    61  a    62  b    63  c    64  d    65  e    66  f    67  g  */
    INVAL,   VALID,   VALID,   VALID,   VALID,   VALID,   VALID,   VALID, 
/*  68  h    69  i    6a  j    6b  k    6c  l    6d  m    6e  n    6f  o  */
    VALID,   VALID,   VALID,   VALID,   VALID,   VALID,   VALID,   VALID, 
/*  70  p    71  q    72  r    73  s    74  t    75  u    76  v    77  w  */
    VALID,   VALID,   VALID,   VALID,   VALID,   VALID,   VALID,   VALID, 
/*  78  x    79  y    7a  z    7b  {    7c  |    7d  }    7e  ~    7f del */
    VALID,   VALID,   VALID,   INVAL,   INVAL,   INVAL,   INVAL,   INVAL, 
};

static int
uctype(c)
unsigned int c;
{
	if (c < 0x0080) {
		return vtbl[c];
	}

	if (c == 0x3005 /* Ideographic Iteration Mark */ ||
		0x309D <= c && c < 0x309F /* Hiragana Iteration Mark, Hiragana Voiced Iteration Mark */ ||
		0x30FC <= c && c < 0x30FF /* Katakana-Hiragana Prolonged Sound Mark, Katakana Iteration Mark, Katakana Voiced Iteration Mark */) {
		return EXTENDER;
	}

	if (0x0080 <= c && c < 0x0250 /* Latin-1 Supplement && Latin Extended-A,B */ ||
		0x0370 <= c && c < 0x0530 /* Greek, Cyrillic  */ ||
		0x1E00 <= c && c < 0x2000 /* Latin Extended Additional && Greek Extended */ ||
		c == 0x3007 /* Ideographic Number Zero */ ||
		0x30A0 <= c && c < 0x3100 /* Katakana */ ||
		0x31F0 <= c && c < 0x3200 /* Katakana Phonetic Extensions */ ||
		0x3400 <= c && c < 0x4DC0 /* CJK Unified Ideographs Extension A */ ||
		0x4E00 <= c && c < 0xA000 /* CJK Unified Ideographs */ ||
		0xF900 <= c && c < 0xFB00 /* CJK Compatibility Ideographs */) {
		return VALID;
	}
	if (0x3040 <= c && c < 0x30A0 /* Hiragana */) {
		return KANA;
	}
	return INVAL;
}

int
isasoundword(s)
char const *s;
{
	int i;
	UChar32 u;
	size_t length;
	int nc = 0;	/* # of characters (Incl. nvc, nkana) */
	int nvc = 0;	/* # of valid characters (Incl. nkana) */
	int nkana = 0;	/* # of Hiragana/Katakana characters */

	if (!strcmp(s, "\343\203\273")) {	/* Katakana Middle Dot */
		return 0;
	}
	length = strlen(s);
	for (i = 0; i < length; ) {
		U8_NEXT(s, i, length, u);
		if (u < 0) {
			return 0;
		}
		nc++;
		switch (uctype(u)) {
		case ALLOW:
			break;
		case EXTENDER:
			if (nc == 1) {
				return 0;
			}
			/* FALLTHRU */
		case VALID:
			nvc++;
			break;
		case KANA:
			nvc++;
			nkana++;
			break;
		case INVAL:
			return 0;
		default:
			syslog(LOG_DEBUG, "isasoundword: internal error");
			return 0;
		}
	}
	if (nvc == 0 || nc == 1 && nkana == 1 || nc == 2 && nkana == 2) {
		return 0;
	}
	return 1;
}
