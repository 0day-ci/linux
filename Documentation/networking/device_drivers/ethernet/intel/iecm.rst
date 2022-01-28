.. SPDX-License-Identifier: GPL-2.0

========================
Intel Ethernet Common Module
========================

The Intel Ethernet Common Module is meant to serve as an abstraction layer
between device specific implementation details and common net device driver
flows. This library provides several function hooks which allow a device driver
to specify register addresses, control queue communications, and other device
specific functionality.  Some functions are required to be implemented while
others have a default implementation that is used when none is supplied by the
device driver.  Doing this, a device driver can be written to take advantage
of existing code while also giving the flexibility to implement device specific
features.

The common use case for this library is for a network device driver that wants
specify its own device specific details but also leverage the more common
code flows found in network device drivers.

Sections in this document:
	Entry Point
	Exit Point
	Register Operations API
	Virtchnl Operations API

Entry Point
~~~~~~~~~~~
The primary entry point to the library is the iecm_probe function.  Prior to
calling this, device drivers must have allocated an iecm_adapter struct and
initialized it with the required API functions.  The adapter struct, along with
the pci_dev struct and the pci_device_id struct, is provided to iecm_probe
which finalizes device initialization and prepares the device for open.

The iecm_dev_ops struct within the iecm_adapter struct is the primary vehicle
for passing information from device drivers to the common module.  A dependent
module must define and assign a reg_ops_init function which will assign the
respective function pointers to initialize register values (see iecm_reg_ops
struct).  These are required to be provided by the dependent device driver as
no suitable default can be assumed for register addresses.

The vc_ops_init function pointer and the related iecm_virtchnl_ops struct are
optional and should only be necessary for device drivers which use a different
method/timing for communicating across a mailbox to the hardware.  Within iecm
is a default interface provided in the case where one is not provided by the
device driver.

Exit Point
~~~~~~~~~~
When the device driver is being prepared to be removed through the pci_driver
remove callback, it is required for the device driver to call iecm_remove with
the pci_dev struct provided.  This is required to ensure all resources are
properly freed and returned to the operating system.

Register Operations API
~~~~~~~~~~~~~~~~~~~~~~~
iecm_reg_ops contains three different function pointers relating to initializing
registers for the specific net device using the library.

ctlq_reg_init relates specifically to setting up registers related to control
queue/mailbox communications.  Registers that should be defined include: head,
tail, len, bah, bal, len_mask, len_ena_mask, and head_mask.

vportq_reg_init relates to setting up queue registers.  The tail registers to
be assigned to the iecm_queue struct for each RX/TX queue.

intr_reg_init relates to any registers needed to setup interrupts.  These
include registers needed to enable the interrupt and change ITR settings.

If the initialization function finds that one or more required function
pointers were not provided, an error will be issued and the device will be
inoperable.


Virtchnl Operations API
~~~~~~~~~~~~~~~~~~~~~~~
The virtchnl is a conduit between driver and hardware that allows device
drivers to send and receive control messages to/from hardware.  This is
optional to be specified as there is a general interface that can be assumed
when using this library.  However, if a device deviates in some way to
communicate across the mailbox correctly, this interface is provided to allow
that.

If vc_ops_init is set in the dev_ops field of the iecm_adapter struct, then it
is assumed the device driver is using providing it's own interface to do
virtchnl communications.  While providing vc_ops_init is optional, if it is
provided, it is required that the device driver provide function pointers for
those functions in vc_ops, with exception for the enable_vport, disable_vport,
and destroy_vport functions which may not be required for all devices.

If the initialization function finds that vc_ops_init was defined but one or
more required function pointers were not provided, an error will be issued and
the device will be inoperable.
