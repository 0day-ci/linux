// SPDX-License-Identifier: GPL-2.0-or-later

#define pr_fmt(fmt) "%s:%s: " fmt, KBUILD_MODNAME, __func__

#include <linux/debugfs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/rbtree_augmented.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/types.h>

enum Cmd { RESET, INSERT, DELETE };

static struct dentry *rbt_if_root;
static struct rb_root rbt = RB_ROOT;
static u64 input_key;

struct data {
	struct rb_node node;
	u64 key;
};

#define data_from_node(from) (rb_entry(from, struct data, node))

#define print(...) seq_printf(m, __VA_ARGS__)
#define print_parent print(" (%llu,%s) ", \
		data_from_node(parent)->key, rb_is_red(parent) ? "Red" : "Black")

static int cmd_show(struct seq_file *m, void *p)
{
	struct rb_node *node = rbt.rb_node, *parent = NULL;
	bool left = false;
	while(true) {
		if (!node) {
			print("Leaf");

			if (!parent)
				break;

			if (left) {
				print_parent;
				node = parent->rb_right;
				left = false;
			} else {
				while (rb_parent(parent) && parent->rb_right == node) {
					print(")");
					node = parent;
					parent = rb_parent(node);
				}

				if (parent->rb_right == node)
					break;

				print_parent;
				node = parent->rb_right;
				left = false;
			}
		} else {
			if (parent)
				print("(");

			print("Node ");
			parent = node;
			node = node->rb_left;
			left = true;
		}

	}
	print("\n");
	return 0;
}

static int key_cmp(const void *_a, const struct rb_node *_b)
{
	u64 a = *(typeof(a) *) _a;
	u64 b = data_from_node(_b)->key;
	if (a < b)
		return -1;
	if (a > b)
		return 1;
	else
		return 0;
}

static int node_cmp(struct rb_node *a, const struct rb_node *b)
{
	return key_cmp(&data_from_node(a)->key, b);
}

ssize_t cmd_exec(struct file *file, const char __user *ubuf, size_t len, loff_t *offp)
{
	int cmd;
	struct data *data, *_n;
	struct rb_node *node;
	int ret = kstrtoint_from_user(ubuf, len, 10, &cmd);
	if (ret)
		return ret;

	switch (cmd) {
	case RESET:
		rbtree_postorder_for_each_entry_safe(data, _n, &rbt, node)
			kfree(data);
		rbt = RB_ROOT;
		break;
	case INSERT:
		data = kzalloc(sizeof(*data), GFP_KERNEL);
		data->key = input_key;
		rb_find_add(&data->node, &rbt, node_cmp);
		break;
	case DELETE:
		node = rb_find(&input_key, &rbt, key_cmp);
		if (!node)
			break;
		rb_erase(node, &rbt);
		kfree(data_from_node(node));
		break;
	default:
		return -EINVAL;
	}
	return len;
}

static int cmd_open(struct inode *inode, struct file *file)
{
	return single_open(file, cmd_show, inode->i_private);
}

static const struct file_operations cmd_fops = {
	.owner		= THIS_MODULE,
	.open		= cmd_open,
	.read		= seq_read,
	.write		= cmd_exec,
	.llseek		= seq_lseek,
	.release	= single_release,
};

int __init rbt_if_init(void)
{
	rbt_if_root = debugfs_create_dir("rbt_if", NULL);
	if (IS_ERR(rbt_if_root))
		return PTR_ERR(rbt_if_root);

	debugfs_create_file("cmd", 0644, rbt_if_root, NULL, &cmd_fops);
	debugfs_create_u64("key", 0644, rbt_if_root, &input_key);
	return 0;
}

void __exit rbt_if_exit(void)
{
	struct data *_n, *pos;
	debugfs_remove_recursive(rbt_if_root);
	rbtree_postorder_for_each_entry_safe(pos, _n, &rbt, node)
		kfree(pos);

}

module_init(rbt_if_init);
module_exit(rbt_if_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mete Polat <metepolat2000@gmail.com>");
