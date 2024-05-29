/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2013, 2014, 2024 The FreeBSD Foundation
 *
 * This software was developed by Konstantin Belousov <kib@FreeBSD.org>
 * under sponsorship from the FreeBSD Foundation.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/memdesc.h>
#include <sys/mutex.h>
#include <sys/sf_buf.h>
#include <sys/sysctl.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/rwlock.h>
#include <sys/taskqueue.h>
#include <sys/tree.h>
#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/vm_object.h>
#include <dev/pci/pcireg.h>
#include <machine/atomic.h>
#include <machine/bus.h>
#include <machine/cpu.h>
#include <x86/include/busdma_impl.h>
#include <dev/iommu/busdma_iommu.h>
#include <dev/iommu/iommu.h>
#include <x86/iommu/x86_iommu.h>
#include <x86/iommu/iommu_intrmap.h>

vm_page_t
iommu_pgalloc(vm_object_t obj, vm_pindex_t idx, int flags)
{
	vm_page_t m;
	int zeroed, aflags;

	zeroed = (flags & IOMMU_PGF_ZERO) != 0 ? VM_ALLOC_ZERO : 0;
	aflags = zeroed | VM_ALLOC_NOBUSY | VM_ALLOC_SYSTEM | VM_ALLOC_NODUMP |
	    ((flags & IOMMU_PGF_WAITOK) != 0 ? VM_ALLOC_WAITFAIL :
	    VM_ALLOC_NOWAIT);
	for (;;) {
		if ((flags & IOMMU_PGF_OBJL) == 0)
			VM_OBJECT_WLOCK(obj);
		m = vm_page_lookup(obj, idx);
		if ((flags & IOMMU_PGF_NOALLOC) != 0 || m != NULL) {
			if ((flags & IOMMU_PGF_OBJL) == 0)
				VM_OBJECT_WUNLOCK(obj);
			break;
		}
		m = vm_page_alloc_contig(obj, idx, aflags, 1, 0,
		    iommu_high, PAGE_SIZE, 0, VM_MEMATTR_DEFAULT);
		if ((flags & IOMMU_PGF_OBJL) == 0)
			VM_OBJECT_WUNLOCK(obj);
		if (m != NULL) {
			if (zeroed && (m->flags & PG_ZERO) == 0)
				pmap_zero_page(m);
			atomic_add_int(&iommu_tbl_pagecnt, 1);
			break;
		}
		if ((flags & IOMMU_PGF_WAITOK) == 0)
			break;
	}
	return (m);
}

void
iommu_pgfree(vm_object_t obj, vm_pindex_t idx, int flags)
{
	vm_page_t m;

	if ((flags & IOMMU_PGF_OBJL) == 0)
		VM_OBJECT_WLOCK(obj);
	m = vm_page_grab(obj, idx, VM_ALLOC_NOCREAT);
	if (m != NULL) {
		vm_page_free(m);
		atomic_subtract_int(&iommu_tbl_pagecnt, 1);
	}
	if ((flags & IOMMU_PGF_OBJL) == 0)
		VM_OBJECT_WUNLOCK(obj);
}

void *
iommu_map_pgtbl(vm_object_t obj, vm_pindex_t idx, int flags,
    struct sf_buf **sf)
{
	vm_page_t m;
	bool allocated;

	if ((flags & IOMMU_PGF_OBJL) == 0)
		VM_OBJECT_WLOCK(obj);
	m = vm_page_lookup(obj, idx);
	if (m == NULL && (flags & IOMMU_PGF_ALLOC) != 0) {
		m = iommu_pgalloc(obj, idx, flags | IOMMU_PGF_OBJL);
		allocated = true;
	} else
		allocated = false;
	if (m == NULL) {
		if ((flags & IOMMU_PGF_OBJL) == 0)
			VM_OBJECT_WUNLOCK(obj);
		return (NULL);
	}
	/* Sleepable allocations cannot fail. */
	if ((flags & IOMMU_PGF_WAITOK) != 0)
		VM_OBJECT_WUNLOCK(obj);
	sched_pin();
	*sf = sf_buf_alloc(m, SFB_CPUPRIVATE | ((flags & IOMMU_PGF_WAITOK)
	    == 0 ? SFB_NOWAIT : 0));
	if (*sf == NULL) {
		sched_unpin();
		if (allocated) {
			VM_OBJECT_ASSERT_WLOCKED(obj);
			iommu_pgfree(obj, m->pindex, flags | IOMMU_PGF_OBJL);
		}
		if ((flags & IOMMU_PGF_OBJL) == 0)
			VM_OBJECT_WUNLOCK(obj);
		return (NULL);
	}
	if ((flags & (IOMMU_PGF_WAITOK | IOMMU_PGF_OBJL)) ==
	    (IOMMU_PGF_WAITOK | IOMMU_PGF_OBJL))
		VM_OBJECT_WLOCK(obj);
	else if ((flags & (IOMMU_PGF_WAITOK | IOMMU_PGF_OBJL)) == 0)
		VM_OBJECT_WUNLOCK(obj);
	return ((void *)sf_buf_kva(*sf));
}

void
iommu_unmap_pgtbl(struct sf_buf *sf)
{

	sf_buf_free(sf);
	sched_unpin();
}

iommu_haddr_t iommu_high;
int iommu_tbl_pagecnt;

SYSCTL_NODE(_hw_iommu, OID_AUTO, dmar, CTLFLAG_RD | CTLFLAG_MPSAFE,
    NULL, "");
SYSCTL_INT(_hw_iommu_dmar, OID_AUTO, tbl_pagecnt, CTLFLAG_RD,
    &iommu_tbl_pagecnt, 0,
    "Count of pages used for DMAR pagetables");

static struct x86_iommu *x86_iommu;

void
set_x86_iommu(struct x86_iommu *x)
{
	MPASS(x86_iommu == NULL);
	x86_iommu = x;
}

struct x86_iommu *
get_x86_iommu(void)
{
	return (x86_iommu);
}

void
iommu_domain_unload_entry(struct iommu_map_entry *entry, bool free,
    bool cansleep)
{
	x86_iommu->domain_unload_entry(entry, free, cansleep);
}

void
iommu_domain_unload(struct iommu_domain *iodom,
    struct iommu_map_entries_tailq *entries, bool cansleep)
{
	x86_iommu->domain_unload(iodom, entries, cansleep);
}

struct iommu_ctx *
iommu_get_ctx(struct iommu_unit *iommu, device_t dev, uint16_t rid,
    bool id_mapped, bool rmrr_init)
{
	return (x86_iommu->get_ctx(iommu, dev, rid, id_mapped, rmrr_init));
}

void
iommu_free_ctx_locked(struct iommu_unit *iommu, struct iommu_ctx *context)
{
	x86_iommu->free_ctx_locked(iommu, context);
}

void
iommu_free_ctx(struct iommu_ctx *context)
{
	x86_iommu->free_ctx(context);
}

struct iommu_unit *
iommu_find(device_t dev, bool verbose)
{
	return (x86_iommu->find(dev, verbose));
}

int
iommu_alloc_msi_intr(device_t src, u_int *cookies, u_int count)
{
	return (x86_iommu->alloc_msi_intr(src, cookies, count));
}

int
iommu_map_msi_intr(device_t src, u_int cpu, u_int vector, u_int cookie,
    uint64_t *addr, uint32_t *data)
{
	return (x86_iommu->map_msi_intr(src, cpu, vector, cookie,
	    addr, data));
}

int
iommu_unmap_msi_intr(device_t src, u_int cookie)
{
	return (x86_iommu->unmap_msi_intr(src, cookie));
}

int
iommu_map_ioapic_intr(u_int ioapic_id, u_int cpu, u_int vector, bool edge,
    bool activehi, int irq, u_int *cookie, uint32_t *hi, uint32_t *lo)
{
	return (x86_iommu->map_ioapic_intr(ioapic_id, cpu, vector, edge,
	    activehi, irq, cookie, hi, lo));
}

int
iommu_unmap_ioapic_intr(u_int ioapic_id, u_int *cookie)
{
	return (x86_iommu->unmap_ioapic_intr(ioapic_id, cookie));
}
