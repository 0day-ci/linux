/* SPDX-License-Identifier: GPL-2.0 */
#ifndef DRIVERS_PCI_PCIE_EM_H
#define DRIVERS_PCI_PCIE_EM_H

#include <linux/pci.h>
#include <linux/acpi.h>

#define PCIE_SSD_LEDS_DSM_GUID						\
	GUID_INIT(0x5d524d9d, 0xfff9, 0x4d4b,				\
		  0x8c, 0xb7, 0x74, 0x7e, 0xd5, 0x1e, 0x19, 0x4d)

#define GET_SUPPORTED_STATES_DSM	0x01
#define GET_STATE_DSM			0x02
#define SET_STATE_DSM			0x03

static inline bool pci_has_pcie_em_dsm(struct pci_dev *pdev)
{
#ifdef CONFIG_ACPI
	acpi_handle handle;
	const guid_t pcie_ssd_leds_dsm_guid = PCIE_SSD_LEDS_DSM_GUID;

	handle = ACPI_HANDLE(&pdev->dev);
	if (handle)
		if (acpi_check_dsm(handle, &pcie_ssd_leds_dsm_guid, 0x1,
				   1 << GET_SUPPORTED_STATES_DSM ||
				   1 << GET_STATE_DSM ||
				   1 << SET_STATE_DSM))
			return true;
#endif
	return false;
}

static inline bool pci_has_npem(struct pci_dev *pdev)
{
	int pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_NPEM);
	u32 cap;

	if (pos)
		if (!pci_read_config_dword(pdev, pos + PCI_NPEM_CAP, &cap))
			return cap & PCI_NPEM_CAP_NPEM_CAP;
	return false;
}

static inline bool pci_has_enclosure_management(struct pci_dev *pdev)
{
	return pci_has_pcie_em_dsm(pdev) || pci_has_npem(pdev);
}

static inline void release_pcie_em_aux_device(struct device *dev)
{
	kfree(to_auxiliary_dev(dev));
}

static inline struct auxiliary_device *register_pcie_em_auxdev(struct device *dev, int id)
{
	struct auxiliary_device *adev;
	int ret;

	if (!pci_has_enclosure_management(to_pci_dev(dev)))
		return NULL;

	adev = kzalloc(sizeof(*adev), GFP_KERNEL);
	if (!adev)
		goto em_reg_out_err;

	adev->name = "pcie_em";
	adev->dev.parent = dev;
	adev->dev.release = release_pcie_em_aux_device;
	adev->id = id;

	ret = auxiliary_device_init(adev);
	if (ret < 0)
		goto em_reg_out_free;

	ret = auxiliary_device_add(adev);
	if (ret) {
		auxiliary_device_uninit(adev);
		goto em_reg_out_free;
	}

	return adev;
em_reg_out_free:
	kfree(adev);
em_reg_out_err:
	dev_warn(dev, "failed to register pcie_em device\n");
	return NULL;
}

static inline void unregister_pcie_em_auxdev(struct auxiliary_device *auxdev)
{
	if (auxdev) {
		auxiliary_device_delete(auxdev);
		auxiliary_device_uninit(auxdev);
	}
}

#endif /* DRIVERS_PCI_PCIE_EM_H */
