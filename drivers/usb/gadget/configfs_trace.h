/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM configfs_gadget

#if !defined(__CONFIGFS_GADGET_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define __CONFIGFS_GADGET_TRACE_H

#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(log_gadget_info,
	TP_PROTO(struct gadget_info *gi),
	TP_ARGS(gi),
	TP_STRUCT__entry(
		__string(gi_group, config_item_name(&gi->group.cg_item))
		__field(bool, unbind)
		__field(bool, use_os_desc)
		__field(char, b_vendor_code)
		__array(char, qw_sign, OS_STRING_QW_SIGN_LEN)
		__field(bool, suspended)
		__field(bool, setup_pending)
		__field(bool, os_desc_pending)
		__field(unsigned int, deactivations)
		__field(int, delayed_status)
		__field(u16, bcdUSB)
		__field(u8, bDeviceClass)
		__field(u8, bDeviceSubClass)
		__field(u8, bDeviceProtocol)
		__field(u8, bMaxPacketSize0)
		__field(u16, idVendor)
		__field(u16, idProduct)
		__field(u16, bcdDevice)
		__field(unsigned int, max_speed)
		__field(bool, needs_serial)
		__string(udc_name, gi->composite.gadget_driver.udc_name)
	),
	TP_fast_assign(
		__assign_str(gi_group, config_item_name(&gi->group.cg_item));
		__entry->unbind = gi->unbind;
		__entry->use_os_desc = gi->use_os_desc;
		__entry->b_vendor_code = gi->b_vendor_code;
		memcpy(__entry->qw_sign, gi->qw_sign, OS_STRING_QW_SIGN_LEN);
		__entry->suspended = gi->cdev.suspended;
		__entry->setup_pending = gi->cdev.setup_pending;
		__entry->os_desc_pending = gi->cdev.os_desc_pending;
		__entry->deactivations = gi->cdev.deactivations;
		__entry->delayed_status = gi->cdev.delayed_status;
		__entry->bcdUSB = le16_to_cpu(gi->cdev.desc.bcdUSB);
		__entry->bDeviceClass = gi->cdev.desc.bDeviceClass;
		__entry->bDeviceSubClass = gi->cdev.desc.bDeviceSubClass;
		__entry->bDeviceProtocol = gi->cdev.desc.bDeviceProtocol;
		__entry->bMaxPacketSize0 = gi->cdev.desc.bMaxPacketSize0;
		__entry->idVendor = le16_to_cpu(gi->cdev.desc.idVendor);
		__entry->idProduct = le16_to_cpu(gi->cdev.desc.idProduct);
		__entry->bcdDevice = le16_to_cpu(gi->cdev.desc.bcdDevice);
		__entry->max_speed = gi->composite.max_speed;
		__entry->needs_serial = gi->composite.needs_serial;
		__assign_str(udc_name, gi->composite.gadget_driver.udc_name);
	),
	TP_printk("%s: %s: %d %d %d %d %d %d %u %d %04x %02x %02x %02x %02x %04x %04x %04x %d %d %s",
		__get_str(gi_group),
		__get_str(udc_name),
		__entry->unbind,
		__entry->use_os_desc,
		__entry->b_vendor_code,
		__entry->suspended,
		__entry->setup_pending,
		__entry->os_desc_pending,
		__entry->deactivations,
		__entry->delayed_status,
		__entry->bcdUSB,
		__entry->bDeviceClass,
		__entry->bDeviceSubClass,
		__entry->bDeviceProtocol,
		__entry->bMaxPacketSize0,
		__entry->idVendor,
		__entry->idProduct,
		__entry->bcdDevice,
		__entry->max_speed,
		__entry->needs_serial,
		__print_hex_str(__entry->qw_sign, OS_STRING_QW_SIGN_LEN)
	)
);

DEFINE_EVENT(log_gadget_info, gadget_dev_desc_bDeviceClass_store,
	TP_PROTO(struct gadget_info *gi),
	TP_ARGS(gi)
);

DEFINE_EVENT(log_gadget_info, gadget_dev_desc_bDeviceSubClass_store,
	TP_PROTO(struct gadget_info *gi),
	TP_ARGS(gi)
);

DEFINE_EVENT(log_gadget_info, gadget_dev_desc_bDeviceProtocol_store,
	TP_PROTO(struct gadget_info *gi),
	TP_ARGS(gi)
);

DEFINE_EVENT(log_gadget_info, gadget_dev_desc_bMaxPacketSize0_store,
	TP_PROTO(struct gadget_info *gi),
	TP_ARGS(gi)
);

DEFINE_EVENT(log_gadget_info, gadget_dev_desc_idVendor_store,
	TP_PROTO(struct gadget_info *gi),
	TP_ARGS(gi)
);

DEFINE_EVENT(log_gadget_info, gadget_dev_desc_idProduct_store,
	TP_PROTO(struct gadget_info *gi),
	TP_ARGS(gi)
);

DEFINE_EVENT(log_gadget_info, gadget_dev_desc_bcdDevice_store,
	TP_PROTO(struct gadget_info *gi),
	TP_ARGS(gi)
);

DEFINE_EVENT(log_gadget_info, gadget_dev_desc_bcdUSB_store,
	TP_PROTO(struct gadget_info *gi),
	TP_ARGS(gi)
);

DEFINE_EVENT(log_gadget_info, unregister_gadget,
	TP_PROTO(struct gadget_info *gi),
	TP_ARGS(gi)
);

DEFINE_EVENT(log_gadget_info, gadget_dev_desc_UDC_store,
	TP_PROTO(struct gadget_info *gi),
	TP_ARGS(gi)
);

DEFINE_EVENT(log_gadget_info, gadget_dev_desc_max_speed_store,
	TP_PROTO(struct gadget_info *gi),
	TP_ARGS(gi)
);

DEFINE_EVENT(log_gadget_info, os_desc_use_store,
	TP_PROTO(struct gadget_info *gi),
	TP_ARGS(gi)
);

DEFINE_EVENT(log_gadget_info, os_desc_b_vendor_code_store,
	TP_PROTO(struct gadget_info *gi),
	TP_ARGS(gi)
);

DEFINE_EVENT(log_gadget_info, configfs_composite_bind,
	TP_PROTO(struct gadget_info *gi),
	TP_ARGS(gi)
);

DEFINE_EVENT(log_gadget_info, configfs_composite_unbind,
	TP_PROTO(struct gadget_info *gi),
	TP_ARGS(gi)
);

DEFINE_EVENT(log_gadget_info, configfs_composite_setup,
	TP_PROTO(struct gadget_info *gi),
	TP_ARGS(gi)
);

DEFINE_EVENT(log_gadget_info, configfs_composite_disconnect,
	TP_PROTO(struct gadget_info *gi),
	TP_ARGS(gi)
);

DEFINE_EVENT(log_gadget_info, configfs_composite_reset,
	TP_PROTO(struct gadget_info *gi),
	TP_ARGS(gi)
);

DEFINE_EVENT(log_gadget_info, configfs_composite_suspend,
	TP_PROTO(struct gadget_info *gi),
	TP_ARGS(gi)
);

DEFINE_EVENT(log_gadget_info, configfs_composite_resume,
	TP_PROTO(struct gadget_info *gi),
	TP_ARGS(gi)
);

DECLARE_EVENT_CLASS(log_config,
	TP_PROTO(struct config_usb_cfg *cfg),
	TP_ARGS(cfg),
	TP_STRUCT__entry(
		__string(gi_group, config_item_name(&cfg_to_gadget_info(cfg)->group.cg_item))
		__string(label, cfg->c.label)
		__field(u8, bConfigurationValue)
		__field(u8, bmAttributes)
		__field(u16, MaxPower)
	),
	TP_fast_assign(
		__assign_str(gi_group, config_item_name(&cfg_to_gadget_info(cfg)->group.cg_item));
		__assign_str(label, cfg->c.label);
		__entry->bConfigurationValue = cfg->c.bConfigurationValue;
		__entry->bmAttributes = cfg->c.bmAttributes;
		__entry->MaxPower = cfg->c.MaxPower;
	),
	TP_printk("%s: %s: %u %u %u",
		__get_str(gi_group),
		__get_str(label),
		__entry->bConfigurationValue,
		__entry->bmAttributes,
		__entry->MaxPower
	)
);

DEFINE_EVENT(log_config, gadget_config_desc_MaxPower_store,
	TP_PROTO(struct config_usb_cfg *cfg),
	TP_ARGS(cfg)
);

DEFINE_EVENT(log_config, gadget_config_desc_bmAttributes_store,
	TP_PROTO(struct config_usb_cfg *cfg),
	TP_ARGS(cfg)
);

DECLARE_EVENT_CLASS(log_config_function,
	TP_PROTO(struct config_usb_cfg *cfg, struct usb_function *f),
	TP_ARGS(cfg, f),
	TP_STRUCT__entry(
		__string(gi_group, config_item_name(&cfg_to_gadget_info(cfg)->group.cg_item))
		__string(label, cfg->c.label)
		__string(name, f->name)
		__field(u8, bConfigurationValue)
		__field(u8, bmAttributes)
		__field(u16, MaxPower)
	),
	TP_fast_assign(
		__assign_str(gi_group, config_item_name(&cfg_to_gadget_info(cfg)->group.cg_item));
		__assign_str(label, cfg->c.label);
		__assign_str(name, f->name);
		__entry->bConfigurationValue = cfg->c.bConfigurationValue;
		__entry->bmAttributes = cfg->c.bmAttributes;
		__entry->MaxPower = cfg->c.MaxPower;
	),
	TP_printk("%s: %s: %u %u %u %s",
		__get_str(gi_group),
		__get_str(label),
		__entry->bConfigurationValue,
		__entry->bmAttributes,
		__entry->MaxPower,
		__get_str(name)
	)
);

DEFINE_EVENT(log_config_function, config_usb_cfg_link,
	TP_PROTO(struct config_usb_cfg *cfg, struct usb_function *f),
	TP_ARGS(cfg, f)
);

DEFINE_EVENT(log_config_function, config_usb_cfg_unlink,
	TP_PROTO(struct config_usb_cfg *cfg, struct usb_function *f),
	TP_ARGS(cfg, f)
);

#endif /* __CONFIGFS_GADGET_TRACE_H */

/* this part has to be here */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/usb/gadget

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE configfs_trace

#include <trace/define_trace.h>
