// SPDX-License-Identifier: GPL-2.0
/*
 * File for PCIe quirks.
 */

/* The low-level PCI operations will be performed in this file. Therefore,
 * let's use dev_*() instead of mwifiex_dbg() here to avoid troubles (e.g.
 * to avoid using mwifiex_adapter struct before init or wifi is powered
 * down, or causes NULL ptr deref).
 */

#include <linux/dmi.h>

#include "pcie_quirks.h"

/* quirk table based on DMI matching */
static const struct dmi_system_id mwifiex_quirk_table[] = {
	{}
};

void mwifiex_initialize_quirks(struct pcie_service_card *card)
{
	struct pci_dev *pdev = card->dev;
	const struct dmi_system_id *dmi_id;

	dmi_id = dmi_first_match(mwifiex_quirk_table);
	if (dmi_id)
		card->quirks = (uintptr_t)dmi_id->driver_data;

	if (!card->quirks)
		dev_info(&pdev->dev, "no quirks enabled\n");
}
