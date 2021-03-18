/* SPDX-License-Identifier: MIT */

#ifndef _DRM_APERTURE_H_
#define _DRM_APERTURE_H_

#include <linux/fb.h>
#include <linux/vgaarb.h>

/**
 * drm_fb_helper_remove_conflicting_framebuffers - remove firmware-configured framebuffers
 * @a: memory range, users of which are to be removed
 * @name: requesting driver name
 * @primary: also kick vga16fb if present
 *
 * This function removes framebuffer devices (initialized by firmware/bootloader)
 * which use memory range described by @a. If @a is NULL all such devices are
 * removed.
 */
static inline int
drm_fb_helper_remove_conflicting_framebuffers(struct apertures_struct *a,
					      const char *name, bool primary)
{
#if IS_REACHABLE(CONFIG_FB)
	return remove_conflicting_framebuffers(a, name, primary);
#else
	return 0;
#endif
}

/**
 * drm_fb_helper_remove_conflicting_pci_framebuffers - remove firmware-configured
 *                                                     framebuffers for PCI devices
 * @pdev: PCI device
 * @name: requesting driver name
 *
 * This function removes framebuffer devices (eg. initialized by firmware)
 * using memory range configured for any of @pdev's memory bars.
 *
 * The function assumes that PCI device with shadowed ROM drives a primary
 * display and so kicks out vga16fb.
 */
static inline int
drm_fb_helper_remove_conflicting_pci_framebuffers(struct pci_dev *pdev,
						  const char *name)
{
	int ret = 0;

	/*
	 * WARNING: Apparently we must kick fbdev drivers before vgacon,
	 * otherwise the vga fbdev driver falls over.
	 */
#if IS_REACHABLE(CONFIG_FB)
	ret = remove_conflicting_pci_framebuffers(pdev, name);
#endif
	if (ret == 0)
		ret = vga_remove_vgacon(pdev);
	return ret;
}

#endif
