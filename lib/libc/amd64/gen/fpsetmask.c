/* $FreeBSD: src/lib/libc/amd64/gen/fpsetmask.c,v 1.1.36.1 2010/12/21 17:10:29 kensmith Exp $ */
#define __IEEEFP_NOINLINES__ 1
#include <ieeefp.h>

fp_except_t fpsetmask(fp_except_t m)
{
	return (__fpsetmask(m));
}
