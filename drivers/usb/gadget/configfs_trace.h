// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM configfs_gadget

#if !defined(__GADGET_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define __GADGET_TRACE_H

#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(log_gadget_info,
	TP_PROTO(struct gadget_info *gi),
	TP_ARGS(gi),
	TP_STRUCT__entry(
		/* gadget_info */
		__string(gi_group, config_item_name(&gi->group.cg_item))
		__field(bool, unbind)
		__field(bool, use_os_desc)
		__field(char, b_vendor_code)

		/* usb_composite_dev */
		__field(bool, suspended)
		__field(bool, setup_pending)
		__field(bool, os_desc_pending)
		__field(unsigned, deactivations)
		__field(int, delayed_status)
		__field(__le16, bcdUSB)
		__field(__le16, bcdDevice)
		__string(config, gi->cdev.config)

		/* usb_composite_driver */
		__field(unsigned, max_speed)
		__field(bool, needs_serial)

		/* usb_gadget_driver */
		__string(udc_name, gi->composite.gadget_driver.udc_name)
	),
	TP_fast_assign(
		__assign_str(gi_group, config_item_name(&gi->group.cg_item));
		__entry->unbind = gi->unbind;
		__entry->use_os_desc = gi->use_os_desc;
		__entry->b_vendor_code = gi->b_vendor_code;

		__entry->suspended = gi->cdev.suspended;
		__entry->setup_pending = gi->cdev.setup_pending;
		__entry->os_desc_pending = gi->cdev.os_desc_pending;
		__entry->deactivations = gi->cdev.deactivations;
		__entry->delayed_status = gi->cdev.delayed_status;
		__entry->bcdUSB = le16_to_cpu(gi->cdev.desc.bcdUSB);
		__entry->bcdDevice = le16_to_cpu(gi->cdev.desc.bcdDevice);
		__assign_str(config, config_trace_string(gi));

		__entry->max_speed = gi->composite.max_speed;
		__entry->needs_serial = gi->composite.needs_serial;

		__assign_str(udc_name, gi->composite.gadget_driver.udc_name);
	),
	TP_printk("gi_group:%s,unbind:%d,use_os_desc:%d,b_vendor_code:%d,"
		" - "
		"suspended:%d,setup_pending:%d,"
		"os_desc_pending:%d,deactivations:%d,delayed_status:%d,"
		"bcdUSB:%04x,bcdDevice:%04x,config:%s,"
		" - "
		"max_speed:%d,needs_serial:%d,"
		" - "
		"udc_name:%s",

		__get_str(gi_group),
		__entry->unbind,
		__entry->use_os_desc,
		__entry->b_vendor_code,

		__entry->suspended,
		__entry->setup_pending,
		__entry->os_desc_pending,
		__entry->deactivations,
		__entry->delayed_status,
		__entry->bcdUSB,
		__entry->bcdDevice,
		__get_str(config),

		__entry->max_speed,
		__entry->needs_serial,

		__get_str(udc_name)
		)
);

DEFINE_EVENT(log_gadget_info, gadget_dev_desc_bcdDevice_store,
	TP_PROTO(struct gadget_info *gi),
	TP_ARGS(gi)
);

DEFINE_EVENT(log_gadget_info, gadget_dev_desc_bcdUSB_store,
	TP_PROTO(struct gadget_info *gi),
	TP_ARGS(gi)
);

DEFINE_EVENT(log_gadget_info, gadget_dev_desc_UDC_show,
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

DEFINE_EVENT(log_gadget_info, gadget_dev_desc_max_speed_show,
	TP_PROTO(struct gadget_info *gi),
	TP_ARGS(gi)
);

DEFINE_EVENT(log_gadget_info, gadget_dev_desc_max_speed_store,
	TP_PROTO(struct gadget_info *gi),
	TP_ARGS(gi)
);

DEFINE_EVENT(log_gadget_info, config_usb_cfg_link,
	TP_PROTO(struct gadget_info *gi),
	TP_ARGS(gi)
);

DEFINE_EVENT(log_gadget_info, config_usb_cfg_unlink,
	TP_PROTO(struct gadget_info *gi),
	TP_ARGS(gi)
);

DEFINE_EVENT(log_gadget_info, gadget_config_desc_MaxPower_show,
	TP_PROTO(struct gadget_info *gi),
	TP_ARGS(gi)
);

DEFINE_EVENT(log_gadget_info, gadget_config_desc_MaxPower_store,
	TP_PROTO(struct gadget_info *gi),
	TP_ARGS(gi)
);

DEFINE_EVENT(log_gadget_info, gadget_config_desc_bmAttributes_show,
	TP_PROTO(struct gadget_info *gi),
	TP_ARGS(gi)
);

DEFINE_EVENT(log_gadget_info, gadget_config_desc_bmAttributes_store,
	TP_PROTO(struct gadget_info *gi),
	TP_ARGS(gi)
);

#endif /* __GADGET_TRACE_H */

/* this part has to be here */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/usb/gadget

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE configfs_trace

#include <trace/define_trace.h>
