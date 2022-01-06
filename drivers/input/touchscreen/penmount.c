
// SPDX-License-Identifier: GPL-2.0-only
/*
 * Penmount serial touchscreen driver
 *
 * Copyright (c) 2006 Rick Koch <n1gp@hotmail.com>
 * Copyright (c) 2022 John Sung <penmount.touch@gmail.com>
 *
 * Based on ELO driver (drivers/input/touchscreen/elo.c)
 * Copyright (c) 2004 Vojtech Pavlik
 */


#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/input/touchscreen.h>
#include <linux/serio.h>
#include <linux/serdev.h>
#include <linux/of.h>
#include <linux/of_device.h>

#define DRIVER_DESC	"PenMount serial touchscreen driver"

MODULE_AUTHOR("Rick Koch <n1gp@hotmail.com>");
MODULE_AUTHOR("John Sung <penmount.touch@gmail.com>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

/*
 * Definitions & global arrays.
 */

#define	PM_MAX_LENGTH	6
#define	PM_MAX_MTSLOT	16
#define	PM_3000_MTSLOT	5
#define	PM_6250_MTSLOT	12

enum {
	PMSERIAL_DEVICEID_9000 = 0,
	PMSERIAL_DEVICEID_6000,
	PMSERIAL_DEVICEID_P2,
	PMSERIAL_DEVICEID_M1,
	PMSERIAL_DEVICEID_6010,
};

/*
 * Multi-touch slot
 */

struct mt_slot {
	unsigned short x, y;
	bool active; /* is the touch valid? */
};

/*
 * Per-touchscreen data.
 */

struct pm_device_conf;

struct pm {
	struct input_dev *dev;
	struct serio *serio;
	const struct pm_device_conf *conf;
	int idx;
	unsigned char data[PM_MAX_LENGTH];
	char phys[32];
	struct mt_slot slots[PM_MAX_MTSLOT];
};

struct pm_device_conf {
	unsigned long baudrate;
	unsigned short productid;
	unsigned char packetsize;
	unsigned char maxcontacts;
	int max;
	void (*parse_packet)(struct pm *);
};

/*
 * pm_mtevent() sends mt events and also emulates pointer movement
 */

static void pm_mtevent(struct pm *pm, struct input_dev *input)
{
	int i;

	for (i = 0; i < pm->conf->maxcontacts; ++i) {
		input_mt_slot(input, i);
		input_mt_report_slot_state(input, MT_TOOL_FINGER,
				pm->slots[i].active);
		if (pm->slots[i].active) {
			input_event(input, EV_ABS, ABS_MT_POSITION_X, pm->slots[i].x);
			input_event(input, EV_ABS, ABS_MT_POSITION_Y, pm->slots[i].y);
		}
	}

	input_mt_report_pointer_emulation(input, true);
	input_sync(input);
}

/*
 * pm_checkpacket() checks if data packet is valid
 */

static bool pm_checkpacket(unsigned char *packet)
{
	int total = 0;
	int i;

	for (i = 0; i < 5; i++)
		total += packet[i];

	return packet[5] == (unsigned char)~(total & 0xff);
}

static void pm_parse_9000(struct pm *pm)
{
	struct input_dev *dev = pm->dev;

	if ((pm->data[0] & 0x80) && pm->conf->packetsize == ++pm->idx) {
		input_report_abs(dev, ABS_X, pm->data[1] * 128 + pm->data[2]);
		input_report_abs(dev, ABS_Y, pm->data[3] * 128 + pm->data[4]);
		input_report_key(dev, BTN_TOUCH, !!(pm->data[0] & 0x40));
		input_sync(dev);
		pm->idx = 0;
	}
}

static void pm_parse_6000(struct pm *pm)
{
	struct input_dev *dev = pm->dev;

	if ((pm->data[0] & 0xbf) == 0x30 && pm->conf->packetsize == ++pm->idx) {
		if (pm_checkpacket(pm->data)) {
			input_report_abs(dev, ABS_X,
					pm->data[2] * 256 + pm->data[1]);
			input_report_abs(dev, ABS_Y,
					pm->data[4] * 256 + pm->data[3]);
			input_report_key(dev, BTN_TOUCH, pm->data[0] & 0x40);
			input_sync(dev);
		}
		pm->idx = 0;
	}
}

static void pm_parse_3000(struct pm *pm)
{
	struct input_dev *dev = pm->dev;

	if ((pm->data[0] & 0xce) == 0x40 && pm->conf->packetsize == ++pm->idx) {
		if (pm_checkpacket(pm->data)) {
			int slotnum = pm->data[0] & 0x0f;
			pm->slots[slotnum].active = pm->data[0] & 0x30;
			pm->slots[slotnum].x = pm->data[2] * 256 + pm->data[1];
			pm->slots[slotnum].y = pm->data[4] * 256 + pm->data[3];
			pm_mtevent(pm, dev);
		}
		pm->idx = 0;
	}
}

static void pm_parse_6250(struct pm *pm)
{
	struct input_dev *dev = pm->dev;

	if ((pm->data[0] & 0xb0) == 0x30 && pm->conf->packetsize == ++pm->idx) {
		if (pm_checkpacket(pm->data)) {
			int slotnum = pm->data[0] & 0x0f;
			pm->slots[slotnum].active = pm->data[0] & 0x40;
			pm->slots[slotnum].x = pm->data[2] * 256 + pm->data[1];
			pm->slots[slotnum].y = pm->data[4] * 256 + pm->data[3];
			pm_mtevent(pm, dev);
		}
		pm->idx = 0;
	}
}

static const struct pm_device_conf pm_device_9000 = {
	.baudrate = 19200,
	.max = 0x3FF,
	.productid = 0x9000,
	.packetsize = 5,
	.maxcontacts = 1,
	.parse_packet = pm_parse_9000,
};

static const struct pm_device_conf pm_device_6000 = {
	.baudrate = 19200,	
	.max = 0x3FF,
	.productid = 0x6000,
	.packetsize = 6,
	.maxcontacts = 1,
	.parse_packet = pm_parse_6000,
};

static const struct pm_device_conf pm_device_p2 = {
	.baudrate = 38400,	
	.max = 0x7FF,
	.productid = 0x3000,
	.packetsize = 6,
	.maxcontacts = PM_3000_MTSLOT,
	.parse_packet = pm_parse_3000,
};

static const struct pm_device_conf pm_device_m1 = {
	.baudrate = 19200,	
	.max = 0x3FF,
	.productid = 0x6250,
	.packetsize = 6,
	.maxcontacts = PM_6250_MTSLOT,
	.parse_packet = pm_parse_6250,
};

static irqreturn_t pm_interrupt(struct serio *serio,
		unsigned char data, unsigned int flags)
{
	struct pm *pm = serio_get_drvdata(serio);

	pm->data[pm->idx] = data;

	pm->conf->parse_packet(pm);

	return IRQ_HANDLED;
}

/*
 * pm_disconnect() is the opposite of pm_connect()
 */

static void pm_disconnect(struct serio *serio)
{
	struct pm *pm = serio_get_drvdata(serio);

	serio_close(serio);

	input_unregister_device(pm->dev);
	kfree(pm);

	serio_set_drvdata(serio, NULL);
}

/*
 * pm_connect() is the routine that is called when someone adds a
 * new serio device that supports PenMount protocol and registers it as
 * an input device.
 */
static struct pm * pm_driver_init(struct device * dev, const struct pm_device_conf * conf, char *phys)
{
	struct pm *pm = NULL;
	struct input_dev *input_dev;
	int max_x, max_y;
	int err;

	pm = kzalloc(sizeof(struct pm), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!pm || !input_dev) {
		err = -ENOMEM;
		goto fail1;
	}
	
	pm->dev = input_dev;	

	input_dev->name = "PenMount Serial TouchScreen";
	input_dev->phys = phys;
	input_dev->id.bustype = BUS_RS232;
	input_dev->id.vendor = SERIO_PENMOUNT;
	input_dev->id.product = 0;
	input_dev->id.version = 0x0100;
	input_dev->dev.parent = dev;

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);

	pm->conf = conf;
	input_dev->id.product = conf->productid;
	max_x = max_y = conf->max;

	input_set_abs_params(pm->dev, ABS_X, 0, max_x, 0, 0);
	input_set_abs_params(pm->dev, ABS_Y, 0, max_y, 0, 0);

	if (pm->conf->maxcontacts > 1) {
		input_mt_init_slots(pm->dev, pm->conf->maxcontacts, 0);
		input_set_abs_params(pm->dev,
				     ABS_MT_POSITION_X, 0, max_x, 0, 0);
		input_set_abs_params(pm->dev,
				     ABS_MT_POSITION_Y, 0, max_y, 0, 0);
	}
	return pm;
	
 fail1:	
	if (input_dev) input_free_device(input_dev);
	if (pm) kfree(pm);
	return NULL;
}

static int pm_connect(struct serio *serio, struct serio_driver *drv)
{
	struct pm *pm = NULL;
	int err = 0;
	const struct pm_device_conf * device = NULL;
	
	switch (serio->id.id) {
	case PMSERIAL_DEVICEID_9000:
		device = &pm_device_9000;
		break;
	default:
	case PMSERIAL_DEVICEID_6000:
		device = &pm_device_6000;
		break;
	case PMSERIAL_DEVICEID_P2:
		device = &pm_device_p2;
		break;
	case PMSERIAL_DEVICEID_M1:
		device = &pm_device_m1;
		break;
	}
	pm = pm_driver_init(&serio->dev, device, serio->phys);
	if (!pm) return -ENOMEM;
	
	pm->serio = serio;
	snprintf(pm->phys, sizeof(pm->phys), "%s/input0", serio->phys);

	serio_set_drvdata(serio, pm);

	err = serio_open(serio, drv);
	if (err)
		goto fail2;

	err = input_register_device(pm->dev);
	if (err)
		goto fail3;

	return 0;

 fail3:	serio_close(serio);
 fail2:	serio_set_drvdata(serio, NULL);
	input_free_device(pm->dev);
	kfree(pm);
	return err;
}

/*
 * The serio driver structure.
 */

static const struct serio_device_id pm_serio_ids[] = {
	{
		.type	= SERIO_RS232,
		.proto	= SERIO_PENMOUNT,
		.id	    = SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{ 0 }
};

MODULE_DEVICE_TABLE(serio, pm_serio_ids);

static struct serio_driver pm_drv = {
	.driver		= {
		.name	= "serio-penmount",
	},
	.description= DRIVER_DESC,
	.id_table	= pm_serio_ids,
	.interrupt	= pm_interrupt,
	.connect	= pm_connect,
	.disconnect	= pm_disconnect,
};

static void pm_serdev_wakeup(struct serdev_device *serdev)
{
	return;
}

static int pm_serdev_receive(struct serdev_device *serdev, const unsigned char *data,
		size_t count)
{
	struct pm *pm = NULL;	
	size_t i = 0;
	
	pm = serdev_device_get_drvdata(serdev);	
	if (pm == NULL) {
		return 0;
	}
	
	for (i = 0; i < count; i++) {
		pm->data[pm->idx] = data[i];
		pm->conf->parse_packet(pm);		
	}

	// Accept all data
	return count;
}

static const
struct serdev_device_ops pm_serdev_ops = {
	.receive_buf = pm_serdev_receive,
	.write_wakeup = pm_serdev_wakeup,
};

static int pm_serdev_enable(struct serdev_device *serdev)
{
	unsigned char cmd[6] = {0xF1, 0x00, 0x00, 0x00, 0x00, 0x0E};
	
	return serdev_device_write(serdev, cmd, sizeof(cmd), 0);
}

static int pm_serdev_probe(struct serdev_device *serdev)
{
	struct pm *pm = NULL;
	uint32_t speed = 0;
	const struct pm_device_conf * conf = &pm_device_6000;
	int err = 0;

	conf = (struct pm_device_conf *)of_device_get_match_data(&serdev->dev);
	pm = pm_driver_init(&serdev->dev, conf, (char *)dev_name(&serdev->dev));
	if (!pm) {
		return -ENOMEM;
	}	
	touchscreen_parse_properties(pm->dev, (pm->conf->maxcontacts > 1), NULL);
	
	serdev_device_set_drvdata(serdev, pm);
	serdev_device_set_client_ops(serdev, &pm_serdev_ops);

	err = serdev_device_open(serdev);
	if (err) {
		kfree(pm);
		return err;
	}

	of_property_read_u32(serdev->dev.of_node, "baudrate", &speed);
	if (!speed) speed = pm->conf->baudrate;
	speed = serdev_device_set_baudrate(serdev, speed);
	dev_info(&serdev->dev, "Using baudrate: %u\n", speed);
	
	serdev_device_set_flow_control(serdev, false);
	if (pm->conf->productid == 0x6000) {
		pm_serdev_enable (serdev);
	}
	
	err = input_register_device(pm->dev);
	if (err) {		
		serdev_device_close(serdev);
		input_free_device(pm->dev);
		kfree(pm);
		return err;
	}	

	return 0;
}

static void pm_serdev_remove(struct serdev_device *serdev)
{
	serdev_device_close(serdev);

	return;
}

static const struct of_device_id pm_serdev_of_match[] = {
	{
		.compatible = "penmount,pm9000",
		.data = &pm_device_9000,
	},
	{
		.compatible = "penmount,pm6000",
		.data = &pm_device_6000,
	},
	{
		.compatible = "penmount,p2",
		.data = &pm_device_p2,
	},
	{
		.compatible = "penmount,m1",
		.data = &pm_device_m1,
	},
	{}
};
MODULE_DEVICE_TABLE(of, pm_serdev_of_match);

static struct serdev_device_driver pm_serdev_drv = {
	.probe = pm_serdev_probe,
	.remove = pm_serdev_remove,
	.driver = {
		.name = "serdev-penmount",
		.of_match_table = of_match_ptr(pm_serdev_of_match),
	},
};

static int __init pm_init ( void )
{
	serdev_device_driver_register ( &pm_serdev_drv );
	return serio_register_driver ( &pm_drv ) ;
}

static void __exit pm_exit ( void )
{
	serdev_device_driver_unregister ( &pm_serdev_drv );
	serio_unregister_driver ( &pm_drv ) ;

	return ;
}

module_init(pm_init);
module_exit(pm_exit);
