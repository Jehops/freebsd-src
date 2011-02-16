/*
 * Copyright (c) 1995-1998 John Birrell <jb@cimlogic.com.au>
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
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY JOHN BIRRELL AND CONTRIBUTORS ``AS IS'' AND
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
 * $FreeBSD: src/lib/libthr/thread/thr_info.c,v 1.10.14.1 2010/12/21 17:10:29 kensmith Exp $
 */

#include "namespace.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <pthread_np.h>
#include "un-namespace.h"

#include "thr_private.h"

__weak_reference(_pthread_set_name_np, pthread_set_name_np);

/* Set the thread name for debug. */
void
_pthread_set_name_np(pthread_t thread, const char *name)
{
	struct pthread *curthread = _get_curthread();
	int ret = 0;

	if (curthread == thread) {
		if (thr_set_name(thread->tid, name))
			ret = errno;
	} else {
		if (_thr_ref_add(curthread, thread, 0) == 0) {
			THR_THREAD_LOCK(curthread, thread);
			if (thread->state != PS_DEAD) {
				if (thr_set_name(thread->tid, name))
					ret = errno;
			}
			THR_THREAD_UNLOCK(curthread, thread);
			_thr_ref_delete(curthread, thread);
		} else {
			ret = ESRCH;
		}
	}
#if 0
	/* XXX should return error code. */
	return (ret);
#endif
}
