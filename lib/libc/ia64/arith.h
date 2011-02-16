/*
 * MD header for contrib/gdtoa
 *
 * $FreeBSD: src/lib/libc/ia64/arith.h,v 1.2.38.1 2010/12/21 17:10:29 kensmith Exp $
 */

/*
 * NOTE: The definitions in this file must be correct or strtod(3) and
 * floating point formats in printf(3) will break!  The file can be
 * generated by running contrib/gdtoa/arithchk.c on the target
 * architecture.  See contrib/gdtoa/gdtoaimp.h for details.
 */

#include <machine/endian.h>

#if _BYTE_ORDER == _LITTLE_ENDIAN

#define IEEE_8087
#define Arith_Kind_ASL 1
#define Long int
#define Intcast (int)(long)
#define Double_Align
#define X64_bit_pointers

#else /* _BYTE_ORDER == _LITTLE_ENDIAN */

#define IEEE_MC68k
#define Arith_Kind_ASL 2
#define Long int
#define Intcast (int)(long)
#define Double_Align
#define X64_bit_pointers
#ifdef gcc_bug	/* XXX Why does arithchk report sudden underflow here? */
#define Sudden_Underflow
#endif

#endif
