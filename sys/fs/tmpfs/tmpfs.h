/*	$NetBSD: tmpfs.h,v 1.26 2007/02/22 06:37:00 thorpej Exp $	*/

/*-
 * Copyright (c) 2005, 2006 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Julio M. Merino Vidal, developed as part of Google's Summer of Code
 * 2005 program.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/fs/tmpfs/tmpfs.h,v 1.11.2.4.6.1 2010/12/21 17:10:29 kensmith Exp $
 */

#ifndef _FS_TMPFS_TMPFS_H_
#define _FS_TMPFS_TMPFS_H_

/* ---------------------------------------------------------------------
 * KERNEL-SPECIFIC DEFINITIONS
 * --------------------------------------------------------------------- */
#include <sys/dirent.h>
#include <sys/mount.h>
#include <sys/queue.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/lock.h>
#include <sys/mutex.h>

/* --------------------------------------------------------------------- */
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/vmmeter.h>
#include <vm/swap_pager.h>

MALLOC_DECLARE(M_TMPFSMNT);
MALLOC_DECLARE(M_TMPFSNAME);

/* --------------------------------------------------------------------- */

/*
 * Internal representation of a tmpfs directory entry.
 */
struct tmpfs_dirent {
	TAILQ_ENTRY(tmpfs_dirent)	td_entries;

	/* Length of the name stored in this directory entry.  This avoids
	 * the need to recalculate it every time the name is used. */
	uint16_t			td_namelen;

	/* The name of the entry, allocated from a string pool.  This
	* string is not required to be zero-terminated; therefore, the
	* td_namelen field must always be used when accessing its value. */
	char *				td_name;

	/* Pointer to the node this entry refers to. */
	struct tmpfs_node *		td_node;
};

/* A directory in tmpfs holds a sorted list of directory entries, which in
 * turn point to other files (which can be directories themselves).
 *
 * In tmpfs, this list is managed by a tail queue, whose head is defined by
 * the struct tmpfs_dir type.
 *
 * It is imporant to notice that directories do not have entries for . and
 * .. as other file systems do.  These can be generated when requested
 * based on information available by other means, such as the pointer to
 * the node itself in the former case or the pointer to the parent directory
 * in the latter case.  This is done to simplify tmpfs's code and, more
 * importantly, to remove redundancy. */
TAILQ_HEAD(tmpfs_dir, tmpfs_dirent);

/* Each entry in a directory has a cookie that identifies it.  Cookies
 * supersede offsets within directories because, given how tmpfs stores
 * directories in memory, there is no such thing as an offset.  (Emulating
 * a real offset could be very difficult.)
 * 
 * The '.', '..' and the end of directory markers have fixed cookies which
 * cannot collide with the cookies generated by other entries.  The cookies
 * fot the other entries are generated based on the memory address on which
 * stores their information is stored.
 *
 * Ideally, using the entry's memory pointer as the cookie would be enough
 * to represent it and it wouldn't cause collisions in any system.
 * Unfortunately, this results in "offsets" with very large values which
 * later raise problems in the Linux compatibility layer (and maybe in other
 * places) as described in PR kern/32034.  Hence we need to workaround this
 * with a rather ugly hack.
 *
 * Linux 32-bit binaries, unless built with _FILE_OFFSET_BITS=64, have off_t
 * set to 'long', which is a 32-bit *signed* long integer.  Regardless of
 * the macro value, GLIBC (2.3 at least) always uses the getdents64
 * system call (when calling readdir) which internally returns off64_t
 * offsets.  In order to make 32-bit binaries work, *GLIBC* converts the
 * 64-bit values returned by the kernel to 32-bit ones and aborts with
 * EOVERFLOW if the conversion results in values that won't fit in 32-bit
 * integers (which it assumes is because the directory is extremely large).
 * This wouldn't cause problems if we were dealing with unsigned integers,
 * but as we have signed integers, this check fails due to sign expansion.
 *
 * For example, consider that the kernel returns the 0xc1234567 cookie to
 * userspace in a off64_t integer.  Later on, GLIBC casts this value to
 * off_t (remember, signed) with code similar to:
 *     system call returns the offset in kernel_value;
 *     off_t casted_value = kernel_value;
 *     if (sizeof(off_t) != sizeof(off64_t) &&
 *         kernel_value != casted_value)
 *             error!
 * In this case, casted_value still has 0xc1234567, but when it is compared
 * for equality against kernel_value, it is promoted to a 64-bit integer and
 * becomes 0xffffffffc1234567, which is different than 0x00000000c1234567.
 * Then, GLIBC assumes this is because the directory is very large.
 *
 * Given that all the above happens in user-space, we have no control over
 * it; therefore we must workaround the issue here.  We do this by
 * truncating the pointer value to a 32-bit integer and hope that there
 * won't be collisions.  In fact, this will not cause any problems in
 * 32-bit platforms but some might arise in 64-bit machines (I'm not sure
 * if they can happen at all in practice).
 *
 * XXX A nicer solution shall be attempted. */
#ifdef _KERNEL
#define	TMPFS_DIRCOOKIE_DOT	0
#define	TMPFS_DIRCOOKIE_DOTDOT	1
#define	TMPFS_DIRCOOKIE_EOF	2
static __inline
off_t
tmpfs_dircookie(struct tmpfs_dirent *de)
{
	off_t cookie;

	cookie = ((off_t)(uintptr_t)de >> 1) & 0x7FFFFFFF;
	MPASS(cookie != TMPFS_DIRCOOKIE_DOT);
	MPASS(cookie != TMPFS_DIRCOOKIE_DOTDOT);
	MPASS(cookie != TMPFS_DIRCOOKIE_EOF);

	return cookie;
}
#endif

/* --------------------------------------------------------------------- */

/*
 * Internal representation of a tmpfs file system node.
 *
 * This structure is splitted in two parts: one holds attributes common
 * to all file types and the other holds data that is only applicable to
 * a particular type.  The code must be careful to only access those
 * attributes that are actually allowed by the node's type.
 *
 *
 * Below is the key of locks used to protected the fields in the following
 * structures.
 *
 */
struct tmpfs_node {
	/* Doubly-linked list entry which links all existing nodes for a
	 * single file system.  This is provided to ease the removal of
	 * all nodes during the unmount operation. */
	LIST_ENTRY(tmpfs_node)	tn_entries;

	/* The node's type.  Any of 'VBLK', 'VCHR', 'VDIR', 'VFIFO',
	 * 'VLNK', 'VREG' and 'VSOCK' is allowed.  The usage of vnode
	 * types instead of a custom enumeration is to make things simpler
	 * and faster, as we do not need to convert between two types. */
	enum vtype		tn_type;

	/* Node identifier. */
	ino_t			tn_id;

	/* Node's internal status.  This is used by several file system
	 * operations to do modifications to the node in a delayed
	 * fashion. */
	int			tn_status;
#define	TMPFS_NODE_ACCESSED	(1 << 1)
#define	TMPFS_NODE_MODIFIED	(1 << 2)
#define	TMPFS_NODE_CHANGED	(1 << 3)

	/* The node size.  It does not necessarily match the real amount
	 * of memory consumed by it. */
	off_t			tn_size;

	/* Generic node attributes. */
	uid_t			tn_uid;
	gid_t			tn_gid;
	mode_t			tn_mode;
	int			tn_flags;
	nlink_t			tn_links;
	struct timespec		tn_atime;
	struct timespec		tn_mtime;
	struct timespec		tn_ctime;
	struct timespec		tn_birthtime;
	unsigned long		tn_gen;

	/* As there is a single vnode for each active file within the
	 * system, care has to be taken to avoid allocating more than one
	 * vnode per file.  In order to do this, a bidirectional association
	 * is kept between vnodes and nodes.
	 *
	 * Whenever a vnode is allocated, its v_data field is updated to
	 * point to the node it references.  At the same time, the node's
	 * tn_vnode field is modified to point to the new vnode representing
	 * it.  Further attempts to allocate a vnode for this same node will
	 * result in returning a new reference to the value stored in
	 * tn_vnode.
	 *
	 * May be NULL when the node is unused (that is, no vnode has been
	 * allocated for it or it has been reclaimed). */
	struct vnode *		tn_vnode;

	/* interlock to protect tn_vpstate */
	struct mtx	tn_interlock;

	/* Identify if current node has vnode assiocate with
	 * or allocating vnode.
	 */
	int		tn_vpstate;

	/* misc data field for different tn_type node */
	union {
		/* Valid when tn_type == VBLK || tn_type == VCHR. */
		dev_t			tn_rdev;

		/* Valid when tn_type == VDIR. */
		struct tn_dir{
			/* Pointer to the parent directory.  The root
			 * directory has a pointer to itself in this field;
			 * this property identifies the root node. */
			struct tmpfs_node *	tn_parent;

			/* Head of a tail-queue that links the contents of
			 * the directory together.  See above for a
			 * description of its contents. */
			struct tmpfs_dir	tn_dirhead;

			/* Number and pointer of the first directory entry
			 * returned by the readdir operation if it were
			 * called again to continue reading data from the
			 * same directory as before.  This is used to speed
			 * up reads of long directories, assuming that no
			 * more than one read is in progress at a given time.
			 * Otherwise, these values are discarded and a linear
			 * scan is performed from the beginning up to the
			 * point where readdir starts returning values. */
			off_t			tn_readdir_lastn;
			struct tmpfs_dirent *	tn_readdir_lastp;
		}tn_dir;

		/* Valid when tn_type == VLNK. */
		/* The link's target, allocated from a string pool. */
		char *			tn_link;

		/* Valid when tn_type == VREG. */
		struct tn_reg {
			/* The contents of regular files stored in a tmpfs
			 * file system are represented by a single anonymous
			 * memory object (aobj, for short).  The aobj provides
			 * direct access to any position within the file,
			 * because its contents are always mapped in a
			 * contiguous region of virtual memory.  It is a task
			 * of the memory management subsystem (see uvm(9)) to
			 * issue the required page ins or page outs whenever
			 * a position within the file is accessed. */
			vm_object_t		tn_aobj;
			size_t			tn_aobj_pages;

		}tn_reg;

		/* Valid when tn_type = VFIFO */
		struct tn_fifo {
			fo_rdwr_t		*tn_fo_read;
			fo_rdwr_t		*tn_fo_write;
		}tn_fifo;
	}tn_spec;
};
LIST_HEAD(tmpfs_node_list, tmpfs_node);

#define tn_rdev tn_spec.tn_rdev
#define tn_dir tn_spec.tn_dir
#define tn_link tn_spec.tn_link
#define tn_reg tn_spec.tn_reg
#define tn_fifo tn_spec.tn_fifo

#define TMPFS_NODE_LOCK(node) mtx_lock(&(node)->tn_interlock)
#define TMPFS_NODE_UNLOCK(node) mtx_unlock(&(node)->tn_interlock)
#define        TMPFS_NODE_MTX(node) (&(node)->tn_interlock)

#define TMPFS_VNODE_ALLOCATING	1
#define TMPFS_VNODE_WANT	2
/* --------------------------------------------------------------------- */

/*
 * Internal representation of a tmpfs mount point.
 */
struct tmpfs_mount {
	/* Maximum number of memory pages available for use by the file
	 * system, set during mount time.  This variable must never be
	 * used directly as it may be bigger than the current amount of
	 * free memory; in the extreme case, it will hold the SIZE_MAX
	 * value.  Instead, use the TMPFS_PAGES_MAX macro. */
	size_t			tm_pages_max;

	/* Number of pages in use by the file system.  Cannot be bigger
	 * than the value returned by TMPFS_PAGES_MAX in any case. */
	size_t			tm_pages_used;

	/* Pointer to the node representing the root directory of this
	 * file system. */
	struct tmpfs_node *	tm_root;

	/* Maximum number of possible nodes for this file system; set
	 * during mount time.  We need a hard limit on the maximum number
	 * of nodes to avoid allocating too much of them; their objects
	 * cannot be released until the file system is unmounted.
	 * Otherwise, we could easily run out of memory by creating lots
	 * of empty files and then simply removing them. */
	ino_t			tm_nodes_max;

	/* unrhdr used to allocate inode numbers */
	struct unrhdr *		tm_ino_unr;

	/* Number of nodes currently that are in use. */
	ino_t			tm_nodes_inuse;

	/* maximum representable file size */
	u_int64_t		tm_maxfilesize;

	/* Nodes are organized in two different lists.  The used list
	 * contains all nodes that are currently used by the file system;
	 * i.e., they refer to existing files.  The available list contains
	 * all nodes that are currently available for use by new files.
	 * Nodes must be kept in this list (instead of deleting them)
	 * because we need to keep track of their generation number (tn_gen
	 * field).
	 *
	 * Note that nodes are lazily allocated: if the available list is
	 * empty and we have enough space to create more nodes, they will be
	 * created and inserted in the used list.  Once these are released,
	 * they will go into the available list, remaining alive until the
	 * file system is unmounted. */
	struct tmpfs_node_list	tm_nodes_used;

	/* All node lock to protect the node list and tmp_pages_used */
	struct mtx allnode_lock;

	/* Pools used to store file system meta data.  These are not shared
	 * across several instances of tmpfs for the reasons described in
	 * tmpfs_pool.c. */
	uma_zone_t		tm_dirent_pool;
	uma_zone_t		tm_node_pool;
};
#define TMPFS_LOCK(tm) mtx_lock(&(tm)->allnode_lock)
#define TMPFS_UNLOCK(tm) mtx_unlock(&(tm)->allnode_lock)

/* --------------------------------------------------------------------- */

/*
 * This structure maps a file identifier to a tmpfs node.  Used by the
 * NFS code.
 */
struct tmpfs_fid {
	uint16_t		tf_len;
	uint16_t		tf_pad;
	ino_t			tf_id;
	unsigned long		tf_gen;
};

/* --------------------------------------------------------------------- */

#ifdef _KERNEL
/*
 * Prototypes for tmpfs_subr.c.
 */

int	tmpfs_alloc_node(struct tmpfs_mount *, enum vtype,
	    uid_t uid, gid_t gid, mode_t mode, struct tmpfs_node *,
	    char *, dev_t, struct thread *, struct tmpfs_node **);
void	tmpfs_free_node(struct tmpfs_mount *, struct tmpfs_node *);
int	tmpfs_alloc_dirent(struct tmpfs_mount *, struct tmpfs_node *,
	    const char *, uint16_t, struct tmpfs_dirent **);
void	tmpfs_free_dirent(struct tmpfs_mount *, struct tmpfs_dirent *,
	    boolean_t);
int	tmpfs_alloc_vp(struct mount *, struct tmpfs_node *, int,
	    struct vnode **, struct thread *);
void	tmpfs_free_vp(struct vnode *);
int	tmpfs_alloc_file(struct vnode *, struct vnode **, struct vattr *,
	    struct componentname *, char *);
void	tmpfs_dir_attach(struct vnode *, struct tmpfs_dirent *);
void	tmpfs_dir_detach(struct vnode *, struct tmpfs_dirent *);
struct tmpfs_dirent *	tmpfs_dir_lookup(struct tmpfs_node *node,
			    struct tmpfs_node *f,
			    struct componentname *cnp);
int	tmpfs_dir_getdotdent(struct tmpfs_node *, struct uio *);
int	tmpfs_dir_getdotdotdent(struct tmpfs_node *, struct uio *);
struct tmpfs_dirent *	tmpfs_dir_lookupbycookie(struct tmpfs_node *, off_t);
int	tmpfs_dir_getdents(struct tmpfs_node *, struct uio *, off_t *);
int	tmpfs_reg_resize(struct vnode *, off_t);
int	tmpfs_chflags(struct vnode *, int, struct ucred *, struct thread *);
int	tmpfs_chmod(struct vnode *, mode_t, struct ucred *, struct thread *);
int	tmpfs_chown(struct vnode *, uid_t, gid_t, struct ucred *,
	    struct thread *);
int	tmpfs_chsize(struct vnode *, u_quad_t, struct ucred *, struct thread *);
int	tmpfs_chtimes(struct vnode *, struct timespec *, struct timespec *,
	    struct timespec *, int, struct ucred *, struct thread *);
void	tmpfs_itimes(struct vnode *, const struct timespec *,
	    const struct timespec *);

void	tmpfs_update(struct vnode *);
int	tmpfs_truncate(struct vnode *, off_t);

/* --------------------------------------------------------------------- */

/*
 * Convenience macros to simplify some logical expressions.
 */
#define IMPLIES(a, b) (!(a) || (b))
#define IFF(a, b) (IMPLIES(a, b) && IMPLIES(b, a))

/* --------------------------------------------------------------------- */

/*
 * Checks that the directory entry pointed by 'de' matches the name 'name'
 * with a length of 'len'.
 */
#define TMPFS_DIRENT_MATCHES(de, name, len) \
    (de->td_namelen == (uint16_t)len && \
    memcmp((de)->td_name, (name), (de)->td_namelen) == 0)

/* --------------------------------------------------------------------- */

/*
 * Ensures that the node pointed by 'node' is a directory and that its
 * contents are consistent with respect to directories.
 */
#define TMPFS_VALIDATE_DIR(node) \
    MPASS((node)->tn_type == VDIR); \
    MPASS((node)->tn_size % sizeof(struct tmpfs_dirent) == 0); \
    MPASS((node)->tn_dir.tn_readdir_lastp == NULL || \
	tmpfs_dircookie((node)->tn_dir.tn_readdir_lastp) == (node)->tn_dir.tn_readdir_lastn);

/* --------------------------------------------------------------------- */

/*
 * Memory management stuff.
 */

/* Amount of memory pages to reserve for the system (e.g., to not use by
 * tmpfs).
 * XXX: Should this be tunable through sysctl, for instance? */
#define TMPFS_PAGES_RESERVED (4 * 1024 * 1024 / PAGE_SIZE)

/*
 * Returns information about the number of available memory pages,
 * including physical and virtual ones.
 *
 * If 'total' is TRUE, the value returned is the total amount of memory
 * pages configured for the system (either in use or free).
 * If it is FALSE, the value returned is the amount of free memory pages.
 *
 * Remember to remove TMPFS_PAGES_RESERVED from the returned value to avoid
 * excessive memory usage.
 *
 */
static __inline size_t
tmpfs_mem_info(void)
{
	size_t size;

	size = swap_pager_avail + cnt.v_free_count + cnt.v_inactive_count;
	size -= size > cnt.v_wire_count ? cnt.v_wire_count : size;
	return size;
}

/* Returns the maximum size allowed for a tmpfs file system.  This macro
 * must be used instead of directly retrieving the value from tm_pages_max.
 * The reason is that the size of a tmpfs file system is dynamic: it lets
 * the user store files as long as there is enough free memory (including
 * physical memory and swap space).  Therefore, the amount of memory to be
 * used is either the limit imposed by the user during mount time or the
 * amount of available memory, whichever is lower.  To avoid consuming all
 * the memory for a given mount point, the system will always reserve a
 * minimum of TMPFS_PAGES_RESERVED pages, which is also taken into account
 * by this macro (see above). */
static __inline size_t
TMPFS_PAGES_MAX(struct tmpfs_mount *tmp)
{
	size_t freepages;

	freepages = tmpfs_mem_info();
	freepages -= freepages < TMPFS_PAGES_RESERVED ?
	    freepages : TMPFS_PAGES_RESERVED;

	return MIN(tmp->tm_pages_max, freepages + tmp->tm_pages_used);
}

/* Returns the available space for the given file system. */
#define TMPFS_META_PAGES(tmp) (howmany((tmp)->tm_nodes_inuse * (sizeof(struct tmpfs_node) \
				+ sizeof(struct tmpfs_dirent)), PAGE_SIZE))
#define TMPFS_FILE_PAGES(tmp) ((tmp)->tm_pages_used)

#define TMPFS_PAGES_AVAIL(tmp) (TMPFS_PAGES_MAX(tmp) > \
			TMPFS_META_PAGES(tmp)+TMPFS_FILE_PAGES(tmp)? \
			TMPFS_PAGES_MAX(tmp) - TMPFS_META_PAGES(tmp) \
			- TMPFS_FILE_PAGES(tmp):0)

#endif

/* --------------------------------------------------------------------- */

/*
 * Macros/functions to convert from generic data structures to tmpfs
 * specific ones.
 */

static inline
struct tmpfs_mount *
VFS_TO_TMPFS(struct mount *mp)
{
	struct tmpfs_mount *tmp;

	MPASS((mp) != NULL && (mp)->mnt_data != NULL);
	tmp = (struct tmpfs_mount *)(mp)->mnt_data;
	return tmp;
}

static inline
struct tmpfs_node *
VP_TO_TMPFS_NODE(struct vnode *vp)
{
	struct tmpfs_node *node;

	MPASS((vp) != NULL && (vp)->v_data != NULL);
	node = (struct tmpfs_node *)vp->v_data;
	return node;
}

static inline
struct tmpfs_node *
VP_TO_TMPFS_DIR(struct vnode *vp)
{
	struct tmpfs_node *node;

	node = VP_TO_TMPFS_NODE(vp);
	TMPFS_VALIDATE_DIR(node);
	return node;
}

#endif /* _FS_TMPFS_TMPFS_H_ */
