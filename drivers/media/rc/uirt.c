// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * USB-UIRT
 *
 * Copyright (C) 2021 Sean Young <sean@mess.org>
 *
 * See http://www.usbuirt.com/USB-UIRT%20Command%20Protocol.doc
 */

#include <linux/completion.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/slab.h>
#include <linux/usb/input.h>
#include <linux/usb/ftdi_sio.h>

#include <media/rc-core.h>

static const u8 CMD_GET_VERSION[] = { 0x23, 0xdd };
static const u8 CMD_SETMODERAW[] = { 0x21, 0xdf };
static const u8 CMD_SETWIDEBAND[] = { 0x24, 0xdc };

#define WDR_TIMEOUT 5000 /* default urb timeout */
#define WDR_SHORT_TIMEOUT 1000	/* shorter urb timeout */
#define UNIT_US 50
#define IR_TIMEOUT 12500
#define MAX_PACKET 64

enum cmd_state {
	CMD_STATE_GET_VERSION,
	CMD_STATE_SET_MODE_RAW,
	CMD_STATE_SET_MODE_WIDEBAND,
	CMD_STATE_IRDATA,
	CMD_STATE_SHORT_TX,
	CMD_STATE_STREAMING_TX,
};

enum rx_state {
	RX_STATE_INTERSPACE_HIGH,
	RX_STATE_INTERSPACE_LOW,
	RX_STATE_ON_HIGH,
	RX_STATE_ON_LOW,
	RX_STATE_FREQ_HIGH,
	RX_STATE_FREQ_LOW,
	RX_STATE_OFF_HIGH,
	RX_STATE_OFF_LOW,
};

struct uirt {
	struct device *dev;
	struct usb_device *usbdev;

	struct rc_dev *rc;
	struct urb *urb_in, *urb_out;

	u8 *in;
	u8 *out;
	struct completion cmd_done;
	u8 freq;
	u8 high;
	bool wideband;
	u32 last_duration;

	enum cmd_state cmd_state;
	enum rx_state rx_state;

	void *tx_buf;
	uint tx_len;

	char phys[64];
};

// read IR in raw mode
static void uirt_raw_mode(struct uirt *uirt, u32 len)
{
	uint i, duration;

	for (i = 2; i < len; i++) {
		switch (uirt->rx_state) {
		case RX_STATE_INTERSPACE_HIGH:
			uirt->rx_state = RX_STATE_INTERSPACE_LOW;
			break;
		case RX_STATE_INTERSPACE_LOW:
			uirt->rx_state = RX_STATE_ON_HIGH;
			break;
		case RX_STATE_ON_HIGH:
			duration = uirt->in[i];
			if (duration == 0)
				duration = 1;

			ir_raw_event_store(uirt->rc, &((struct ir_raw_event) {
				.duration = duration * UNIT_US,
				.pulse = true,
			}));

			uirt->rx_state = RX_STATE_OFF_HIGH;
			break;
		case RX_STATE_OFF_HIGH:
			if (uirt->in[i] == 0xff) {
				ir_raw_event_store(uirt->rc, &((struct ir_raw_event) {
					.duration = IR_TIMEOUT,
					.timeout = true,
				}));
				uirt->rx_state = RX_STATE_INTERSPACE_HIGH;
				break;
			}

			duration = uirt->in[i];
			if (duration == 0)
				duration = 1;

			ir_raw_event_store(uirt->rc, &((struct ir_raw_event) {
				.duration = duration * UNIT_US,
				.pulse = false,
			}));
			uirt->rx_state = RX_STATE_ON_HIGH;
			break;
		default:
			WARN(1, "unreachable state");
			uirt->rx_state = RX_STATE_INTERSPACE_HIGH;
			break;
		}
	}

	ir_raw_event_handle(uirt->rc);
}

// read IR in wideband mode
static void uirt_wideband(struct uirt *uirt, u32 len)
{
	uint i, duration, carrier, pulses;

	for (i = 2; i < len; i++) {
		switch (uirt->rx_state) {
		case RX_STATE_INTERSPACE_HIGH:
			uirt->rx_state = RX_STATE_INTERSPACE_LOW;
			break;
		case RX_STATE_INTERSPACE_LOW:
			uirt->rx_state = RX_STATE_ON_HIGH;
			break;
		case RX_STATE_ON_HIGH:
			uirt->high = uirt->in[i];
			uirt->rx_state = RX_STATE_ON_LOW;
			break;
		case RX_STATE_ON_LOW:
			// duration is in 400ns units
			duration = (uirt->high << 8) | uirt->in[i];
			uirt->last_duration = duration;
			ir_raw_event_store(uirt->rc, &((struct ir_raw_event) {
				.duration = DIV_ROUND_CLOSEST(duration * 2, 5),
				.pulse = true,
			}));
			uirt->rx_state = RX_STATE_FREQ_HIGH;
			break;
		case RX_STATE_FREQ_HIGH:
			if (uirt->in[i] & 0x80) {
				uirt->high = uirt->in[i] & 0x7f;
				uirt->rx_state = RX_STATE_FREQ_LOW;
			} else if (!uirt->in[i]) {
				uirt->rx_state = RX_STATE_OFF_HIGH;
			} else {
				dev_dbg(uirt->dev, "carrier duration2 %u pulses %u",
					uirt->last_duration, uirt->in[i]);

				carrier = DIV_ROUND_CLOSEST_ULL(uirt->in[i] * 10000000ull,
								uirt->last_duration * 4);
				ir_raw_event_store(uirt->rc, &((struct ir_raw_event) {
					.carrier = carrier,
					.carrier_report = true,
				}));

				uirt->rx_state = RX_STATE_OFF_HIGH;
			}
			break;
		case RX_STATE_FREQ_LOW:
			pulses = (uirt->high << 8) | uirt->in[i];
			if (pulses) {
				dev_dbg(uirt->dev, "carrier duration %u pulses %u",
					uirt->last_duration, pulses);

				carrier = DIV_ROUND_CLOSEST_ULL(pulses * 10000000ull,
								uirt->last_duration * 4);
				ir_raw_event_store(uirt->rc, &((struct ir_raw_event) {
					.carrier = carrier,
					.carrier_report = true,
				}));
			}
			uirt->rx_state = RX_STATE_OFF_HIGH;
			break;
		case RX_STATE_OFF_HIGH:
			if (uirt->in[i] == 0xff) {
				ir_raw_event_store(uirt->rc, &((struct ir_raw_event) {
					.duration = IR_TIMEOUT,
					.timeout = true,
				}));
				uirt->rx_state = RX_STATE_INTERSPACE_HIGH;
			} else {
				uirt->high = uirt->in[i];
				uirt->rx_state = RX_STATE_OFF_LOW;
			}
			break;
		case RX_STATE_OFF_LOW:
			// duration is in 400ns units
			duration = (uirt->high << 8) | uirt->in[i];
			ir_raw_event_store(uirt->rc, &((struct ir_raw_event) {
				.duration = DIV_ROUND_CLOSEST(duration * 2, 5),
				.pulse = false,
			}));
			uirt->rx_state = RX_STATE_ON_HIGH;
			break;
		}
	}

	ir_raw_event_handle(uirt->rc);
}

static void uirt_response(struct uirt *uirt, u32 len)
{
	int i;

	dev_dbg(uirt->dev, "state:%d data: %*phN\n", uirt->cmd_state, len, uirt->in);

	// Do we have more IR to transmit
	if (uirt->cmd_state == CMD_STATE_STREAMING_TX && len >= 2 &&
	    uirt->tx_len && uirt->in[0] & FTDI_RS0_CTS) {
		u32 len;
		int err;

		len = min_t(u32, uirt->tx_len, MAX_PACKET);

		memcpy(uirt->out, uirt->tx_buf, len);
		uirt->urb_out->transfer_buffer_length = len;

		uirt->tx_len -= len;
		uirt->tx_buf += len;

		err = usb_submit_urb(uirt->urb_out, GFP_ATOMIC);
		if (err != 0)
			dev_warn(uirt->dev,
				 "failed to submit out urb: %d\n", err);
	}

	// if we only have two bytes, it just gives us the serial line status
	if (len <= 2)
		return;

	switch (uirt->cmd_state) {
	case CMD_STATE_GET_VERSION:
		if (len == 10) {
			// check checksum
			u8 checksum = 0;

			for (i = 2; i < len; i++)
				checksum += uirt->in[i];

			if (checksum != 0) {
				dev_err(uirt->dev, "checksum does not match: %*phN\n",
					len, uirt->in);
				return;
			}

			dev_info(uirt->dev,
				 "USB-UIRT firmware v%u.%u protocol v%u.%u %02u-%02u-%04u",
				 uirt->in[2], uirt->in[3], uirt->in[4],
				 uirt->in[5], uirt->in[6], uirt->in[7],
				 2000 + uirt->in[8]);

			complete(&uirt->cmd_done);
			return;
		}
		break;
	case CMD_STATE_SHORT_TX:
	case CMD_STATE_STREAMING_TX:
	case CMD_STATE_SET_MODE_RAW:
	case CMD_STATE_SET_MODE_WIDEBAND:
		if (len == 3) {
			switch (uirt->in[2]) {
			case 0x20:
				// 0x20 transmitting is expected during tx
				if (uirt->cmd_state == CMD_STATE_STREAMING_TX)
					return;

				if (uirt->cmd_state == CMD_STATE_SHORT_TX)
					complete(&uirt->cmd_done);
				else
					dev_err(uirt->dev, "device transmitting");
				break;
			case 0x21:
				if (uirt->tx_len) {
					dev_err(uirt->dev, "tx completed with %u left to send",
						uirt->tx_len);
				} else {
					if (uirt->cmd_state == CMD_STATE_SET_MODE_RAW)
						uirt->wideband = false;
					if (uirt->cmd_state == CMD_STATE_SET_MODE_WIDEBAND)
						uirt->wideband = true;

					complete(&uirt->cmd_done);
				}
				break;
			case 0x80:
				dev_err(uirt->dev, "checksum error");
				break;
			case 0x81:
				dev_err(uirt->dev, "timeout");
				break;
			case 0x82:
				dev_err(uirt->dev, "command error");
				break;
			default:
				dev_err(uirt->dev, "unknown response");
			}

			uirt->cmd_state = CMD_STATE_IRDATA;
			return;
		}
	default:
		break;
	}

	if (uirt->wideband)
		uirt_wideband(uirt, len);
	else
		uirt_raw_mode(uirt, len);
}

static void uirt_out_callback(struct urb *urb)
{
	struct uirt *uirt = urb->context;

	if (urb->status)
		dev_warn(uirt->dev, "out urb status: %d\n", urb->status);
}

static void uirt_in_callback(struct urb *urb)
{
	struct uirt *uirt = urb->context;
	int ret;

	if (urb->status == 0)
		uirt_response(uirt, urb->actual_length);
	else
		dev_dbg(uirt->dev, "in urb status: %d\n", urb->status);

	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret && ret != -ENODEV)
		dev_warn(uirt->dev, "failed to resubmit urb: %d\n", ret);
}

static int uirt_command(struct uirt *uirt, const u8 *cmd, int cmd_len,
			enum cmd_state state)
{
	int err;

	init_completion(&uirt->cmd_done);

	uirt->cmd_state = state;

	memcpy(uirt->out, cmd, cmd_len);
	uirt->urb_out->transfer_buffer_length = cmd_len;

	err = usb_submit_urb(uirt->urb_out, GFP_KERNEL);
	if (err != 0) {
		uirt->cmd_state = CMD_STATE_IRDATA;
		return err;
	}

	if (!wait_for_completion_timeout(&uirt->cmd_done,
					 msecs_to_jiffies(WDR_SHORT_TIMEOUT))) {
		usb_kill_urb(uirt->urb_out);
		uirt->cmd_state = CMD_STATE_IRDATA;
		return -ETIMEDOUT;
	}

	return 0;
}

static int uirt_setup(struct uirt *uirt)
{
	int err;

	err = uirt_command(uirt, CMD_SETMODERAW, sizeof(CMD_SETMODERAW),
			   CMD_STATE_SET_MODE_RAW);
	if (err) {
		dev_err(uirt->dev, "could not send set mode raw command: %d\n",
			err);
		return err;
	}

	err = uirt_command(uirt, CMD_GET_VERSION,
			   sizeof(CMD_GET_VERSION), CMD_STATE_GET_VERSION);
	if (err != 0) {
		dev_err(uirt->dev, "could not send get version command: %d\n",
			err);
		return err;
	}

	return 0;
}

// IR TX when the data can fit into a single packet (i.e 64 bytes).
// count must be no larger than 24, else we might overflow the buffer.
static int uirt_short_tx(struct rc_dev *rc, uint *txbuf, uint count)
{
	struct uirt *uirt = rc->priv;
	u8 *out, checksum;
	u32 i, dest, width, freq;
	int err;

	out = kzalloc(MAX_PACKET, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	out[0] = 0x36; // DOTXRAW
	out[2] = uirt->freq; // carrier frequency
	out[3] = 1; // number of repeats

	dest = 7;

	freq = uirt->freq & 0x7f;

	for (i = 0; i < count; i++) {
		// width = (us / freq) * 2.5
		width = DIV_ROUND_CLOSEST(txbuf[i] * 5, freq * 2);

		if (width == 0)
			width = 1;
		else if (width > 127)
			out[dest++] = (width >> 8) | 0x80;

		out[dest++] = width;
	}

	// length of RAWSTRUCT + 1
	out[1] = dest - 1;
	// number of bytes in encoded pulse/space
	out[6] = dest - 7;

	// checksum
	for (i = 0, checksum = 0; i < dest; i++)
		checksum -= out[i];

	out[dest++] = checksum;

	uirt->tx_buf = NULL;
	uirt->tx_len = 0;

	err = uirt_command(uirt, out, dest, CMD_STATE_SHORT_TX);
	kfree(out);
	if (err != 0)
		return err;

	return count;
}

static int uirt_tx(struct rc_dev *rc, uint *txbuf, uint count)
{
	struct uirt *uirt = rc->priv;
	u8 *out;
	u32 i, dest, unit_raw, freq, len;
	int err;

	// streaming tx does not work for short IR; use non-streaming
	// tx for short IR
	if (count <= 24)
		return uirt_short_tx(rc, txbuf, count);

	out = kmalloc(count * 2 + 3, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	out[0] = 0x25; // Streaming Transmit
	out[1] = 0xdb; // checksum
	out[2] = uirt->freq; // carrier frequency

	dest = 3;

	freq = uirt->freq & 0x7f;

	for (i = 0; i < count; i++) {
		// width = (us / freq) * 2.5
		unit_raw = DIV_ROUND_CLOSEST(txbuf[i] * 5, freq * 2);

		if (unit_raw == 0)
			unit_raw = 1;
		else if (unit_raw > 127)
			out[dest++] = (unit_raw >> 8) | 0x80;

		out[dest++] = unit_raw;
	}

	len = min_t(u32, dest, MAX_PACKET);

	uirt->tx_buf = out + len;
	uirt->tx_len = dest - len;

	err = uirt_command(uirt, out, len, CMD_STATE_STREAMING_TX);
	kfree(out);
	if (err != 0)
		return err;

	return count;
}

static int uirt_set_tx_carrier(struct rc_dev *dev, uint32_t carrier)
{
	struct uirt *uirt = dev->priv;

	if (carrier == 0)
		uirt->freq = 0xc0;
	else if (carrier >= 20000 && carrier <= 500000)
		uirt->freq = 2500000 / carrier;
	else
		return -EINVAL;

	return 0;
}

static int uirt_set_rx_wideband(struct rc_dev *dev, int enable)
{
	struct uirt *uirt = dev->priv;
	int err;

	if (enable)
		err = uirt_command(uirt, CMD_SETWIDEBAND,
				   sizeof(CMD_SETWIDEBAND),
				   CMD_STATE_SET_MODE_WIDEBAND);
	else
		err = uirt_command(uirt, CMD_SETMODERAW,
				   sizeof(CMD_SETMODERAW),
				   CMD_STATE_SET_MODE_RAW);

	if (err) {
		dev_err(uirt->dev, "could not send set mode command: %d\n",
			err);
		return err;
	}

	return 0;
}

static int init_ftdi(struct usb_device *udev)
{
	int err;

	// set the baud rate
	err = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			      FTDI_SIO_SET_BAUDRATE_REQUEST,
			      FTDI_SIO_SET_BAUDRATE_REQUEST_TYPE,
			      0x4009, 0x0001,
			      NULL, 0, WDR_SHORT_TIMEOUT);
	if (err)
		return err;

	// enabling rts/cts flow control
	err = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			      FTDI_SIO_SET_FLOW_CTRL_REQUEST,
			      FTDI_SIO_SET_FLOW_CTRL_REQUEST_TYPE,
			      0, FTDI_SIO_RTS_CTS_HS,
			      NULL, 0, WDR_SHORT_TIMEOUT);
	if (err)
		return err;

	// Set latency in milliseconds. The USB-UIRT will generate a
	// urb every latency milliseconds (IR or not), so this should be
	// set as high as possible.
	//
	// The USB-UIRT has a IR timeout of 14ms (i.e. it sends the 0xff
	// byte after 14ms of IR silence. So if we set the timeout to a higher
	// value, the 0xff just gets sent in a separate packet, and there
	// is a small delay for the IR to be processed.
	return usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			      FTDI_SIO_SET_LATENCY_TIMER_REQUEST,
			      FTDI_SIO_SET_LATENCY_TIMER_REQUEST_TYPE,
			      50, 0,
			      NULL, 0, WDR_TIMEOUT);
}

static int uirt_probe(struct usb_interface *intf,
		      const struct usb_device_id *id)
{
	struct usb_host_interface *idesc = intf->cur_altsetting;
	struct usb_device *usbdev = interface_to_usbdev(intf);
	struct usb_endpoint_descriptor *ep_in = NULL;
	struct usb_endpoint_descriptor *ep_out = NULL;
	struct usb_endpoint_descriptor *ep = NULL;
	struct uirt *uirt;
	struct rc_dev *rc;
	struct urb *urb;
	int i, pipe, err = -ENOMEM;

	for (i = 0; i < idesc->desc.bNumEndpoints; i++) {
		ep = &idesc->endpoint[i].desc;

		if (!ep_in && usb_endpoint_is_bulk_in(ep) &&
		    usb_endpoint_maxp(ep) == MAX_PACKET)
			ep_in = ep;

		if (!ep_out && usb_endpoint_is_bulk_out(ep) &&
		    usb_endpoint_maxp(ep) == MAX_PACKET)
			ep_out = ep;
	}

	if (!ep_in || !ep_out) {
		dev_err(&intf->dev, "required endpoints not found\n");
		return -ENODEV;
	}

	uirt = kzalloc(sizeof(*uirt), GFP_KERNEL);
	if (!uirt)
		return -ENOMEM;

	uirt->in = kmalloc(MAX_PACKET, GFP_KERNEL);
	if (!uirt->in)
		goto free_uirt;

	uirt->out = kmalloc(MAX_PACKET, GFP_KERNEL);
	if (!uirt->out)
		goto free_uirt;

	rc = rc_allocate_device(RC_DRIVER_IR_RAW);
	if (!rc)
		goto free_uirt;

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		goto free_rcdev;

	pipe = usb_rcvbulkpipe(usbdev, ep_in->bEndpointAddress);
	usb_fill_bulk_urb(urb, usbdev, pipe, uirt->in, MAX_PACKET,
			  uirt_in_callback, uirt);
	uirt->urb_in = urb;

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		goto free_rcdev;

	pipe = usb_sndbulkpipe(usbdev, ep_out->bEndpointAddress);
	usb_fill_bulk_urb(urb, usbdev, pipe, uirt->out, MAX_PACKET,
			  uirt_out_callback, uirt);

	uirt->dev = &intf->dev;
	uirt->usbdev = usbdev;
	uirt->rc = rc;
	uirt->urb_out = urb;
	uirt->rx_state = RX_STATE_INTERSPACE_HIGH;

	err = usb_submit_urb(uirt->urb_in, GFP_KERNEL);
	if (err != 0) {
		dev_err(uirt->dev, "failed to submit read urb: %d\n", err);
		return err;
	}

	err = init_ftdi(usbdev);
	if (err) {
		dev_err(uirt->dev, "failed to setup ftdi: %d\n", err);
		return err;
	}

	err = uirt_setup(uirt);
	if (err)
		goto free_rcdev;

	usb_make_path(usbdev, uirt->phys, sizeof(uirt->phys));

	rc->device_name = "USB-UIRT";
	rc->driver_name = KBUILD_MODNAME;
	rc->input_phys = uirt->phys;
	usb_to_input_id(usbdev, &rc->input_id);
	rc->dev.parent = &intf->dev;
	rc->priv = uirt;
	rc->tx_ir = uirt_tx;
	rc->s_tx_carrier = uirt_set_tx_carrier;
	rc->s_learning_mode = uirt_set_rx_wideband;
	rc->allowed_protocols = RC_PROTO_BIT_ALL_IR_DECODER;
	rc->map_name = RC_MAP_RC6_MCE;
	rc->rx_resolution = UNIT_US;
	rc->timeout = IR_TIMEOUT;

	uirt_set_tx_carrier(rc, 38000);

	err = rc_register_device(rc);
	if (err)
		goto free_rcdev;

	usb_set_intfdata(intf, uirt);

	return 0;

free_rcdev:
	usb_kill_urb(uirt->urb_out);
	usb_free_urb(uirt->urb_out);
	usb_kill_urb(uirt->urb_in);
	usb_free_urb(uirt->urb_in);
	rc_free_device(rc);
free_uirt:
	kfree(uirt->in);
	kfree(uirt->out);
	kfree(uirt);
	return err;
}

static void uirt_disconnect(struct usb_interface *intf)
{
	struct uirt *ir = usb_get_intfdata(intf);

	rc_unregister_device(ir->rc);
	usb_set_intfdata(intf, NULL);
	usb_kill_urb(ir->urb_out);
	usb_free_urb(ir->urb_out);
	usb_kill_urb(ir->urb_in);
	usb_free_urb(ir->urb_in);
	kfree(ir->in);
	kfree(ir->out);
	kfree(ir);
}

static const struct usb_device_id uirt_table[] = {
	{ USB_DEVICE(0x0403, 0xf850) },
	{ }
};

static struct usb_driver uirt_driver = {
	.name = KBUILD_MODNAME,
	.probe = uirt_probe,
	.disconnect = uirt_disconnect,
	.id_table = uirt_table,
};

module_usb_driver(uirt_driver);

MODULE_AUTHOR("Sean Young <sean@mess.org>");
MODULE_DESCRIPTION("USB-UIRT driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(usb, uirt_table);
