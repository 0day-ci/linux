// SPDX-License-Identifier: GPL-2.0+
/*
 * KMS driver for Loongson display controller
 */

/*
 * Authors:
 *      Sui Jingfeng <suijingfeng@loongson.cn>
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/of_reserved_mem.h>

#include <drm/drm_aperture.h>
#include <drm/drm_of.h>
#include <drm/drm_vblank.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_vram_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_probe_helper.h>

#include "lsdc_drv.h"
#include "lsdc_irq.h"
#include "lsdc_regs.h"
#include "lsdc_connector.h"
#include "lsdc_pll.h"

#define DRIVER_AUTHOR		"Sui Jingfeng <suijingfeng@loongson.cn>"
#define DRIVER_NAME		"lsdc"
#define DRIVER_DESC		"drm driver for loongson's display controller"
#define DRIVER_DATE		"20200701"
#define DRIVER_MAJOR		1
#define DRIVER_MINOR		0
#define DRIVER_PATCHLEVEL	0

static int lsdc_use_vram_helper = -1;
MODULE_PARM_DESC(use_vram_helper, "Using vram helper based driver(0 = disabled)");
module_param_named(use_vram_helper, lsdc_use_vram_helper, int, 0644);

static const struct lsdc_chip_desc dc_in_ls2k1000 = {
	.chip = LSDC_CHIP_2K1000,
	.num_of_crtc = LSDC_NUM_CRTC,
	/* ls2k1000 user manual say the max pixel clock can be about 200MHz */
	.max_pixel_clk = 200000,
	.max_width = 2560,
	.max_height = 2048,
	.num_of_hw_cursor = 1,
	.hw_cursor_w = 32,
	.hw_cursor_h = 32,
	.stride_alignment = 256,
	.have_builtin_i2c = false,
	.has_vram = false,
};

static const struct lsdc_chip_desc dc_in_ls2k0500 = {
	.chip = LSDC_CHIP_2K0500,
	.num_of_crtc = LSDC_NUM_CRTC,
	.max_pixel_clk = 200000,
	.max_width = 2048,
	.max_height = 2048,
	.num_of_hw_cursor = 1,
	.hw_cursor_w = 32,
	.hw_cursor_h = 32,
	.stride_alignment = 256,
	.have_builtin_i2c = false,
	.has_vram = false,
};

static const struct lsdc_chip_desc dc_in_ls7a1000 = {
	.chip = LSDC_CHIP_7A1000,
	.num_of_crtc = LSDC_NUM_CRTC,
	.max_pixel_clk = 200000,
	.max_width = 2048,
	.max_height = 2048,
	.num_of_hw_cursor = 1,
	.hw_cursor_w = 32,
	.hw_cursor_h = 32,
	.stride_alignment = 256,
	.have_builtin_i2c = true,
	.has_vram = true,
};

static enum drm_mode_status
lsdc_device_mode_valid(struct drm_device *ddev, const struct drm_display_mode *mode)
{
	struct lsdc_device *ldev = to_lsdc(ddev);

	if (ldev->use_vram_helper)
		return drm_vram_helper_mode_valid(ddev, mode);

	return MODE_OK;
}

static const struct drm_mode_config_funcs lsdc_mode_config_funcs = {
	.fb_create = drm_gem_fb_create,
	.output_poll_changed = drm_fb_helper_output_poll_changed,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
	.mode_valid = lsdc_device_mode_valid,
};

#ifdef CONFIG_DEBUG_FS
static int lsdc_show_clock(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = (struct drm_info_node *)m->private;
	struct drm_device *ddev = node->minor->dev;
	struct drm_crtc *crtc;

	drm_for_each_crtc(crtc, ddev) {
		struct lsdc_display_pipe *pipe;
		struct lsdc_pll *pixpll;
		const struct lsdc_pixpll_funcs *funcs;
		struct lsdc_pll_core_values params;
		unsigned int out_khz;
		struct drm_display_mode *adj;

		pipe = container_of(crtc, struct lsdc_display_pipe, crtc);
		if (!pipe->available)
			continue;

		adj = &crtc->state->adjusted_mode;

		pixpll = &pipe->pixpll;
		funcs = pixpll->funcs;
		out_khz = funcs->get_clock_rate(pixpll, &params);

		seq_printf(m, "Display pipe %u: %dx%d\n",
			   pipe->index, adj->hdisplay, adj->vdisplay);

		seq_printf(m, "Frequency actually output: %u kHz\n", out_khz);
		seq_printf(m, "Pixel clock required: %d kHz\n", adj->clock);
		seq_printf(m, "diff: %d kHz\n", adj->clock);

		seq_printf(m, "div_ref=%u, loopc=%u, div_out=%u\n",
			   params.div_ref, params.loopc, params.div_out);

		seq_printf(m, "hsync_start=%d, hsync_end=%d, htotal=%d\n",
			   adj->hsync_start, adj->hsync_end, adj->htotal);
		seq_printf(m, "vsync_start=%d, vsync_end=%d, vtotal=%d\n\n",
			   adj->vsync_start, adj->vsync_end, adj->vtotal);
	}

	return 0;
}

static int lsdc_show_mm(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = (struct drm_info_node *)m->private;
	struct drm_device *ddev = node->minor->dev;
	struct drm_printer p = drm_seq_file_printer(m);

	drm_mm_print(&ddev->vma_offset_manager->vm_addr_space_mm, &p);

	return 0;
}

static struct drm_info_list lsdc_debugfs_list[] = {
	{ "clocks", lsdc_show_clock, 0 },
	{ "mm",     lsdc_show_mm,   0, NULL },
};

static void lsdc_debugfs_init(struct drm_minor *minor)
{
	drm_debugfs_create_files(lsdc_debugfs_list,
				 ARRAY_SIZE(lsdc_debugfs_list),
				 minor->debugfs_root,
				 minor);
}
#endif

static struct drm_gem_object *
lsdc_drm_gem_create_object(struct drm_device *ddev, size_t size)
{
	struct drm_gem_cma_object *obj = kzalloc(sizeof(*obj), GFP_KERNEL);

	if (!obj)
		return ERR_PTR(-ENOMEM);

	obj->map_noncoherent = true;

	return &obj->base;
}

static int lsdc_gem_cma_dumb_create(struct drm_file *file,
				    struct drm_device *ddev,
				    struct drm_mode_create_dumb *args)
{
	struct lsdc_device *ldev = to_lsdc(ddev);
	const struct lsdc_chip_desc *desc = ldev->desc;
	unsigned int bytes_per_pixel = (args->bpp + 7) / 8;
	unsigned int pitch = bytes_per_pixel * args->width;

	/*
	 * The dc in ls7a1000/ls2k1000/ls2k0500 require the stride be a
	 * multiple of 256 bytes which is for sake of optimize dma data
	 * transfer.
	 */
	args->pitch = roundup(pitch, desc->stride_alignment);

	return drm_gem_cma_dumb_create_internal(file, ddev, args);
}

DEFINE_DRM_GEM_CMA_FOPS(lsdc_drv_fops);

static const struct drm_driver lsdc_drm_driver_cma_stub = {
	.driver_features = DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.lastclose = drm_fb_helper_lastclose,
	.fops = &lsdc_drv_fops,
	.gem_create_object = lsdc_drm_gem_create_object,

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,

	DRM_GEM_CMA_DRIVER_OPS_WITH_DUMB_CREATE(lsdc_gem_cma_dumb_create),

#ifdef CONFIG_DEBUG_FS
	.debugfs_init = lsdc_debugfs_init,
#endif
};

DEFINE_DRM_GEM_FOPS(lsdc_gem_fops);

static const struct drm_driver lsdc_vram_driver_stub = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,
	.fops = &lsdc_gem_fops,

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,

	DRM_GEM_VRAM_DRIVER,
};

static int lsdc_modeset_init(struct lsdc_device *ldev, const uint32_t num_crtc)
{
	struct drm_device *ddev = &ldev->drm;
	unsigned int i;
	int ret;

	/* first, find all available connector, and take a record */
	for (i = 0; i < num_crtc; i++) {
		struct lsdc_display_pipe *const dispipe = &ldev->disp_pipe[i];
		struct lsdc_connector *lconn = lsdc_connector_init(ldev, i);
		/* Fail if the connector could not be initialized */
		if (IS_ERR(lconn))
			return PTR_ERR(lconn);

		if (!lconn) {
			dispipe->lconn = NULL;
			dispipe->available = false;
			continue;
		}

		dispipe->available = true;
		dispipe->lconn = lconn;
		ldev->num_output++;
	}

	drm_info(ddev, "number of outputs: %u\n", ldev->num_output);

	for (i = 0; i < num_crtc; i++) {
		struct lsdc_display_pipe * const dispipe = &ldev->disp_pipe[i];
		struct drm_plane * const primary = &dispipe->primary;
		struct drm_plane * const cursor = &dispipe->cursor;
		struct drm_encoder * const encoder = &dispipe->encoder;
		struct drm_crtc * const crtc = &dispipe->crtc;
		struct lsdc_pll * const pixpll = &dispipe->pixpll;

		dispipe->index = i;

		ret = lsdc_pixpll_init(pixpll, ddev, i);
		if (ret)
			return ret;

		ret = lsdc_plane_init(ldev, primary, DRM_PLANE_TYPE_PRIMARY, i);
		if (ret)
			return ret;

		ret = lsdc_plane_init(ldev, cursor, DRM_PLANE_TYPE_CURSOR, i);
		if (ret)
			return ret;

		/*
		 * Initial all of the CRTC available, in this way the crtc
		 * index is equal to the hardware crtc index. That is:
		 * display pipe 0 = crtc0 + dvo0 + encoder0
		 * display pipe 1 = crtc1 + dvo1 + encoder1
		 */
		ret = lsdc_crtc_init(ddev, crtc, i, primary, cursor);
		if (ret)
			return ret;

		if (dispipe->available) {
			ret = lsdc_encoder_init(encoder,
						&dispipe->lconn->base,
						ddev,
						i,
						ldev->num_output);
			if (ret)
				return ret;
		}

		drm_info(ddev, "display pipe %u initialized\n", i);
	}

	return 0;
}

static int lsdc_mode_config_init(struct lsdc_device *ldev)
{
	const struct lsdc_chip_desc * const descp = ldev->desc;
	struct drm_device *ddev = &ldev->drm;
	int ret;

	spin_lock_init(&ldev->reglock);

	drm_mode_config_init(ddev);

	ddev->mode_config.funcs = &lsdc_mode_config_funcs;
	ddev->mode_config.min_width = 1;
	ddev->mode_config.min_height = 1;
	ddev->mode_config.preferred_depth = 24;
	ddev->mode_config.prefer_shadow = ldev->use_vram_helper;

	ddev->mode_config.max_width = 4096;
	ddev->mode_config.max_height = 4096;

	ddev->mode_config.cursor_width = descp->hw_cursor_h;
	ddev->mode_config.cursor_height = descp->hw_cursor_h;

	if (ldev->vram_base)
		ddev->mode_config.fb_base = ldev->vram_base;

	ret = lsdc_modeset_init(ldev, descp->num_of_crtc);
	if (ret)
		goto out_mode_config;

	drm_mode_config_reset(ddev);

	return 0;

out_mode_config:
	drm_mode_config_cleanup(ddev);

	return ret;
}

static void lsdc_mode_config_fini(struct drm_device *ddev)
{
	struct lsdc_device *ldev = to_lsdc(ddev);

	drm_kms_helper_poll_fini(ddev);

	drm_dev_unregister(ddev);

	devm_free_irq(ddev->dev, ldev->irq, ddev);

	drm_atomic_helper_shutdown(ddev);

	drm_mode_config_cleanup(ddev);
}

/*
 * lsdc_determine_chip - a function to tell different chips apart.
 */
static const struct lsdc_chip_desc *
lsdc_determine_chip(struct pci_dev *pdev, int *has)
{
	static const struct lsdc_match {
		char name[128];
		const void *data;
	} compat[] = {
		{ .name = "loongson,ls7a1000-dc", .data = &dc_in_ls7a1000 },
		{ .name = "loongson,ls2k1000-dc", .data = &dc_in_ls2k1000 },
		{ .name = "loongson,ls2k0500-dc", .data = &dc_in_ls2k0500 },
		{ .name = "loongson,loongson64c-4core-ls7a", .data = &dc_in_ls7a1000 },
		{ .name = "loongson,loongson64g-4core-ls7a", .data = &dc_in_ls7a1000 },
		{ .name = "loongson,loongson64-2core-2k1000", .data = &dc_in_ls2k1000 },
		{ .name = "loongson,loongson2k1000", .data = &dc_in_ls2k1000 },
		{ /* sentinel */ },
	};

	struct device_node *np;
	unsigned int i;

	/* Deduce DC variant information from the DC device node */
	for (i = 0; i < ARRAY_SIZE(compat); ++i) {
		np = of_find_compatible_node(NULL, NULL, compat[i].name);
		if (!np)
			continue;

		of_node_put(np);

		return compat[i].data;
	}

	return NULL;
}

static int lsdc_drm_suspend(struct device *dev)
{
	struct drm_device *ddev = dev_get_drvdata(dev);

	return drm_mode_config_helper_suspend(ddev);
}

static int lsdc_drm_resume(struct device *dev)
{
	struct drm_device *ddev = dev_get_drvdata(dev);

	return drm_mode_config_helper_resume(ddev);
}

static int lsdc_pm_freeze(struct device *dev)
{
	return lsdc_drm_suspend(dev);
}

static int lsdc_pm_thaw(struct device *dev)
{
	return lsdc_drm_resume(dev);
}

static int lsdc_pm_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	int error;

	error = lsdc_pm_freeze(dev);
	if (error)
		return error;

	pci_save_state(pdev);
	/* Shut down the device */
	pci_disable_device(pdev);
	pci_set_power_state(pdev, PCI_D3hot);

	return 0;
}

static int lsdc_pm_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	if (pcim_enable_device(pdev))
		return -EIO;

	pci_set_power_state(pdev, PCI_D0);

	pci_restore_state(pdev);

	return lsdc_pm_thaw(dev);
}

static const struct dev_pm_ops lsdc_pm_ops = {
	.suspend = lsdc_pm_suspend,
	.resume = lsdc_pm_resume,
	.freeze = lsdc_pm_freeze,
	.thaw = lsdc_pm_thaw,
	.poweroff = lsdc_pm_freeze,
	.restore = lsdc_pm_resume,
};

static int lsdc_remove_conflicting_framebuffers(const struct drm_driver *drv)
{
	/* lsdc is a pci device, but it don't have a dedicate vram bar because
	 * of historic reason. The display controller is ported from Loongson
	 * 2H series SoC which date back to 2012.
	 * And simplefb node may have been located anywhere in memory.
	 */
	return drm_aperture_remove_conflicting_framebuffers(0, ~0, false, drv);
}

static int lsdc_vram_init(struct lsdc_device *ldev)
{
	struct drm_device *ddev = &ldev->drm;
	struct pci_dev *gpu;
	resource_size_t base, size;
	int ret;

	/* BAR 2 of LS7A1000's GPU contain VRAM, It's GC1000 */
	gpu = pci_get_device(PCI_VENDOR_ID_LOONGSON, 0x7a15, NULL);
	base = pci_resource_start(gpu, 2);
	size =  pci_resource_len(gpu, 2);

	drm_info(ddev, "vram start: 0x%llx, size: %uMB\n", (u64)base, (u32)(size >> 20));

	if (!request_mem_region(base, size, "lsdc_vram")) {
		drm_err(ddev, "can't reserve VRAM memory region\n");
		return -ENXIO;
	}

	ret = drmm_vram_helper_init(ddev, base, size);
	if (ret) {
		drm_err(ddev, "can't init vram helper\n");
		return ret;
	}

	ldev->vram_base = base;
	ldev->vram_size = size;

	return 0;
}

static int lsdc_pci_probe(struct pci_dev *pdev, const struct pci_device_id * const ent)
{
	const struct drm_driver *driver = &lsdc_drm_driver_cma_stub;
	int has_dedicated_vram = 0;
	struct lsdc_device *ldev;
	struct drm_device *ddev;
	const struct lsdc_chip_desc *descp;
	int ret;

	descp = lsdc_determine_chip(pdev, &has_dedicated_vram);
	if (!descp) {
		dev_err(&pdev->dev, "unknown dc ip core, abort\n");
		return -ENOENT;
	}

	lsdc_remove_conflicting_framebuffers(driver);

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	pci_set_master(pdev);

	/* Get the optional framebuffer memory resource */
	ret = of_reserved_mem_device_init(&pdev->dev);
	if (ret && (ret != -ENODEV))
		return ret;

	if (lsdc_use_vram_helper > 0) {
		driver = &lsdc_vram_driver_stub;
	} else if ((lsdc_use_vram_helper < 0) && descp->has_vram) {
		lsdc_use_vram_helper = 1;
		driver = &lsdc_vram_driver_stub;
	} else {
		driver = &lsdc_drm_driver_cma_stub;
	}

	ldev = devm_drm_dev_alloc(&pdev->dev, driver, struct lsdc_device, drm);
	if (IS_ERR(ldev))
		return PTR_ERR(ldev);

	ldev->use_vram_helper = lsdc_use_vram_helper;
	ldev->desc = descp;

	/* BAR 0 contains registers */
	ldev->reg_base = devm_ioremap_resource(&pdev->dev, &pdev->resource[0]);
	if (IS_ERR(ldev->reg_base))
		return PTR_ERR(ldev->reg_base);

	if (descp->has_vram && ldev->use_vram_helper)
		lsdc_vram_init(ldev);

	ddev = &ldev->drm;
	pci_set_drvdata(pdev, ddev);

	ret = lsdc_mode_config_init(ldev);
	if (ret)
		goto err_out;

	ldev->irq = pdev->irq;

	drm_info(&ldev->drm, "irq = %d\n", ldev->irq);

	ret = devm_request_threaded_irq(&pdev->dev, pdev->irq,
					lsdc_irq_handler_cb,
					lsdc_irq_thread_cb,
					IRQF_ONESHOT,
					dev_name(&pdev->dev),
					ddev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register lsdc interrupt\n");
		goto err_out;
	}

	ret = drm_vblank_init(ddev, ldev->desc->num_of_crtc);
	if (ret) {
		dev_err(&pdev->dev, "Fatal error during vblank init: %d\n", ret);
		goto err_out;
	}

	drm_kms_helper_poll_init(ddev);

	ret = drm_dev_register(ddev, 0);
	if (ret)
		goto err_out;

	drm_fbdev_generic_setup(ddev, 32);

	return 0;

err_out:
	drm_dev_put(ddev);

	return ret;
}

static void lsdc_pci_remove(struct pci_dev *pdev)
{
	struct drm_device *ddev = pci_get_drvdata(pdev);

	lsdc_mode_config_fini(ddev);

	drm_dev_put(ddev);

	pci_clear_master(pdev);

	pci_release_regions(pdev);
}

static const struct pci_device_id lsdc_pciid_list[] = {
	{PCI_VENDOR_ID_LOONGSON, 0x7a06, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0, 0, 0, 0, 0, 0, 0}
};

static struct pci_driver lsdc_pci_driver = {
	.name = DRIVER_NAME,
	.id_table = lsdc_pciid_list,
	.probe = lsdc_pci_probe,
	.remove = lsdc_pci_remove,
	.driver.pm = &lsdc_pm_ops,
};

static int __init lsdc_drm_init(void)
{
	struct pci_dev *pdev = NULL;

	if (drm_firmware_drivers_only())
		return -EINVAL;

	while ((pdev = pci_get_class(PCI_CLASS_DISPLAY_VGA << 8, pdev))) {
		/*
		 * Multiple video card workaround
		 *
		 * This integrated video card will always be selected as
		 * default boot device by vgaarb subsystem.
		 */
		if (pdev->vendor != PCI_VENDOR_ID_LOONGSON) {
			pr_info("Discrete graphic card detected, abort\n");
			return 0;
		}
	}

	return pci_register_driver(&lsdc_pci_driver);
}
module_init(lsdc_drm_init);

static void __exit lsdc_drm_exit(void)
{
	pci_unregister_driver(&lsdc_pci_driver);
}
module_exit(lsdc_drm_exit);

MODULE_DEVICE_TABLE(pci, lsdc_pciid_list);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL v2");
