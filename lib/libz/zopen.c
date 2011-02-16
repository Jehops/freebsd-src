/*
 * Public domain stdio wrapper for libz, written by Johan Danielsson.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: src/lib/libz/zopen.c,v 1.2.42.1 2010/12/21 17:10:29 kensmith Exp $");

#include <stdio.h>
#include <zlib.h>

FILE *zopen(const char *fname, const char *mode);

/* convert arguments */
static int
xgzread(void *cookie, char *data, int size)
{
    return gzread(cookie, data, size);
}

static int
xgzwrite(void *cookie, const char *data, int size)
{
    return gzwrite(cookie, (void*)data, size);
}

FILE *
zopen(const char *fname, const char *mode)
{
    gzFile gz = gzopen(fname, mode);
    if(gz == NULL)
	return NULL;

    if(*mode == 'r')
	return (funopen(gz, xgzread, NULL, NULL, gzclose));
    else
	return (funopen(gz, NULL, xgzwrite, NULL, gzclose));
}
