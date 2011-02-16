/*	$NetBSD: fpgetround.c,v 1.2 2002/01/13 21:45:50 thorpej Exp $	*/

/*
 * Written by J.T. Conklin, Apr 10, 1995
 * Public domain.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: src/lib/libc/sparc64/gen/fpgetround.c,v 1.1.40.1 2010/12/21 17:10:29 kensmith Exp $");

#include <machine/fsr.h>
#include <ieeefp.h>

fp_rnd_t
fpgetround()
{
	unsigned int x;

	__asm__("st %%fsr,%0" : "=m" (x));
	return ((fp_rnd_t)FSR_GET_RD(x));
}
