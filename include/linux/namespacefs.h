/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * namespacefs.h - a pseudo file system for examining namespaces.
 */

#ifndef _NAMESPACEFS_H_
#define _NAMESPACEFS_H_

#ifdef CONFIG_NAMESPACE_FS

#include <linux/fs.h>

struct dentry *
namespacefs_create_file(const char *name, struct dentry *parent,
			const struct user_namespace *user_ns,
			const struct file_operations *fops,
			void *data);
struct dentry *
namespacefs_create_dir(const char *name, struct dentry *parent,
		       const struct user_namespace *user_ns);
void namespacefs_remove_dir(struct dentry *dentry);

#else

static inline struct dentry *
namespacefs_create_file(const char *name, struct dentry *parent,
			const struct user_namespace *user_ns,
			const struct file_operations *fops,
			void *data)
{
	return NULL;
}

static inline struct dentry *
namespacefs_create_dir(const char *name, struct dentry *parent,
		       const struct user_namespace *user_ns)
{
	return NULL;
}

static inline void namespacefs_remove_dir(struct dentry *dentry)
{
}

#endif /* CONFIG_NAMESPACE_FS */

#endif
