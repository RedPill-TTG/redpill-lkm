/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Internal procfs definitions
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This file contains curated definitions from original fs/proc/internal.h file which we (unfortunately) need to access.
 * As the internal.h, as the name implies, is meant for in-tree use only official toolkits lack it.
 * Kernel version constrains here are taken from the real kernel source tree to prevent including wrong structs
 * definitions. Please keep it neat as mismatch will cause very hard to debug problems.
 */

#warning "Using compatibility file for fs/proc/internal.h - if possible do NOT compile using toolkit"

#include <linux/proc_fs.h>
#include <linux/proc_ns.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0) && LINUX_VERSION_CODE < KERNEL_VERSION(3,19,0) //v3.10 - v3.18
struct proc_dir_entry {
	unsigned int low_ino;
	umode_t mode;
	nlink_t nlink;
	kuid_t uid;
	kgid_t gid;
	loff_t size;
	const struct inode_operations *proc_iops;
	const struct file_operations *proc_fops;
	struct proc_dir_entry *next, *parent, *subdir;
	void *data;
	atomic_t count;		/* use count */
	atomic_t in_use;	/* number of callers into module in progress; */
			/* negative -> it's going away RSN */
	struct completion *pde_unload_completion;
	struct list_head pde_openers;	/* who did ->open, but not ->release */
	spinlock_t pde_unload_lock; /* proc_fops checks and pde_users bumps */
	u8 namelen;
	char name[];
};

union proc_op {
	int (*proc_get_link)(struct dentry *, struct path *);
	int (*proc_read)(struct task_struct *task, char *page);
	int (*proc_show)(struct seq_file *m,
		struct pid_namespace *ns, struct pid *pid,
		struct task_struct *task);
};

struct proc_inode {
	struct pid *pid;
	int fd;
	union proc_op op;
	struct proc_dir_entry *pde;
	struct ctl_table_header *sysctl;
	struct ctl_table *sysctl_entry;
	struct proc_ns ns;
	struct inode vfs_inode;
};

//See https://github.com/torvalds/linux/commit/771187d61bb3cbaf62c492ec3b8b789933f7691e
//v3.19 - <v4.9 (they changed it a bit starting with v4.9)
#elif LINUX_VERSION_CODE < KERNEL_VERSION(4,9,0)
struct proc_dir_entry {
	unsigned int low_ino;
	umode_t mode;
	nlink_t nlink;
	kuid_t uid;
	kgid_t gid;
	loff_t size;
	const struct inode_operations *proc_iops;
	const struct file_operations *proc_fops;
	struct proc_dir_entry *parent;
	struct rb_root subdir;
	struct rb_node subdir_node;
	void *data;
	atomic_t count;		/* use count */
	atomic_t in_use;	/* number of callers into module in progress; */
			/* negative -> it's going away RSN */
	struct completion *pde_unload_completion;
	struct list_head pde_openers;	/* who did ->open, but not ->release */
	spinlock_t pde_unload_lock; /* proc_fops checks and pde_users bumps */
	u8 namelen;
	char name[];
};

union proc_op {
	int (*proc_get_link)(struct dentry *, struct path *);
	int (*proc_show)(struct seq_file *m,
		struct pid_namespace *ns, struct pid *pid,
		struct task_struct *task);
};

struct proc_inode {
	struct pid *pid;
	int fd;
	union proc_op op;
	struct proc_dir_entry *pde;
	struct ctl_table_header *sysctl;
	struct ctl_table *sysctl_entry;
	const struct proc_ns_operations *ns_ops;
	struct inode vfs_inode;
};
#endif

//These methods are the same forever
static inline struct proc_inode *PROC_I(const struct inode *inode)
{
	return container_of(inode, struct proc_inode, vfs_inode);
}

static inline struct proc_dir_entry *PDE(const struct inode *inode)
{
	return PROC_I(inode)->pde;
}