/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021 pureLiFi
 */

#ifndef _PURELIFI_USB_H
#define _PURELIFI_USB_H

#include <linux/completion.h>
#include <linux/netdevice.h>
#include <linux/spinlock.h>
#include <linux/skbuff.h>
#include <linux/usb.h>

#include "intf.h"

#define USB_BULK_MSG_TIMEOUT_MS 2000

#define PURELIFI_X_VENDOR_ID_0   0x16C1
#define PURELIFI_X_PRODUCT_ID_0  0x1CDE
#define PURELIFI_XC_VENDOR_ID_0  0x2EF5
#define PURELIFI_XC_PRODUCT_ID_0 0x0008
#define PURELIFI_XL_VENDOR_ID_0  0x2EF5
#define PURELIFI_XL_PRODUCT_ID_0 0x000A /* Station */

#define PLF_FPGA_STATUS_LEN 2
#define PLF_FPGA_STATE_LEN 9
#define PLF_BULK_TLEN 16384
#define PLF_FPGA_MG 6 /* Magic check */
#define PLF_XL_BUF_LEN 64

#define PLF_USB_TIMEOUT 1000
#define PLF_MSLEEP_TIME 200

#define PURELIFI_URB_RETRY_MAX 5

#define purelifi_usb_dev(usb) (&(usb)->intf->dev)

int plf_usb_wreq(const u8 *buffer, int buffer_len,
		 enum usb_req_enum_t usb_req_id);
void tx_urb_complete(struct urb *urb);

enum {
	USB_MAX_RX_SIZE       = 4800,
	USB_MAX_EP_INT_BUFFER = 64,
};

/* USB interrupt */
struct purelifi_usb_interrupt {
	spinlock_t lock;/* spin lock for usb interrupt buffer */
	struct urb *urb;
	void *buffer;
	int interval;
};

#define RX_URBS_COUNT 5

struct purelifi_usb_rx {
	spinlock_t lock;/* spin lock for rx urb */
	struct mutex setup_mutex; /* mutex lockt for rx urb */
	u8 fragment[2 * USB_MAX_RX_SIZE];
	unsigned int fragment_length;
	unsigned int usb_packet_size;
	struct urb **urbs;
	int urbs_count;
};

struct station_t {
   //  7...3    |    2      |     1     |     0	    |
   // Reserved  | Heartbeat | FIFO full | Connected |
	unsigned char flag;
	unsigned char mac[ETH_ALEN];
	struct sk_buff_head data_list;
};

struct firmware_file {
	u32 total_files;
	u32 total_size;
	u32 size;
	u32 start_addr;
	u32 control_packets;
} __packed;

#define STATION_CONNECTED_FLAG 0x1
#define STATION_FIFO_FULL_FLAG 0x2
#define STATION_HEARTBEAT_FLAG 0x4

#define PURELIFI_SERIAL_LEN 256
/**
 * struct purelifi_usb_tx - structure used for transmitting frames
 * @enabled: atomic enabled flag, indicates whether tx is enabled
 * @lock: lock for transmission
 * @submitted: anchor for URBs sent to device
 * @submitted_urbs: atomic integer that counts the URBs having sent to the
 *   device, which haven't been completed
 * @stopped: indicates whether higher level tx queues are stopped
 */
#define STA_BROADCAST_INDEX (AP_USER_LIMIT)
#define MAX_STA_NUM         (AP_USER_LIMIT + 1)
struct purelifi_usb_tx {
	atomic_t enabled;
	spinlock_t lock; /*spinlock for USB tx */
	u8 mac_fifo_full;
	struct sk_buff_head submitted_skbs;
	struct usb_anchor submitted;
	int submitted_urbs;
	u8 stopped:1;
	struct timer_list tx_retry_timer;
	struct station_t station[MAX_STA_NUM];
};

/* Contains the usb parts. The structure doesn't require a lock because intf
 * will not be changed after initialization.
 */
struct purelifi_usb {
	struct timer_list sta_queue_cleanup;
	struct purelifi_usb_rx rx;
	struct purelifi_usb_tx tx;
	struct usb_interface *intf;
	u8 req_buf[64]; /* purelifi_usb_iowrite16v needs 62 bytes */
	bool rx_usb_enabled;
	bool initialized;
	bool was_running;
};

enum endpoints {
	EP_DATA_IN  = 2,
	EP_DATA_OUT = 8,
};

enum devicetype {
	DEVICE_LIFI_X  = 0,
	DEVICE_LIFI_XC  = 1,
	DEVICE_LIFI_XL  = 1,
};

int plf_usb_wreq_async(struct purelifi_usb *usb, const u8 *buffer,
		       int buffer_len, enum usb_req_enum_t usb_req_id,
		       usb_complete_t complete_fn, void *context);

static inline struct usb_device *
purelifi_usb_to_usbdev(struct purelifi_usb *usb)
{
	return interface_to_usbdev(usb->intf);
}

static inline struct ieee80211_hw *
purelifi_intf_to_hw(struct usb_interface *intf)
{
	return usb_get_intfdata(intf);
}

static inline struct ieee80211_hw *
purelifi_usb_to_hw(struct purelifi_usb *usb)
{
	return purelifi_intf_to_hw(usb->intf);
}

void purelifi_usb_init(struct purelifi_usb *usb, struct ieee80211_hw *hw,
		       struct usb_interface *intf);
void purelifi_send_packet_from_data_queue(struct purelifi_usb *usb);
void purelifi_usb_release(struct purelifi_usb *usb);
void purelifi_usb_disable_rx(struct purelifi_usb *usb);
void purelifi_usb_enable_tx(struct purelifi_usb *usb);
void purelifi_usb_disable_tx(struct purelifi_usb *usb);
int purelifi_usb_tx(struct purelifi_usb *usb, struct sk_buff *skb);
int purelifi_usb_enable_rx(struct purelifi_usb *usb);
int purelifi_usb_init_hw(struct purelifi_usb *usb);
const char *purelifi_speed(enum usb_device_speed speed);

/*Firmware declarations */
int download_xl_firmware(struct usb_interface *intf);
int download_fpga(struct usb_interface *intf);

int upload_mac_and_serial(struct usb_interface *intf,
			  unsigned char *hw_address,
			  unsigned char *serial_number);

#endif
