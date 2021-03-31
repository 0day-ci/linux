/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2021, Linaro Ltd <loic.poulain@linaro.org> */

#ifndef __WWAN_H
#define __WWAN_H

#include <linux/device.h>
#include <linux/kernel.h>

/**
 * enum wwan_port_type - WWAN port types
 * @WWAN_PORT_AT: AT commands
 * @WWAN_PORT_MBIM: Mobile Broadband Interface Model control
 * @WWAN_PORT_QMI: Qcom modem/MSM interface for modem control
 * @WWAN_PORT_QCDM: Qcom Modem diagnostic interface
 * @WWAN_PORT_FIREHOSE: XML based command protocol
 * @WWAN_PORT_MAX
 */
enum wwan_port_type {
	WWAN_PORT_AT,
	WWAN_PORT_MBIM,
	WWAN_PORT_QMI,
	WWAN_PORT_QCDM,
	WWAN_PORT_FIREHOSE,
	WWAN_PORT_MAX,
};

/**
 * struct wwan_port - The structure that defines a WWAN port
 * @type: Port type
 * @fops: Pointer to file operations
 * @dev: Underlying device
 */
struct wwan_port {
	enum wwan_port_type type;
	const struct file_operations *fops;
	struct device dev;
};

/**
 * wwan_create_port - Add a new WWAN port
 * @parent: Device to use as parent and shared by all WWAN ports
 * @type: WWAN port type
 * @fops: File operations
 * @private_data: Pointer to caller private_data
 *
 * Allocate and register a new WWAN port. The port will be automatically exposed
 * to user as a character device. The port will be automatically attached to the
 * right WWAN device, based on the parent pointer. The parent pointer is the
 * device shared by all components of a same WWAN modem (e.g. USB dev, PCI dev,
 * MHI controller...).
 *
 * private_data will be placed in the file's private_data so it can be used by
 * the port file operations.
 *
 * This function must be balanced with a call to wwan_destroy_port().
 *
 * Returns a valid pointer to wwan_port on success or PTR_ERR on failure
 */
struct wwan_port *wwan_create_port(struct device *parent,
				   enum wwan_port_type type,
				   const struct file_operations *fops,
				   void *private_data);

/**
 * wwan_remove_port - Remove a WWAN port
 * @port: WWAN port to remove
 *
 * Remove a previously created port.
 */
void wwan_remove_port(struct wwan_port *port);

#endif /* __WWAN_H */
