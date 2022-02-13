// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2019-2022, Intel Corporation. All rights reserved.
 */

#include <linux/irq.h>
#include <linux/mei_aux.h>
#include "i915_reg.h"
#include "i915_drv.h"
#include "gt/intel_gt.h"
#include "intel_gsc.h"

#define GSC_BAR_LENGTH  0x00000FFC

static void gsc_irq_mask(struct irq_data *d)
{
	/* generic irq handling */
}

static void gsc_irq_unmask(struct irq_data *d)
{
	/* generic irq handling */
}

static struct irq_chip gsc_irq_chip = {
	.name = "gsc_irq_chip",
	.irq_mask = gsc_irq_mask,
	.irq_unmask = gsc_irq_unmask,
};

static int gsc_irq_init(struct drm_i915_private *dev_priv, int irq)
{
	irq_set_chip_and_handler_name(irq, &gsc_irq_chip,
				      handle_simple_irq, "gsc_irq_handler");

	return irq_set_chip_data(irq, dev_priv);
}

struct intel_gsc_def {
	const char *name;
	const unsigned long bar;
	size_t bar_size;
};

/* gscfi (graphics system controller firmware interface) resources */
static const struct intel_gsc_def intel_gsc_def_dg1[] = {
	{
	},
	{
		.name = "mei-gscfi",
		.bar = GSC_DG1_HECI2_BASE,
		.bar_size = GSC_BAR_LENGTH,
	}
};

static void intel_gsc_release_dev(struct device *dev)
{
	struct auxiliary_device *aux_dev = to_auxiliary_dev(dev);
	struct mei_aux_device *adev = auxiliary_dev_to_mei_aux_dev(aux_dev);

	kfree(adev);
}

static void intel_gsc_destroy_one(struct intel_gsc_intf *intf)
{
	if (intf->adev) {
		auxiliary_device_delete(&intf->adev->aux_dev);
		auxiliary_device_uninit(&intf->adev->aux_dev);
		intf->adev = NULL;
	}
	if (intf->irq >= 0)
		irq_free_desc(intf->irq);
	intf->irq = -1;
}

static void intel_gsc_init_one(struct drm_i915_private *dev_priv,
			       struct intel_gsc_intf *intf,
			       unsigned int intf_id)
{
	struct pci_dev *pdev = to_pci_dev(dev_priv->drm.dev);
	struct mei_aux_device *adev;
	struct auxiliary_device *aux_dev;
	const struct intel_gsc_def *def;
	int ret;

	intf->irq = -1;
	intf->id = intf_id;

	if (intf_id == 0 && !HAS_HECI_PXP(dev_priv))
		return;

	def = &intel_gsc_def_dg1[intf_id];

	dev_dbg(&pdev->dev, "init gsc one with id %d\n", intf_id);
	intf->irq = irq_alloc_desc(0);
	if (intf->irq < 0) {
		dev_err(&pdev->dev, "gsc irq error %d\n", intf->irq);
		return;
	}

	ret = gsc_irq_init(dev_priv, intf->irq);
	if (ret < 0) {
		dev_err(&pdev->dev, "gsc irq init failed %d\n", ret);
		goto fail;
	}

	adev = kzalloc(sizeof(*adev), GFP_KERNEL);
	if (!adev)
		goto fail;

	adev->irq = intf->irq;
	adev->bar.parent = &pdev->resource[0];
	adev->bar.start = def->bar + pdev->resource[0].start;
	adev->bar.end = adev->bar.start + def->bar_size - 1;
	adev->bar.flags = IORESOURCE_MEM;
	adev->bar.desc = IORES_DESC_NONE;

	aux_dev = &adev->aux_dev;
	aux_dev->name = def->name;
	aux_dev->id = (pci_domain_nr(pdev->bus) << 16) |
		      PCI_DEVID(pdev->bus->number, pdev->devfn);
	aux_dev->dev.parent = &pdev->dev;
	aux_dev->dev.release = intel_gsc_release_dev;

	ret = auxiliary_device_init(aux_dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "gsc aux init failed %d\n", ret);
		kfree(adev);
		goto fail;
	}

	ret = auxiliary_device_add(aux_dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "gsc aux add failed %d\n", ret);
		/* adev will be freed with the put_device() and .release sequence */
		auxiliary_device_uninit(aux_dev);
		goto fail;
	}
	intf->adev = adev;

	dev_dbg(&pdev->dev, "gsc init one done\n");
	return;
fail:
	intel_gsc_destroy_one(intf);
}

static void intel_gsc_irq_handler(struct intel_gt *gt, unsigned int intf_id)
{
	int ret;

	if (intf_id >= INTEL_GSC_NUM_INTERFACES)
		return;

	if (!HAS_HECI_GSC(gt->i915))
		return;

	if (gt->gsc.intf[intf_id].irq <= 0) {
		DRM_ERROR_RATELIMITED("error handling GSC irq: irq not set");
		return;
	}

	ret = generic_handle_irq(gt->gsc.intf[intf_id].irq);
	if (ret)
		DRM_ERROR_RATELIMITED("error handling GSC irq: %d\n", ret);
}

void gsc_irq_handler(struct intel_gt *gt, u32 iir)
{
	if (iir & GSC_IRQ_INTF(0))
		intel_gsc_irq_handler(gt, 0);
	if (iir & GSC_IRQ_INTF(1))
		intel_gsc_irq_handler(gt, 1);
}

void intel_gsc_init(struct intel_gsc *gsc, struct drm_i915_private *dev_priv)
{
	unsigned int i;

	if (!HAS_HECI_GSC(dev_priv))
		return;

	for (i = 0; i < INTEL_GSC_NUM_INTERFACES; i++)
		intel_gsc_init_one(dev_priv, &gsc->intf[i], i);
}

void intel_gsc_fini(struct intel_gsc *gsc)
{
	struct intel_gt *gt = gsc_to_gt(gsc);
	unsigned int i;

	if (!HAS_HECI_GSC(gt->i915))
		return;

	for (i = 0; i < INTEL_GSC_NUM_INTERFACES; i++)
		intel_gsc_destroy_one(&gsc->intf[i]);
}
