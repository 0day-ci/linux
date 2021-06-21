// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2014 Broadcom Corporation
 */
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_irq.h>

#include <defs.h>
#include "debug.h"
#include "core.h"
#include "common.h"
#include "of.h"

void brcmf_of_probe(struct device *dev, enum brcmf_bus_type bus_type,
		    struct brcmf_mp_device *settings)
{
	struct brcmfmac_sdio_pd *sdio = &settings->bus.sdio;
	struct device_node *root, *np = dev->of_node;
	int irq;
	u32 irqf;
	u32 val;

	/* Set board-type to the first string of the machine compatible prop */
	root = of_find_node_by_path("/");
	if (root) {
		char *board_type;
		const char *tmp;

		of_property_read_string_index(root, "compatible", 0, &tmp);

		/* get rid of '/' in the compatible string to be able to find the FW */
		board_type = devm_kstrdup(dev, tmp, GFP_KERNEL);
		if (!board_type)
			return;
		strreplace(board_type, '/', '-');
		settings->board_type = board_type;

		of_node_put(root);
	}

	if (!np || bus_type != BRCMF_BUSTYPE_SDIO ||
	    !of_device_is_compatible(np, "brcm,bcm4329-fmac"))
		return;

	if (of_property_read_u32(np, "brcm,drive-strength", &val) == 0)
		sdio->drive_strength = val;

	/* make sure there are interrupts defined in the node */
	if (!of_find_property(np, "interrupts", NULL))
		return;

	irq = irq_of_parse_and_map(np, 0);
	if (!irq) {
		brcmf_err("interrupt could not be mapped\n");
		return;
	}
	irqf = irqd_get_trigger_type(irq_get_irq_data(irq));

	sdio->oob_irq_supported = true;
	sdio->oob_irq_nr = irq;
	sdio->oob_irq_flags = irqf;
}
