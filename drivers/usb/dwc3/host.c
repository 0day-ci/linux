// SPDX-License-Identifier: GPL-2.0
/*
 * host.c - DesignWare USB3 DRD Controller Host Glue
 *
 * Copyright (C) 2011 Texas Instruments Incorporated - https://www.ti.com
 *
 * Authors: Felipe Balbi <balbi@ti.com>,
 */

#include <linux/acpi.h>
#include <linux/platform_device.h>

#include "core.h"
#include "../host/xhci.h"
#include "../host/xhci-plat.h"

static int xhci_dwc3_suspend_quirk(struct usb_hcd *hcd);

static const struct xhci_plat_priv xhci_plat_dwc3_xhci = {
	.suspend_quirk = xhci_dwc3_suspend_quirk,
};

static int dwc3_host_get_irq(struct dwc3 *dwc)
{
	struct platform_device	*dwc3_pdev = to_platform_device(dwc->dev);
	int irq;

	irq = platform_get_irq_byname_optional(dwc3_pdev, "host");
	if (irq > 0)
		goto out;

	if (irq == -EPROBE_DEFER)
		goto out;

	irq = platform_get_irq_byname_optional(dwc3_pdev, "dwc_usb3");
	if (irq > 0)
		goto out;

	if (irq == -EPROBE_DEFER)
		goto out;

	irq = platform_get_irq(dwc3_pdev, 0);
	if (irq > 0)
		goto out;

	if (!irq)
		irq = -EINVAL;

out:
	return irq;
}

int dwc3_host_init(struct dwc3 *dwc)
{
	struct property_entry	props[4];
	struct platform_device	*xhci;
	int			ret, irq;
	struct resource		*res;
	struct platform_device	*dwc3_pdev = to_platform_device(dwc->dev);
	int			prop_idx = 0;

	irq = dwc3_host_get_irq(dwc);
	if (irq < 0)
		return irq;

	res = platform_get_resource_byname(dwc3_pdev, IORESOURCE_IRQ, "host");
	if (!res)
		res = platform_get_resource_byname(dwc3_pdev, IORESOURCE_IRQ,
				"dwc_usb3");
	if (!res)
		res = platform_get_resource(dwc3_pdev, IORESOURCE_IRQ, 0);
	if (!res)
		return -ENOMEM;

	dwc->xhci_resources[1].start = irq;
	dwc->xhci_resources[1].end = irq;
	dwc->xhci_resources[1].flags = res->flags;
	dwc->xhci_resources[1].name = res->name;

	xhci = platform_device_alloc("xhci-hcd", PLATFORM_DEVID_AUTO);
	if (!xhci) {
		dev_err(dwc->dev, "couldn't allocate xHCI device\n");
		return -ENOMEM;
	}

	xhci->dev.parent	= dwc->dev;
	ACPI_COMPANION_SET(&xhci->dev, ACPI_COMPANION(dwc->dev));

	dwc->xhci = xhci;

	ret = platform_device_add_resources(xhci, dwc->xhci_resources,
						DWC3_XHCI_RESOURCES_NUM);
	if (ret) {
		dev_err(dwc->dev, "couldn't add resources to xHCI device\n");
		goto err;
	}

	memset(props, 0, sizeof(struct property_entry) * ARRAY_SIZE(props));

	if (dwc->usb3_lpm_capable)
		props[prop_idx++] = PROPERTY_ENTRY_BOOL("usb3-lpm-capable");

	if (dwc->usb2_lpm_disable)
		props[prop_idx++] = PROPERTY_ENTRY_BOOL("usb2-lpm-disable");

	/**
	 * WORKAROUND: dwc3 revisions <=3.00a have a limitation
	 * where Port Disable command doesn't work.
	 *
	 * The suggested workaround is that we avoid Port Disable
	 * completely.
	 *
	 * This following flag tells XHCI to do just that.
	 */
	if (DWC3_VER_IS_WITHIN(DWC3, ANY, 300A))
		props[prop_idx++] = PROPERTY_ENTRY_BOOL("quirk-broken-port-ped");

	if (prop_idx) {
		ret = device_create_managed_software_node(&xhci->dev, props, NULL);
		if (ret) {
			dev_err(dwc->dev, "failed to add properties to xHCI\n");
			goto err;
		}
	}

	ret = platform_device_add_data(xhci, &xhci_plat_dwc3_xhci,
			sizeof(struct xhci_plat_priv));
	if (ret) {
		dev_err(dwc->dev, "failed to add data to xHCI\n");
		goto err;
	}

	ret = platform_device_add(xhci);
	if (ret) {
		dev_err(dwc->dev, "failed to register xHCI device\n");
		goto err;
	}

	return 0;
err:
	platform_device_put(xhci);
	return ret;
}

static void dwc3_set_phy_mode(struct usb_hcd *hcd)
{

	int i, num_ports;
	u32 reg;
	unsigned int ss_phy_mode = 0;
	struct dwc3 *dwc = dev_get_drvdata(hcd->self.controller->parent);
	struct xhci_hcd	*xhci_hcd = hcd_to_xhci(hcd);

	dwc->hs_phy_mode = 0;

	reg = readl(&xhci_hcd->cap_regs->hcs_params1);
	num_ports = HCS_MAX_PORTS(reg);

	for (i = 0; i < num_ports; i++) {
		reg = readl(&xhci_hcd->op_regs->port_status_base + i * 0x04);
		if (reg & PORT_PE) {
			if (DEV_HIGHSPEED(reg) || DEV_FULLSPEED(reg))
				dwc->hs_phy_mode |= PHY_MODE_USB_HOST_HS;
			else if (DEV_LOWSPEED(reg))
				dwc->hs_phy_mode |= PHY_MODE_USB_HOST_LS;

			if (DEV_SUPERSPEED(reg))
				ss_phy_mode |= PHY_MODE_USB_HOST_SS;
		}
	}
	phy_set_mode(dwc->usb2_generic_phy, dwc->hs_phy_mode);
	phy_set_mode(dwc->usb3_generic_phy, ss_phy_mode);
}

int xhci_dwc3_suspend_quirk(struct usb_hcd *hcd)
{
	struct dwc3 *dwc = dev_get_drvdata(hcd->self.controller->parent);

	dwc3_set_phy_mode(hcd);

	if (usb_wakeup_enabled_descendants(hcd->self.root_hub))
		dwc->phy_power_off = false;
	else
		dwc->phy_power_off = true;

	return 0;
}

void dwc3_host_exit(struct dwc3 *dwc)
{
	platform_device_unregister(dwc->xhci);
}
