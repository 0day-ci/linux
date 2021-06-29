
#include "srvfs.h"

#include <linux/fs.h>

static int srvfs_dir_unlink(struct inode *inode, struct dentry *dentry)
{
	struct srvfs_fileref *fileref = dentry->d_inode->i_private;

	if (fileref == NULL)
		return -EFAULT;

	d_delete(dentry);
	dput(dentry);

	return 0;
}

static int srvfs_dir_create(struct user_namespace *mnt_userns,
			    struct inode *inode, struct dentry *dentry,
			    umode_t mode, bool excl)
{
	return srvfs_insert_file(inode->i_sb, dget(dentry));
}

const struct inode_operations srvfs_rootdir_inode_operations = {
	.lookup		= simple_lookup,
	.unlink		= srvfs_dir_unlink,
	.create		= srvfs_dir_create,
};
