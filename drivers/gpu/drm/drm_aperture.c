// SPDX-License-Identifier: MIT

#include <linux/device.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <drm/drm_aperture.h>
#include <drm/drm_drv.h>
#include <drm/drm_print.h>

/**
 * DOC: overview
 *
 * A graphics device might be supported by different drivers, but only one
 * driver can be active at any given time. Many systems load a generic
 * graphics drivers, such as EFI-GOP or VESA, early during the boot process.
 * During later boot stages, they replace the generic driver with a dedicated,
 * hardware-specific driver. To take over the device the dedicated driver
 * first has to remove the generic driver. DRM aperture functions manage
 * ownership of DRM framebuffer memory and hand-over between drivers.
 *
 * DRM drivers should call drm_fb_helper_remove_conflicting_framebuffers()
 * at the top of their probe function. The function removes any generic
 * driver that is currently associated with the given framebuffer memory.
 * If the framebuffer is located at PCI BAR 0, the rsp code looks as in the
 * example given below.
 *
 * .. code-block:: c
 *
 *	static int remove_conflicting_framebuffers(struct pci_dev *pdev)
 *	{
 *		struct apertures_struct *ap;
 *		bool primary = false;
 *		int ret;
 *
 *		ap = alloc_apertures(1);
 *		if (!ap)
 *			return -ENOMEM;
 *
 *		ap->ranges[0].base = pci_resource_start(pdev, 0);
 *		ap->ranges[0].size = pci_resource_len(pdev, 0);
 *
 *	#ifdef CONFIG_X86
 *		primary = pdev->resource[PCI_ROM_RESOURCE].flags & IORESOURCE_ROM_SHADOW;
 *	#endif
 *		ret = drm_fb_helper_remove_conflicting_framebuffers(ap, "example driver", primary);
 *		kfree(ap);
 *
 *		return ret;
 *	}
 *
 *	static int probe(struct pci_dev *pdev)
 *	{
 *		int ret;
 *
 *		// Remove any generic drivers...
 *		ret = remove_conflicting_framebuffers(pdev);
 *		if (ret)
 *			return ret;
 *
 *		// ... and initialize the hardware.
 *		...
 *
 *		drm_dev_register();
 *
 *		return 0;
 *	}
 *
 * For PCI devices it is often sufficient to use drm_fb_helper_remove_conflicting_pci_framebuffers()
 * and let it detect the framebuffer apertures automatically.
 *
 * .. code-block:: c
 *
 *	static int probe(struct pci_dev *pdev)
 *	{
 *		int ret;
 *
 *		// Remove any generic drivers...
 *		ret = drm_fb_helper_remove_conflicting_pci_framebuffers(pdev, "example driver");
 *		if (ret)
 *			return ret;
 *
 *		// ... and initialize the hardware.
 *		...
 *
 *		drm_dev_register();
 *
 *		return 0;
 *	}
 *
 * Drivers that are susceptible to being removed be other drivers, such as
 * generic EFI or VESA drivers, have to register themselves as owners of their
 * given framebuffer memory. Ownership of the framebuffer memory is achived
 * by calling devm_aperture_acquire(). On success, the driver is the owner
 * of the framebuffer range. The function fails if the framebuffer is already
 * by another driver. See below for an example.
 *
 * .. code-block:: c
 *
 *	static struct drm_aperture_funcs ap_funcs = {
 *		.detach = ...
 *	};
 *
 *	static int acquire_framebuffers(struct drm_device *dev, struct pci_dev *pdev)
 *	{
 *		resource_size_t start, len;
 *		struct drm_aperture *ap;
 *
 *		base = pci_resource_start(pdev, 0);
 *		size = pci_resource_len(pdev, 0);
 *
 *		ap = devm_acquire_aperture(dev, base, size, &ap_funcs);
 *		if (IS_ERR(ap))
 *			return PTR_ERR(ap);
 *
 *		return 0;
 *	}
 *
 *	static int probe(struct pci_dev *pdev)
 *	{
 *		struct drm_device *dev;
 *		int ret;
 *
 *		// ... Initialize the device...
 *		dev = devm_drm_dev_alloc();
 *		...
 *
 *		// ... and acquire ownership of the framebuffer.
 *		ret = acquire_framebuffers(dev, pdev);
 *		if (ret)
 *			return ret;
 *
 *		drm_dev_register();
 *
 *		return 0;
 *	}
 *
 * The generic driver is now subject to forced removal by other drivers. This
 * is when the detach function in struct &drm_aperture_funcs comes into play.
 * When a driver calls drm_fb_helper_remove_conflicting_framebuffers() et al
 * for the registered framebuffer range, the DRM core calls struct
 * &drm_aperture_funcs.detach and the generic driver has to onload itself. It
 * may not access the device's registers, framebuffer memory, ROM, etc after
 * detach returned. If the driver supports hotplugging, detach can be treated
 * like an unplug event.
 *
 * .. code-block:: c
 *
 *	static void detach_from_device(struct drm_device *dev,
 *				       resource_size_t base,
 *				       resource_size_t size)
 *	{
 *		// Signal unplug
 *		drm_dev_unplug(dev);
 *
 *		// Maybe do other clean-up operations
 *		...
 *	}
 *
 *	static struct drm_aperture_funcs ap_funcs = {
 *		.detach = detach_from_device,
 *	};
 */

/**
 * struct drm_aperture - Represents a DRM framebuffer aperture
 *
 * This structure has no public fields.
 */
struct drm_aperture {
	struct drm_device *dev;
	resource_size_t base;
	resource_size_t size;

	const struct drm_aperture_funcs *funcs;

	struct list_head lh;
};

static LIST_HEAD(drm_apertures);

static DEFINE_MUTEX(drm_apertures_lock);

static bool overlap(resource_size_t base1, resource_size_t end1,
		    resource_size_t base2, resource_size_t end2)
{
	return (base1 < end2) && (end1 > base2);
}

static void devm_aperture_acquire_release(void *data)
{
	struct drm_aperture *ap = data;
	bool detached = !ap->dev;

	if (!detached)
		mutex_lock(&drm_apertures_lock);

	list_del(&ap->lh);

	if (!detached)
		mutex_unlock(&drm_apertures_lock);
}

/**
 * devm_aperture_acquire - Acquires ownership of a framebuffer on behalf of a DRM driver.
 * @dev:	the DRM device to own the framebuffer memory
 * @base:	the framebuffer's byte offset in physical memory
 * @size:	the framebuffer size in bytes
 * @funcs:	callback functions
 *
 * Installs the given device as the new owner. The function fails if the
 * framebuffer range, or parts of it, is currently owned by another driver.
 * To evict current owners, callers should use
 * drm_fb_helper_remove_conflicting_framebuffers() et al. before calling this
 * function. Acquired apertures are released automatically if the underlying
 * device goes away.
 *
 * Returns:
 * An instance of struct &drm_aperture on success, or a pointer-encoded
 * errno value otherwise.
 */
struct drm_aperture *
devm_aperture_acquire(struct drm_device *dev,
		      resource_size_t base, resource_size_t size,
		      const struct drm_aperture_funcs *funcs)
{
	size_t end = base + size;
	struct list_head *pos;
	struct drm_aperture *ap;
	int ret;

	mutex_lock(&drm_apertures_lock);

	list_for_each(pos, &drm_apertures) {
		ap = container_of(pos, struct drm_aperture, lh);
		if (overlap(base, end, ap->base, ap->base + ap->size))
			return ERR_PTR(-EBUSY);
	}

	ap = devm_kzalloc(dev->dev, sizeof(*ap), GFP_KERNEL);
	if (!ap)
		return ERR_PTR(-ENOMEM);

	ap->dev = dev;
	ap->base = base;
	ap->size = size;
	ap->funcs = funcs;
	INIT_LIST_HEAD(&ap->lh);

	list_add(&ap->lh, &drm_apertures);

	mutex_unlock(&drm_apertures_lock);

	ret = devm_add_action_or_reset(dev->dev, devm_aperture_acquire_release, ap);
	if (ret)
		return ERR_PTR(ret);

	return ap;
}
EXPORT_SYMBOL(devm_aperture_acquire);

void drm_aperture_detach_drivers(resource_size_t base, resource_size_t size)
{
	resource_size_t end = base + size;
	struct list_head *pos, *n;

	mutex_lock(&drm_apertures_lock);

	list_for_each_safe(pos, n, &drm_apertures) {
		struct drm_aperture *ap =
			container_of(pos, struct drm_aperture, lh);
		struct drm_device *dev = ap->dev;

		if (!overlap(base, end, ap->base, ap->base + ap->size))
			continue;

		ap->dev = NULL; /* detach from device */
		if (drm_WARN_ON(dev, !ap->funcs->detach))
			continue;
		ap->funcs->detach(dev, ap->base, ap->size);
	}

	mutex_unlock(&drm_apertures_lock);
}
EXPORT_SYMBOL(drm_aperture_detach_drivers);
