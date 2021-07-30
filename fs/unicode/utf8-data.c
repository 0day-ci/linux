// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/kernel.h>
#include "utf8n.h"

#define __INCLUDED_FROM_UTF8NORM_C__
#include "utf8data.h"
#undef __INCLUDED_FROM_UTF8NORM_C__

struct utf8_data ops = {
	.owner = THIS_MODULE,

	.utf8vers = utf8vers,

	.utf8agetab = utf8agetab,
	.utf8agetab_size = ARRAY_SIZE(utf8agetab),

	.utf8nfdicfdata = utf8nfdicfdata,
	.utf8nfdicfdata_size = ARRAY_SIZE(utf8nfdicfdata),

	.utf8nfdidata = utf8nfdidata,
	.utf8nfdidata_size = ARRAY_SIZE(utf8nfdidata),

	.utf8data = utf8data,
	.utf8data_size = ARRAY_SIZE(utf8data),
};

static int __init utf8_init(void)
{
	unicode_register(&ops);
	return 0;
}

static void __exit utf8_exit(void)
{
	unicode_unregister();
}

module_init(utf8_init);
module_exit(utf8_exit);

MODULE_LICENSE("GPL v2");
