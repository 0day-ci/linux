/*
 * Copyright(c) 2011-2016 Intel Corporation. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "i915_drv.h"
#include "i915_vgpu.h"
#include "intel_gvt.h"
#include "gvt/gvt.h"

/**
 * DOC: Intel GVT-g host support
 *
 * Intel GVT-g is a graphics virtualization technology which shares the
 * GPU among multiple virtual machines on a time-sharing basis. Each
 * virtual machine is presented a virtual GPU (vGPU), which has equivalent
 * features as the underlying physical GPU (pGPU), so i915 driver can run
 * seamlessly in a virtual machine.
 *
 * To virtualize GPU resources GVT-g driver depends on hypervisor technology
 * e.g KVM/VFIO/mdev, Xen, etc. to provide resource access trapping capability
 * and be virtualized within GVT-g device module. More architectural design
 * doc is available on https://01.org/group/2230/documentation-list.
 */

static bool is_supported_device(struct drm_i915_private *dev_priv)
{
	if (IS_BROADWELL(dev_priv))
		return true;
	if (IS_SKYLAKE(dev_priv))
		return true;
	if (IS_KABYLAKE(dev_priv))
		return true;
	if (IS_BROXTON(dev_priv))
		return true;
	if (IS_COFFEELAKE(dev_priv))
		return true;
	if (IS_COMETLAKE(dev_priv))
		return true;

	return false;
}

/**
 * intel_gvt_sanitize_options - sanitize GVT related options
 * @dev_priv: drm i915 private data
 *
 * This function is called at the i915 options sanitize stage.
 */
void intel_gvt_sanitize_options(struct drm_i915_private *dev_priv)
{
	if (!dev_priv->params.enable_gvt)
		return;

	if (intel_vgpu_active(dev_priv)) {
		drm_info(&dev_priv->drm, "GVT-g is disabled for guest\n");
		goto bail;
	}

	if (!is_supported_device(dev_priv)) {
		drm_info(&dev_priv->drm,
			 "Unsupported device. GVT-g is disabled\n");
		goto bail;
	}

	return;
bail:
	dev_priv->params.enable_gvt = 0;
}

#define GENERATE_MMIO_TABLE_IN_I915
static int new_mmio_info(struct intel_gvt *gvt, u32 offset)
{
	void *mmio = gvt->hw_state.mmio;

	*(u32 *)(mmio + offset) = intel_uncore_read_notrace(gvt->gt->uncore,
							    _MMIO(offset));
	return 0;
}

#include "gvt/reg.h"
#include "gvt/mmio_table.h"
#undef GENERATE_MMIO_TABLE_IN_I915

static void init_device_info(struct intel_gvt *gvt)
{
	struct intel_gvt_device_info *info = &gvt->device_info;
	struct pci_dev *pdev = to_pci_dev(gvt->gt->i915->drm.dev);

	info->max_support_vgpus = 8;
	info->cfg_space_size = PCI_CFG_SPACE_EXP_SIZE;
	info->mmio_size = 2 * 1024 * 1024;
	info->mmio_bar = 0;
	info->gtt_start_offset = 8 * 1024 * 1024;
	info->gtt_entry_size = 8;
	info->gtt_entry_size_shift = 3;
	info->gmadr_bytes_in_cmd = 8;
	info->max_surface_size = 36 * 1024 * 1024;
	info->msi_cap_offset = pdev->msi_cap;
}

/**
 * intel_gvt_init - initialize GVT components
 * @dev_priv: drm i915 private data
 *
 * This function is called at the initialization stage to create a GVT device.
 *
 * Returns:
 * Zero on success, negative error code if failed.
 *
 */
int intel_gvt_init(struct drm_i915_private *dev_priv)
{
	struct pci_dev *pdev = to_pci_dev(dev_priv->drm.dev);
	struct intel_gvt *gvt = NULL;
	struct intel_gvt_hw_state *hw_state;
	struct intel_gvt_device_info *info;
	void *mem;
	int ret;
	int i;

	if (i915_inject_probe_failure(dev_priv))
		return -ENODEV;

	if (!dev_priv->params.enable_gvt) {
		drm_dbg(&dev_priv->drm,
			"GVT-g is disabled by kernel params\n");
		return 0;
	}

	if (intel_uc_wants_guc_submission(&dev_priv->gt.uc)) {
		drm_err(&dev_priv->drm,
			"i915 GVT-g loading failed due to Graphics virtualization is not yet supported with GuC submission\n");
		goto bail;
	}

	gvt = kzalloc(sizeof(struct intel_gvt), GFP_KERNEL);
	if (!gvt)
		goto bail;

	gvt->gt = &dev_priv->gt;
	hw_state = &gvt->hw_state;
	info = &gvt->device_info;

	init_device_info(gvt);

	mem = kmalloc(info->cfg_space_size, GFP_KERNEL);
	if (!mem)
		goto err_cfg_space;

	hw_state->cfg_space = mem;

	mem = vmalloc(info->mmio_size);
	if (!mem)
		goto err_mmio;

	hw_state->mmio = mem;

	for (i = 0; i < PCI_CFG_SPACE_EXP_SIZE; i += 4)
		pci_read_config_dword(pdev, i, hw_state->cfg_space + i);

	ret = intel_gvt_init_mmio_info(gvt);
	if (ret)
		goto err_mmio_info;

	dev_priv->gvt = gvt;

	ret = intel_gvt_init_device(dev_priv);
	if (ret) {
		drm_dbg(&dev_priv->drm, "Fail to init GVT device\n");
		goto err_mmio_info;
	}

	return 0;

err_mmio_info:
	vfree(hw_state->mmio);
err_mmio:
	kfree(hw_state->cfg_space);
err_cfg_space:
	kfree(gvt);
bail:
	dev_priv->params.enable_gvt = 0;
	return 0;
}

static inline bool intel_gvt_active(struct drm_i915_private *dev_priv)
{
	return dev_priv->gvt;
}

/**
 * intel_gvt_driver_remove - cleanup GVT components when i915 driver is
 *			     unbinding
 * @dev_priv: drm i915 private *
 *
 * This function is called at the i915 driver unloading stage, to shutdown
 * GVT components and release the related resources.
 */
void intel_gvt_driver_remove(struct drm_i915_private *dev_priv)
{
	struct intel_gvt *gvt = dev_priv->gvt;

	if (!intel_gvt_active(dev_priv))
		return;

	kfree(gvt->hw_state.cfg_space);
	vfree(gvt->hw_state.mmio);
	intel_gvt_clean_device(dev_priv);
	kfree(gvt);
	dev_priv->gvt = NULL;
}

/**
 * intel_gvt_resume - GVT resume routine wapper
 *
 * @dev_priv: drm i915 private *
 *
 * This function is called at the i915 driver resume stage to restore required
 * HW status for GVT so that vGPU can continue running after resumed.
 */
void intel_gvt_resume(struct drm_i915_private *dev_priv)
{
	if (intel_gvt_active(dev_priv))
		intel_gvt_pm_resume(dev_priv->gvt);
}
