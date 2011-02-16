/*
 * Copyright (c) 1980, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1980, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif

#if 0
#ifndef lint
static char sccsid[] = "@(#)colcrt.c	8.1 (Berkeley) 6/6/93";
#endif
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: src/usr.bin/colcrt/colcrt.c,v 1.18.34.1 2010/12/21 17:10:29 kensmith Exp $");

#include <err.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

/*
 * colcrt - replaces col for crts with new nroff esp. when using tbl.
 * Bill Joy UCB July 14, 1977
 *
 * This filter uses a screen buffer, 267 half-lines by 132 columns.
 * It interprets the up and down sequences generated by the new
 * nroff when used with tbl and by \u \d and \r.
 * General overstriking doesn't work correctly.
 * Underlining is split onto multiple lines, etc.
 *
 * Option - suppresses all underlining.
 * Option -2 forces printing of all half lines.
 */

wchar_t	page[267][132];

int	outline = 1;
int	outcol;

char	suppresul;
char	printall;

static void	move(int, int);
static void	pflush(int);
static int	plus(wchar_t, wchar_t);
static void	usage(void);

int
main(int argc, char *argv[])
{
	wint_t c;
	wchar_t *cp, *dp;
	int ch, i, w;

	setlocale(LC_ALL, "");

	while ((ch = getopt(argc, argv, "-2")) != -1)
		switch (ch) {
		case '-':
			suppresul = 1;
			break;
		case '2':
			printall = 1;
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	do {
		if (argc > 0) {
			if (freopen(argv[0], "r", stdin) == NULL) {
				fflush(stdout);
				err(1, "%s", argv[0]);
			}
			argc--;
			argv++;
		}
		for (;;) {
			c = getwc(stdin);
			if (c == WEOF) {
				pflush(outline);
				fflush(stdout);
				break;
			}
			switch (c) {
			case '\n':
				if (outline >= 265)
					pflush(62);
				outline += 2;
				outcol = 0;
				continue;
			case '\016':
			case '\017':
				continue;
			case 033:
				c = getwc(stdin);
				switch (c) {
				case '9':
					if (outline >= 266)
						pflush(62);
					outline++;
					continue;
				case '8':
					if (outline >= 1)
						outline--;
					continue;
				case '7':
					outline -= 2;
					if (outline < 0)
						outline = 0;
					continue;
				default:
					continue;
				}
			case '\b':
				if (outcol)
					outcol--;
				continue;
			case '\t':
				outcol += 8;
				outcol &= ~7;
				outcol--;
				c = ' ';
			default:
				if ((w = wcwidth(c)) <= 0)
					w = 1;	/* XXX */
				if (outcol + w > 132) {
					outcol += w;
					continue;
				}
				cp = &page[outline][outcol];
				outcol += w;
				if (c == '_') {
					if (suppresul)
						continue;
					cp += 132;
					c = '-';
				}
				if (*cp == 0) {
					for (i = 0; i < w; i++)
						cp[i] = c;
					dp = cp - (outcol - w);
					for (cp--; cp >= dp && *cp == 0; cp--)
						*cp = ' ';
				} else {
					if (plus(c, *cp) || plus(*cp, c))
						*cp = '+';
					else if (*cp == ' ' || *cp == 0) {
						for (i = 1; i < w; i++)
							if (cp[i] != ' ' &&
							    cp[i] != 0)
								goto cont;
						for (i = 0; i < w; i++)
							cp[i] = c;
					}
				}
cont:
				continue;
			}
		}
		if (ferror(stdin))
			err(1, NULL);
	} while (argc > 0);
	fflush(stdout);
	exit(0);
}

static void
usage(void)
{
	fprintf(stderr, "usage: colcrt [-] [-2] [file ...]\n");
	exit(1);
}

static int
plus(wchar_t c, wchar_t d)
{

	return ((c == '|' && d == '-') || d == '_');
}

static void
pflush(int ol)
{
	static int first;
	int i;
	wchar_t *cp;
	char lastomit;
	int l, w;

	l = ol;
	lastomit = 0;
	if (l > 266)
		l = 266;
	else
		l |= 1;
	for (i = first | 1; i < l; i++) {
		move(i, i - 1);
		move(i, i + 1);
	}
	for (i = first; i < l; i++) {
		cp = page[i];
		if (printall == 0 && lastomit == 0 && *cp == 0) {
			lastomit = 1;
			continue;
		}
		lastomit = 0;
		while (*cp != L'\0') {
			if ((w = wcwidth(*cp)) > 0) {
				putwchar(*cp);
				cp += w;
			} else
				cp++;
		}
		putwchar(L'\n');
	}
	wmemcpy(page[0], page[ol], (267 - ol) * 132);
	wmemset(page[267- ol], L'\0', ol * 132);
	outline -= ol;
	outcol = 0;
	first = 1;
}

static void
move(int l, int m)
{
	wchar_t *cp, *dp;

	for (cp = page[l], dp = page[m]; *cp; cp++, dp++) {
		switch (*cp) {
			case '|':
				if (*dp != ' ' && *dp != '|' && *dp != 0)
					return;
				break;
			case ' ':
				break;
			default:
				return;
		}
	}
	if (*cp == 0) {
		for (cp = page[l], dp = page[m]; *cp; cp++, dp++)
			if (*cp == '|')
				*dp = '|';
			else if (*dp == 0)
				*dp = ' ';
		page[l][0] = 0;
	}
}
