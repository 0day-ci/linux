/* SPDX-License-Identifier: GPL-2.0 */
#ifndef USB__GADGET__CONFIGFS__H
#define USB__GADGET__CONFIGFS__H

#include <linux/configfs.h>

#define MAX_USB_STRING_LANGS 2

struct gadget_info {
	struct config_group group;
	struct config_group functions_group;
	struct config_group configs_group;
	struct config_group strings_group;
	struct config_group os_desc_group;

	struct mutex lock;
	struct usb_gadget_strings *gstrings[MAX_USB_STRING_LANGS + 1];
	struct list_head string_list;
	struct list_head available_func;

	struct usb_composite_driver composite;
	struct usb_composite_dev cdev;
	bool use_os_desc;
	char b_vendor_code;
	char qw_sign[OS_STRING_QW_SIGN_LEN];
	spinlock_t spinlock;
	bool unbind;
};

static inline struct gadget_info *to_gadget_info(struct config_item *item)
{
	return container_of(to_config_group(item), struct gadget_info, group);
}

struct config_usb_cfg {
	struct config_group group;
	struct config_group strings_group;
	struct list_head string_list;
	struct usb_configuration c;
	struct list_head func_list;
	struct usb_gadget_strings *gstrings[MAX_USB_STRING_LANGS + 1];
};

static inline struct config_usb_cfg *to_config_usb_cfg(struct config_item *item)
{
	return container_of(to_config_group(item), struct config_usb_cfg,
			group);
}

struct gadget_strings {
	struct usb_gadget_strings stringtab_dev;
	struct usb_string strings[USB_GADGET_FIRST_AVAIL_IDX];
	char *manufacturer;
	char *product;
	char *serialnumber;

	struct config_group group;
	struct list_head list;
};

struct os_desc {
	struct config_group group;
};

struct gadget_config_name {
	struct usb_gadget_strings stringtab_dev;
	struct usb_string strings;
	char *configuration;

	struct config_group group;
	struct list_head list;
};


void unregister_gadget_item(struct config_item *item);

struct config_group *usb_os_desc_prepare_interf_dir(
		struct config_group *parent,
		int n_interf,
		struct usb_os_desc **desc,
		char **names,
		struct module *owner);

static inline struct usb_os_desc *to_usb_os_desc(struct config_item *item)
{
	return container_of(to_config_group(item), struct usb_os_desc, group);
}

#endif /*  USB__GADGET__CONFIGFS__H */
