// SPDX-License-Identifier: GPL-2.0-or-later

#include <kunit/test.h>
#include <linux/configfs.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/uio.h>

/*
 * Maximum number of bytes supported by the configfs attributes in this unit
 * test.
 */
enum { ATTR_MAX_SIZE = 256 };

static struct test_item {
	uint32_t nbytes;
	char data[ATTR_MAX_SIZE];
} bin_attr, text_attr;

static ssize_t attr_read(struct test_item *ti, void *buf, size_t len)
{
	size_t nbytes = min_t(size_t, len, ti->nbytes);

	memcpy(buf, ti->data, nbytes);
	return nbytes;
}

static ssize_t attr_write(struct test_item *ti, const void *buf, size_t len)
{
	if (len > ATTR_MAX_SIZE)
		return -EINVAL;
	ti->nbytes = len;
	memcpy(ti->data, buf, len);
	return len;
}

static DEFINE_SEMAPHORE(bin_attr_written);

static ssize_t bin_attr_read(struct config_item *item, void *buf, size_t len)
{
	return buf ? attr_read(&bin_attr, buf, len) : bin_attr.nbytes;
}

static ssize_t bin_attr_write(struct config_item *item, const void *buf,
			      size_t len)
{
	up(&bin_attr_written);
	return attr_write(&bin_attr, buf, len);
}

CONFIGFS_BIN_ATTR(, bin_attr, NULL, ATTR_MAX_SIZE);

static struct configfs_bin_attribute *bin_attrs[] = {
	&attr_bin_attr,
	NULL,
};

static ssize_t text_attr_show(struct config_item *item, char *buf)
{
	return attr_read(&text_attr, buf, strlen(buf));
}

static ssize_t text_attr_store(struct config_item *item, const char *buf,
			       size_t size)
{
	return attr_write(&text_attr, buf, size);
}

CONFIGFS_ATTR(, text_attr);

static struct configfs_attribute *text_attrs[] = {
	&attr_text_attr,
	NULL,
};

static const struct config_item_type test_configfs_type = {
	.ct_owner	= THIS_MODULE,
	.ct_bin_attrs	= bin_attrs,
	.ct_attrs	= text_attrs,
};

/*
 * Return the file mode if @path exists or an error code if opening @path via
 * filp_open() in read-only mode failed.
 */
int get_file_mode(const char *path)
{
	struct file *file;
	int res;

	file = filp_open(path, O_RDONLY, 0400);
	if (IS_ERR(file)) {
		res = PTR_ERR(file);
		goto out;
	}
	res = file_inode(file)->i_mode;
	filp_close(file, NULL);

out:
	return res;
}

static int mkdir(const char *name, umode_t mode)
{
	struct dentry *dentry;
	struct path path;
	int err;

	err = get_file_mode(name);
	if (err >= 0 && S_ISDIR(err))
		return 0;

	dentry = kern_path_create(AT_FDCWD, name, &path, LOOKUP_DIRECTORY);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);

	err = vfs_mkdir(&init_user_ns, d_inode(path.dentry), dentry, mode);
	done_path_create(&path, dentry);

	return err;
}

static int mount_configfs(void)
{
	int res;

	res = get_file_mode("/sys/kernel/config/unit-test");
	if (res >= 0)
		return 0;
	res = mkdir("/sys", 0755);
	if (res < 0)
		return res;
	res = mkdir("/sys/kernel", 0755);
	if (res < 0)
		return res;
	res = mkdir("/sys/kernel/config", 0755);
	if (res < 0)
		return res;
	pr_info("mounting configfs ...\n");
	res = do_mount("", "/sys/kernel/config", "configfs", 0, NULL);
	if (res < 0)
		pr_err("mounting configfs failed: %d\n", res);
	else
		pr_info("mounted configfs.\n");
	return res;
}

static void unmount_configfs(void)
{
	/* How to unmount a filesystem from kernel code? */
}

#define KUNIT_EXPECT_MODE(test, left_arg, mask, right)			\
({									\
	const int left = (left_arg);					\
									\
	KUNIT_EXPECT_TRUE_MSG(test, left >= 0 && (left & mask) == right, \
		"(" #left_arg "(%d) & " #mask ") != " #right, left);	\
})

static void configfs_mounted(struct kunit *test)
{
	KUNIT_EXPECT_MODE(test, get_file_mode("/"), 0500, 0500);
	KUNIT_EXPECT_MODE(test, get_file_mode("/sys"), 0500, 0500);
	KUNIT_EXPECT_MODE(test, get_file_mode("/sys/kernel"), 0500, 0500);
	KUNIT_EXPECT_MODE(test, get_file_mode("/sys/kernel/config"), 0500, 0500);
	KUNIT_EXPECT_MODE(test, get_file_mode("/sys/kernel/config/unit-test"),
			  0500, 0500);
	KUNIT_EXPECT_MODE(test, get_file_mode
			  ("/sys/kernel/config/unit-test/text_attr"),
			  0700, 0600);
}

static void configfs_text_attr(struct kunit *test)
{
	struct file *f = filp_open("/sys/kernel/config/unit-test/text_attr",
				   O_RDWR, 0);
	static const char text1[] =
		"The quick brown fox jumps over the lazy dog";
	const int off1 = 0;
	const int len1 = strlen(text1);
	static const char text2[] = "huge";
	const int off2 = strlen(text1) - strlen(text2) - 4;
	const int len2 = strlen(text2);
	char text3[sizeof(text1)];
	int res;
	loff_t pos;

	KUNIT_EXPECT_EQ(test, PTR_ERR_OR_ZERO(f), 0);
	if (IS_ERR(f))
		return;
	/* Write at a non-zero offset. */
	pos = off2;
	res = kernel_write(f, text2, len2, &pos);
	KUNIT_EXPECT_EQ(test, res, len2);
	KUNIT_EXPECT_EQ(test, pos, off2 + len2);
	/* Verify the effect of the above kernel_write() call. */
	pos = 0;
	res = kernel_read(f, text3, sizeof(text3), &pos);
	KUNIT_EXPECT_EQ(test, res, len2);
	KUNIT_EXPECT_EQ(test, pos, len2);
	if (res >= 0) {
		text3[res] = '\0';
		KUNIT_EXPECT_STREQ(test, text3, text2);
	}
	/* Write at offset zero. */
	pos = off1;
	res = kernel_write(f, text1, len1, &pos);
	KUNIT_EXPECT_EQ(test, res, len1);
	KUNIT_EXPECT_EQ(test, pos, len1);
	/* Verify the effect of the above kernel_write() call. */
	pos = 0;
	res = kernel_read(f, text3, sizeof(text3), &pos);
	KUNIT_EXPECT_EQ(test, res, len1);
	KUNIT_EXPECT_EQ(test, pos, len1);
	if (res >= 0) {
		text3[res] = '\0';
		KUNIT_EXPECT_STREQ(test, text3, text1);
	}
	/* Write at a non-zero offset. */
	pos = off2;
	res = kernel_write(f, text2, len2, &pos);
	KUNIT_EXPECT_EQ(test, res, len2);
	KUNIT_EXPECT_EQ(test, pos, off2 + len2);
	/* Verify that the above kernel_write() call truncated the attribute. */
	pos = 0;
	res = kernel_read(f, text3, sizeof(text3), &pos);
	KUNIT_EXPECT_EQ(test, res, len2);
	KUNIT_EXPECT_EQ(test, pos, len2);
	if (res >= 0) {
		text3[res] = '\0';
		KUNIT_EXPECT_STREQ(test, text3, text2);
	}
	/* Read from offset 1. */
	pos = 1;
	res = kernel_read(f, text3, sizeof(text3), &pos);
	KUNIT_EXPECT_EQ(test, res, len2 - 1);
	KUNIT_EXPECT_EQ(test, pos, len2);
	if (res >= 0) {
		text3[res] = '\0';
		KUNIT_EXPECT_STREQ(test, text3, text2 + 1);
	}
	/* Write at offset -1. */
	pos = -1;
	res = kernel_write(f, text1, len1, &pos);
	KUNIT_EXPECT_EQ(test, res, -EINVAL);
	/* Write at the largest possible positive offset. */
	pos = LLONG_MAX - len1;
	res = kernel_write(f, text1, len1, &pos);
	KUNIT_EXPECT_EQ(test, res, len1);
	/* Read from offset -1. */
	pos = -1;
	res = kernel_read(f, text3, sizeof(text3), &pos);
	KUNIT_EXPECT_EQ(test, res, -EINVAL);
	/* Read from the largest possible positive offset. */
	pos = LLONG_MAX - sizeof(text3);
	res = kernel_read(f, text3, sizeof(text3), &pos);
	KUNIT_EXPECT_EQ(test, res, 0);
	/* Verify the effect of the latest kernel_write() call. */
	pos = 0;
	res = kernel_read(f, text3, sizeof(text3), &pos);
	KUNIT_EXPECT_EQ(test, res, len1);
	KUNIT_EXPECT_EQ(test, pos, len1);
	if (res >= 0) {
		text3[res] = '\0';
		KUNIT_EXPECT_STREQ(test, text3, text1);
	}
	filp_close(f, NULL);
}

#define KUNIT_EXPECT_MEMEQ(test, left, right, len)			\
	KUNIT_EXPECT_TRUE_MSG(test, memcmp(left, right, len) == 0,	\
			      #left " != " #right ": %.*s <> %.*s",	\
			      (int)len, left, (int)len, right)

static void configfs_bin_attr(struct kunit *test)
{
	struct file *f = filp_open("/sys/kernel/config/unit-test/bin_attr",
				   O_RDWR, 0);
	static const u8 data1[] =
		"\xff\x00The quick brown fox jumps over the lazy dog";
	const int off1 = 0;
	const int len1 = sizeof(data1) - 1;
	static const u8 data2[] = "huge";
	const int off2 = len1 - strlen(data2) - 4;
	const int len2 = strlen(data2);
	u8 data3[sizeof(data1)];
	int res;
	loff_t pos;

	bin_attr.nbytes = len1;

	KUNIT_EXPECT_EQ(test, PTR_ERR_OR_ZERO(f), 0);
	if (IS_ERR(f))
		return;
	/* Write at offset zero. */
	pos = off1;
	res = kernel_write(f, data1, len1, &pos);
	KUNIT_EXPECT_EQ(test, res, len1);
	KUNIT_EXPECT_EQ(test, pos, off1 + len1);
	/* Write at a non-zero offset. */
	pos = off2;
	res = kernel_write(f, data2, len2, &pos);
	KUNIT_EXPECT_EQ(test, res, len2);
	KUNIT_EXPECT_EQ(test, pos, off2 + len2);
	filp_close(f, NULL);

	/*
	 * buffer->bin_attr->write() is called from inside
	 * configfs_release_bin_file() and the latter function is
	 * called asynchronously. Hence the down() calls below to wait
	 * until the write method has been called.
	 */
	down(&bin_attr_written);
	down(&bin_attr_written);

	f = filp_open("/sys/kernel/config/unit-test/bin_attr", O_RDONLY, 0);
	KUNIT_EXPECT_EQ(test, PTR_ERR_OR_ZERO(f), 0);
	if (IS_ERR(f))
		return;
	/* Verify the effect of the two kernel_write() calls. */
	pos = 0;
	res = kernel_read(f, data3, sizeof(data3), &pos);
	KUNIT_EXPECT_EQ(test, res, len1);
	KUNIT_EXPECT_EQ(test, pos, len1);
	if (res >= 0) {
		data3[res] = '\0';
		KUNIT_EXPECT_MEMEQ(test, data3,
			"\xff\x00The quick brown fox jumps over the huge dog",
			len1);
	}
	/* Read from offset 1. */
	pos = 1;
	res = kernel_read(f, data3, sizeof(data3), &pos);
	KUNIT_EXPECT_EQ(test, res, len1 - 1);
	KUNIT_EXPECT_EQ(test, pos, len1);
	if (res >= 0) {
		data3[res] = '\0';
		KUNIT_EXPECT_MEMEQ(test, data3,
			"\x00The quick brown fox jumps over the huge dog",
			len1 - 1);
	}
	filp_close(f, NULL);

	f = filp_open("/sys/kernel/config/unit-test/bin_attr", O_RDWR, 0);
	KUNIT_EXPECT_EQ(test, PTR_ERR_OR_ZERO(f), 0);
	if (IS_ERR(f))
		return;
	/* Write at offset -1. */
	pos = -1;
	res = kernel_write(f, data1, len1, &pos);
	KUNIT_EXPECT_EQ(test, res, -EINVAL);
	/* Write at the largest possible positive offset. */
	pos = LLONG_MAX - len1;
	res = kernel_write(f, data1, len1, &pos);
	KUNIT_EXPECT_EQ(test, res, -EFBIG);
	filp_close(f, NULL);

	/* Wait until the .write() function has been called. */
	down(&bin_attr_written);

	KUNIT_EXPECT_EQ(test, bin_attr.nbytes, 0);

	f = filp_open("/sys/kernel/config/unit-test/bin_attr", O_RDONLY, 0);
	KUNIT_EXPECT_EQ(test, PTR_ERR_OR_ZERO(f), 0);
	if (IS_ERR(f))
		return;
	/* Read from offset -1. */
	pos = -1;
	res = kernel_read(f, data3, sizeof(data3), &pos);
	KUNIT_EXPECT_EQ(test, res, -EINVAL);
	/* Read from the largest possible positive offset. */
	pos = LLONG_MAX - sizeof(data3);
	res = kernel_read(f, data3, sizeof(data3), &pos);
	KUNIT_EXPECT_EQ(test, res, 0);
	KUNIT_EXPECT_EQ(test, pos, LLONG_MAX - sizeof(data3));
	/* Read from offset zero. */
	pos = 0;
	res = kernel_read(f, data3, sizeof(data3), &pos);
	KUNIT_EXPECT_EQ(test, res, 0);
	KUNIT_EXPECT_EQ(test, pos, 0);
	filp_close(f, NULL);
}

static struct kunit_case configfs_test_cases[] = {
	KUNIT_CASE(configfs_mounted),
	KUNIT_CASE(configfs_text_attr),
	KUNIT_CASE(configfs_bin_attr),
	{},
};

static struct configfs_subsystem test_subsys = {
	.su_group = {
		.cg_item = {
			.ci_namebuf = "unit-test",
			.ci_type    = &test_configfs_type,
		}
	},
};

static int configfs_suite_init(void)
{
	int res;

	config_group_init(&test_subsys.su_group);
	mutex_init(&test_subsys.su_mutex);
	res = configfs_register_subsystem(&test_subsys);
	if (res < 0) {
		pr_err("Registration of configfs subsystem failed: %d\n", res);
		return res;
	}
	return mount_configfs();
}

static void configfs_suite_exit(void)
{
	configfs_unregister_subsystem(&test_subsys);
	unmount_configfs();
}

static struct kunit_suite configfs_test_module = {
	.name		= "configfs unit tests",
	.init_suite	= configfs_suite_init,
	.exit_suite	= configfs_suite_exit,
	.test_cases	= configfs_test_cases,
};
kunit_test_suites(&configfs_test_module);

MODULE_LICENSE("GPL v2");
