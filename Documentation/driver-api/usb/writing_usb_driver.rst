.. _writing-usb-driver:

==========================
Writing USB Device Drivers
==========================

:Author: Greg Kroah-Hartman
:Author: Oliver Neukum

Introduction
============

The Linux USB subsystem has grown from supporting only two different
types of devices in the 2.2.7 kernel (mice and keyboards), to over 20
different types of devices in the 2.4 kernel. Linux currently supports
almost all USB class devices (standard types of devices like keyboards,
mice, modems, printers and speakers) and an ever-growing number of
vendor-specific devices (such as USB to serial converters, digital
cameras, Ethernet devices and MP3 players). For a full list of the
different USB devices currently supported, see Resources.

The remaining kinds of USB devices that do not have support on Linux are
almost all vendor-specific devices. Each vendor decides to implement a
custom protocol to talk to their device, so a custom driver usually
needs to be created. Some vendors are open with their USB protocols and
help with the creation of Linux drivers, while others do not publish
them, and developers are forced to reverse-engineer. See Resources for
some links to handy reverse-engineering tools.

This article tries to show best practices in writing USB drivers
based on examples taken from various drivers in the kernel. I will
concentrate on those who use a simple character device as an interface
to user space to not obscure the specifics of USB with those of
other subsystems.

Linux USB Basics
================

If you are going to write a Linux USB driver, please become familiar
with the USB protocol specification. It can be found, along with many
other useful documents, at the USB home page (see Resources). An
excellent introduction to the Linux USB subsystem can be found at the
USB Working Devices List (see Resources). It explains how the Linux USB
subsystem is structured and introduces the reader to the concept of USB
urbs (USB Request Blocks), which are essential to USB drivers.

The first thing a Linux USB driver needs to do is register itself with
the Linux USB subsystem, giving it some information about which devices
the driver supports and which functions to call when a device supported
by the driver is inserted or removed from the system. All of this
information is passed to the USB subsystem in the :c:type:`usb_driver`
structure. Just about any driver can serve as an example.

static struct usb_driver uas_driver = {
	.name = "uas",
	.probe = uas_probe,
	.disconnect = uas_disconnect,
	.pre_reset = uas_pre_reset,
	.post_reset = uas_post_reset,
	.suspend = uas_suspend,
	.resume = uas_resume,
	.reset_resume = uas_reset_resume,
	.drvwrap.driver.shutdown = uas_shutdown,
	.id_table = uas_usb_ids,
};


The variable name is a string that describes the driver. It is used in
informational messages printed to the system log. Within the system it has
no further function.

The next two function pointers, probe and disconnect are called due to
an addition event, that is when a device is added or removed to the system
or a new driver loaded and a match occurs.
Which devices match a driver is controlled by the ``id_table`` variable.

The actual registration with the USB subsystem referencing the declared
table is done either via the module_usb_driver(), as shown in the chaoskey
driver::

static struct usb_driver chaoskey_driver = {
	.name = DRIVER_SHORT,
	.probe = chaoskey_probe,
	.disconnect = chaoskey_disconnect,
	.suspend = chaoskey_suspend,
	.resume = chaoskey_resume,
	.reset_resume = chaoskey_resume,
	.id_table = chaoskey_table,
	.supports_autosuspend = 1,
};

module_usb_driver(chaoskey_driver);

It is also possible to define an __init function called when the module
containing the driver is loaded and to register the driver in that::

static int __init uas_init(void)
{
	int rv;

	workqueue = alloc_workqueue("uas", WQ_MEM_RECLAIM, 0);
	if (!workqueue)
		return -ENOMEM;

	rv = usb_register(&uas_driver);
	if (rv) {
		destroy_workqueue(workqueue);
		return -ENOMEM;
	}

	return 0;
}

module_init(uas_init);

The USB driver is then registered with a call to usb_register(). This manner
of initialisation should only be used if resources for the whole driver, like
the workqueue in UAS, need to be initialised or allocated.


When the driver is unloaded from the system, it needs to deregister
itself with the USB subsystem. This is done with usb_deregister()
function::

static void __exit uas_exit(void)
{
	usb_deregister(&uas_driver);
	destroy_workqueue(workqueue);
}

A driver shall bother with this only if it does not use module_usb_driver().


To enable the linux-hotplug system to load the driver automatically when
the device is plugged in, you need to create a ``MODULE_DEVICE_TABLE``.
The following code tells the hotplug scripts that this module supports
device with a given specific vendor and product ID::

static const struct usb_device_id chaoskey_table[] = {
	{ USB_DEVICE(CHAOSKEY_VENDOR_ID, CHAOSKEY_PRODUCT_ID) },
	{ USB_DEVICE(ALEA_VENDOR_ID, ALEA_PRODUCT_ID) },
	{ },
};
MODULE_DEVICE_TABLE(usb, chaoskey_table);

Drivers typically reuse the table given to the USB subsystem for matching
for this purpose.

There are other macros that can be used in describing a struct
:c:type:`usb_device_id` for drivers that support a whole class of USB
drivers. See :ref:`usb.h <usb_header>` for more information on this.

Device operation
================

When a device is plugged into the USB bus that matches the device ID
pattern that your driver registered with the USB core, the probe
function is called. The :c:type:`usb_interface` structure and
the interface ID are passed to the function::

static int uas_probe(struct usb_interface *intf, const struct usb_device_id *id)


The driver now needs to verify that this device is actually one that it
can accept. If so, it returns 0. If not, or if any error occurs during
initialization, an errorcode (such as ``-ENOMEM`` or ``-ENODEV``) is
returned from the probe function.

Drivers usually start out allocating memory for a representation
of that device in kernel space::

static int usblp_probe(struct usb_interface *intf,
		       const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct usblp *usblp;
	int protocol;
	int retval;

	/* Malloc and start initializing usblp structure so we can use it
	 * directly. */
	usblp = kzalloc(sizeof(struct usblp), GFP_KERNEL);
	if (!usblp) {
		retval = -ENOMEM;
		goto abort_ret;
	}

Then they check out the suitability of the device::

	protocol = usblp_select_alts(usblp);
	if (protocol < 0) {
		dev_dbg(&intf->dev,
			"incompatible printer-class device 0x%4.4X/0x%4.4X\n",
			le16_to_cpu(dev->descriptor.idVendor),
			le16_to_cpu(dev->descriptor.idProduct));
		retval = -ENODEV;
		goto abort;
	}

And penultimately they may put the device into a correct initial state::

	/* Setup the selected alternate setting and endpoints. */
	if (usblp_set_protocol(usblp, protocol) < 0) {
		retval = -ENODEV;	/* ->probe isn't ->ioctl */
		goto abort;
	}

Only as the very last step may the device be registered with the USB subsystem,
if a character device is intended as an interface to user space::

	retval = usb_register_dev(intf, &usblp_class);
	if (retval) {
		dev_err(&intf->dev,
			"usblp: Not able to get a minor (base %u, slice default): %d\n",
			USBLP_MINOR_BASE, retval);
		goto abort_intfdata;
	}

or with another subsystem, like SCSI core in UAS::

	result = scsi_add_host(shost, &intf->dev);
	if (result)
		goto free_streams;

At this point your device is live and your driver must be fully operational.
No mutual exclusion to probe() is provided.

Conversely, when the device is removed from the USB bus, the disconnect
function is called with the interface pointer. The driver needs to shut down
any pending urbs that are in the USB system.
Remember that a device may still be open at this stage as far as user space
is concerned. Subsequent operations need to fail gracefully and enough state
retained for this purpose. That means some deferred cleanup in those cases.

Now that the device is plugged into the system and the driver is bound
to the device, any of the functions in the :c:type:`file_operations` structure
that were passed to the USB subsystem will be called from a user program
trying to talk to the device. The first function called will be open, as
the program tries to open the device for I/O. We increment our private
usage count and save a pointer to our internal structure in the file
structure. This is done so that future calls to file operations will
enable the driver to determine which device the user is addressing. All
of this is done in the following example::

static int chaoskey_open(struct inode *inode, struct file *file)
{
	struct chaoskey *dev;
	struct usb_interface *interface;

	/* get the interface from minor number and driver information */
	interface = usb_find_interface(&chaoskey_driver, iminor(inode));
	if (!interface)
		return -ENODEV;

	usb_dbg(interface, "open");

	dev = usb_get_intfdata(interface);
	if (!dev) {
		usb_dbg(interface, "open (dev)");
		return -ENODEV;
	}

	file->private_data = dev;
	mutex_lock(&dev->lock);
	++dev->open;
	mutex_unlock(&dev->lock);

Multiple calls can race. Proper locking needs to be used in the driver.
A private count needs to be maintained because the driver must be able
to determine when the last user of an unplugged device goes away.

After the open function is called, the read and write functions are
called to receive and send data to the device. We are using cdc-wdm
as an example::

static ssize_t wdm_write
(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)

Data transfers to devices are described, initiated and controlled by means
of a data structure called URB, which a separate tutorial is provided for.
A write operation basically splits up its data into URBs and submits them
in the right order.
The basic operation is complicated by the requirements of the USB subsystem
that URBs not be used while power management operations or resets are
under way. Drivers need to heck for such conditions or make sure that the
device be not in power save::

	if (test_bit(WDM_DISCONNECTING, &desc->flags)) {
		rv = -ENODEV;
		goto out_free_mem_lock;
	}

	r = usb_autopm_get_interface(desc->intf);
	if (r < 0) {
		rv = usb_translate_errors(r);
		goto out_free_mem_lock;
	}

and::

	if (test_bit(WDM_RESETTING, &desc->flags))
		r = -EIO;

The :c:func:`usb_bulk_msg` function can be very useful for doing single reads
or writes to a device; however, if you need to read or write constantly to
a device, it is recommended to set up your own urbs and submit them to
the USB subsystem. Not using your own URBs prevents you from interrupting
ongoing transfers and does not allow you to use the ful bus bandwidth.

Whenever user space closes a file, the release operation is called::

 static int chaoskey_release(struct inode *inode, struct file *file)
 
 In addition to a conventional device, which needs to only stop IO,
 USB need to handle that a device has already gone away. In that case
 the driver needs to remove any reference to the device once the last user
 closes the device::
 
 	--dev->open;

	if (!dev->present) {
		if (dev->open == 0) {
			mutex_unlock(&dev->lock);
			chaoskey_free(dev);
		} else
			mutex_unlock(&dev->lock);
	} else
		mutex_unlock(&dev->lock);

One of the more difficult problems that USB drivers must be able to
handle smoothly is the fact that the USB device may be removed from the
system at any point in time, even if a program is currently talking to
it. It needs to be able to shut down any current reads and writes and
notify the user-space programs that the device is no longer there. The
following code (function ``chaoskey_disconnect``) is an example of how to do
this::

static void chaoskey_disconnect(struct usb_interface *interface)
{
	struct chaoskey	*dev;

	usb_dbg(interface, "disconnect");
	dev = usb_get_intfdata(interface);
	if (!dev) {
		usb_dbg(interface, "disconnect failed - no dev");
		return;
	}

	if (dev->hwrng_registered)
		hwrng_unregister(&dev->hwrng);

	usb_deregister_dev(interface, &chaoskey_class);

	usb_set_intfdata(interface, NULL);
	mutex_lock(&dev->lock);

	dev->present = false;
	usb_poison_urb(dev->urb);

	if (!dev->open) {
		mutex_unlock(&dev->lock);
		chaoskey_free(dev);
	} else
		mutex_unlock(&dev->lock);

	usb_dbg(interface, "disconnect done");
}

First, the device is deregistered from the system::

	usb_deregister_dev(interface, &chaoskey_class);

That step prevents any new users of the device and generates an event
reported to user space.

Second the device is internally marked not present, thereby entering a kind
of undead state::

	usb_set_intfdata(interface, NULL);
	mutex_lock(&dev->lock);

	dev->present = false;

Third any present and future IO is terminated::

	usb_poison_urb(dev->urb);

Only in the last step is the internal representation removed, if and
only if, no users are left::

	if (!dev->open) {
		mutex_unlock(&dev->lock);
		chaoskey_free(dev);

These operations must be performed in this specific order.


Operations on the whole device
==============================

Keep in mind that drivers for USB devices are technically drivers for
interfaces of USB devices. Some operations, namely reset and power
management apply to the whole device. For those the drivers must
coordinate. This is done in suc a way that before an operation one
function is called and after an operation another function is called.

For reset the following methods are provided::

	.pre_reset = uas_pre_reset,
	.post_reset = uas_post_reset,

After pre_reset() the device must have ceased all IO and may not begin
new IO. This is done here::

static int wdm_pre_reset(struct usb_interface *intf)
{
	struct wdm_device *desc = wdm_find_device(intf);

	/*
	 * we notify everybody using poll of
	 * an exceptional situation
	 * must be done before recovery lest a spontaneous
	 * message from the device is lost
	 */
	spin_lock_irq(&desc->iuspin);
	set_bit(WDM_RESETTING, &desc->flags);	/* inform read/write */
	set_bit(WDM_READ, &desc->flags);	/* unblock read */
	clear_bit(WDM_IN_USE, &desc->flags);	/* unblock write */
	desc->rerr = -EINTR;
	spin_unlock_irq(&desc->iuspin);
	wake_up_all(&desc->wait);

And all new IO must be prevented::

	poison_urbs(desc);
	cancel_work_sync(&desc->rxwork);
	cancel_work_sync(&desc->service_outs_intr);

After post_reset() IO can be restarted::

static int wdm_post_reset(struct usb_interface *intf)
{
	struct wdm_device *desc = wdm_find_device(intf);
	int rv;

	unpoison_urbs(desc);
	clear_bit(WDM_OVERFLOW, &desc->flags);
	clear_bit(WDM_RESETTING, &desc->flags);

That may involve bringing the device back to an operable state,
from the default state like UAS shows::

	err = uas_configure_endpoints(devinfo);
	if (err && err != -ENODEV)
		shost_printk(KERN_ERR, shost,
			     "%s: alloc streams error %d after reset",
			     __func__, err);

The driver must also somehow notify that the device may have lost data
or state. That can be done either in pre_reset(), like cdc-wdm does::

 	spin_lock_irq(&desc->iuspin);
	set_bit(WDM_RESETTING, &desc->flags);	/* inform read/write */
	set_bit(WDM_READ, &desc->flags);	/* unblock read */
	clear_bit(WDM_IN_USE, &desc->flags);	/* unblock write */
	desc->rerr = -EINTR;
	spin_unlock_irq(&desc->iuspin);

or it can be done in post reset, lie UAS does::

	/* we must unblock the host in every case lest we deadlock */
	spin_lock_irqsave(shost->host_lock, flags);
	scsi_report_bus_reset(shost, 0);
	spin_unlock_irqrestore(shost->host_lock, flags);

The operations for power management are covered in their own article.


Conclusion
==========

Writing Linux USB device drivers is not a difficult task as the
chaoskey or usblp drivers show. These drivers, combined with the other current
USB drivers, should provide enough examples to help a beginning author
create a working driver in a minimal amount of time. The linux-usb-devel
mailing list archives also contain a lot of helpful information.

Resources
=========

The Linux USB Project:
http://www.linux-usb.org/

Linux Hotplug Project:
http://linux-hotplug.sourceforge.net/

linux-usb Mailing List Archives:
https://lore.kernel.org/linux-usb/

Programming Guide for Linux USB Device Drivers:
https://lmu.web.psi.ch/docu/manuals/software_manuals/linux_sl/usb_linux_programming_guide.pdf

USB Home Page: https://www.usb.org
