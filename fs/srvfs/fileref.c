
#include "srvfs.h"

#include <linux/file.h>
#include <linux/slab.h>

struct srvfs_fileref *srvfs_fileref_new(void)
{
	struct srvfs_fileref *fileref;

	fileref = kzalloc(sizeof(struct srvfs_fileref), GFP_KERNEL);
	if (!fileref)
		return NULL;

	kref_init(&fileref->refcount);
	return fileref;
}

struct srvfs_fileref *srvfs_fileref_get(struct srvfs_fileref *fileref)
{
	kref_get(&fileref->refcount);
	return fileref;
}

void srvfs_fileref_destroy(struct kref *ref)
{
	struct srvfs_fileref *fileref =
	    container_of(ref, struct srvfs_fileref, refcount);
	if (fileref->file)
		fput(fileref->file);
	kfree(fileref);
}

void srvfs_fileref_put(struct srvfs_fileref *fileref)
{
	if (!fileref)
		return;
	kref_put(&fileref->refcount, srvfs_fileref_destroy);
}

void srvfs_fileref_set(struct srvfs_fileref *fileref, struct file *newfile)
{
	struct file *oldfile;

	oldfile = fileref->file;
	fileref->file = newfile;

	if (oldfile)
		fput(oldfile);
}
