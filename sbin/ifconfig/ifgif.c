/*-
 * Copyright (c) 2009 Hiroki Sato.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef lint
static const char rcsid[] =
  "$FreeBSD: src/sbin/ifconfig/ifgif.c,v 1.2.4.3.4.1 2010/12/21 17:10:29 kensmith Exp $";
#endif

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sockio.h>

#include <stdlib.h>
#include <unistd.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_gif.h>
#include <net/route.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>

#include "ifconfig.h"

static void	gif_status(int);

static struct {
	const char	*label;
	u_int		mask;
} gif_opts[] = {
	{ "ACCEPT_REV_ETHIP_VER",	GIF_ACCEPT_REVETHIP	},
	{ "SEND_REV_ETHIP_VER",		GIF_SEND_REVETHIP	},
};

static void
gif_status(int s)
{
	int opts;
	int nopts = 0;
	int i;

	ifr.ifr_data = (caddr_t)&opts;
	if (ioctl(s, GIFGOPTS, &ifr) == -1)
		return;
	if (opts == 0)
		return;

	printf("\toptions=%d<", opts);
	for (i=0; i < sizeof(gif_opts)/sizeof(gif_opts[0]); i++) {
		if (opts & gif_opts[i].mask) {
			if (nopts++)
				printf(",");
			printf("%s", gif_opts[i].label);
		}
	}
	printf(">\n");
}

static void
setgifopts(const char *val,
	int d, int s, const struct afswtch *afp)
{
	int opts;

	ifr.ifr_data = (caddr_t)&opts;
	if (ioctl(s, GIFGOPTS, &ifr) == -1) {
		warn("ioctl(GIFGOPTS)");
		return;
	}

	if (d < 0)
		opts &= ~(-d);
	else
		opts |= d;

	if (ioctl(s, GIFSOPTS, &ifr) == -1) {
		warn("ioctl(GIFSOPTS)");
		return;
	}
}

static struct cmd gif_cmds[] = {
	DEF_CMD("accept_rev_ethip_ver",	GIF_ACCEPT_REVETHIP,	setgifopts),
	DEF_CMD("-accept_rev_ethip_ver",-GIF_ACCEPT_REVETHIP,	setgifopts),
	DEF_CMD("send_rev_ethip_ver",	GIF_SEND_REVETHIP,	setgifopts),
	DEF_CMD("-send_rev_ethip_ver",	-GIF_SEND_REVETHIP,	setgifopts),
};

static struct afswtch af_gif = {
	.af_name	= "af_gif",
	.af_af		= AF_UNSPEC,
	.af_other_status = gif_status,
};

static __constructor void
gif_ctor(void)
{
#define	N(a)	(sizeof(a) / sizeof(a[0]))
	int i;

	for (i = 0; i < N(gif_cmds); i++)
		cmd_register(&gif_cmds[i]);
	af_register(&af_gif);
#undef N
}
