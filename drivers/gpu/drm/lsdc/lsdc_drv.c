// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2020 Loongson Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 */

/*
 * Authors:
 *      Sui Jingfeng <suijingfeng@loongson.cn>
 */

#include <linux/errno.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/of_reserved_mem.h>

#include <drm/drm_drv.h>
#include <drm/drm_aperture.h>
#include <drm/drm_of.h>
#include <drm/drm_plane.h>
#include <drm/drm_vblank.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_damage_helper.h>
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

static int lsdc_modeset = 1;
MODULE_PARM_DESC(modeset, "Enable/disable CMA-based KMS(1 = enabled(default), 0 = disabled)");
module_param_named(modeset, lsdc_modeset, int, 0644);

static int lsdc_cached_coherent = 1;
MODULE_PARM_DESC(cached_coherent, "using cached coherent mapping(1 = enabled(default), 0 = disabled)");
module_param_named(cached_coherent, lsdc_cached_coherent, int, 0644);

static int lsdc_dirty_update = -1;
MODULE_PARM_DESC(dirty_update, "enable dirty update(1 = enabled, 0 = disabled(default))");
module_param_named(dirty_update, lsdc_dirty_update, int, 0644);

static int lsdc_use_vram_helper = -1;
MODULE_PARM_DESC(use_vram_helper, "use vram helper(1 = enabled, 0 = disabled(default))");
module_param_named(use_vram_helper, lsdc_use_vram_helper, int, 0644);


static const struct lsdc_chip_desc dc_in_ls2k1000 = {
	.chip = LSDC_CHIP_2K1000,
	.num_of_crtc = LSDC_MAX_CRTC,
	/* ls2k1000 user manual say the max pixel clock can be about 200MHz */
	.max_pixel_clk = 200000,
	.max_width = 2560,
	.max_height = 2048,
	.num_of_hw_cursor = 1,
	.hw_cursor_w = 32,
	.hw_cursor_h = 32,
	.have_builtin_i2c = false,
};

static const struct lsdc_chip_desc dc_in_ls2k0500 = {
	.chip = LSDC_CHIP_2K0500,
	.num_of_crtc = LSDC_MAX_CRTC,
	.max_pixel_clk = 200000,
	.max_width = 2048,
	.max_height = 2048,
	.num_of_hw_cursor = 1,
	.hw_cursor_w = 32,
	.hw_cursor_h = 32,
	.have_builtin_i2c = false,
};

static const struct lsdc_chip_desc dc_in_ls7a1000 = {
	.chip = LSDC_CHIP_7A1000,
	.num_of_crtc = LSDC_MAX_CRTC,
	.max_pixel_clk = 200000,
	.max_width = 2048,
	.max_height = 2048,
	.num_of_hw_cursor = 1,
	.hw_cursor_w = 32,
	.hw_cursor_h = 32,
	.have_builtin_i2c = true,
};


static const struct of_device_id lsdc_drm_of_match[] = {
	{ .compatible = "loongson,loongson64c-4core-ls7a", .data = &dc_in_ls7a1000 },
	{ .compatible = "loongson,loongson64g-4core-ls7a", .data = &dc_in_ls7a1000 },
	{ .compatible = "loongson,loongson64-2core-2k1000", .data = &dc_in_ls2k1000 },
	{ .compatible = "loongson,loongson2k1000", .data = &dc_in_ls2k1000 },
	{ .compatible = "loongson,ls2k1000", .data = &dc_in_ls2k1000 },
	{ .compatible = "loongson,display-subsystem", .data = &dc_in_ls2k1000 },
	{ .compatible = "loongson,ls-fb", .data = &dc_in_ls2k1000 },
	{ .compatible = "loongson,loongson2k0500", .data = &dc_in_ls2k0500 },
	{ /* sentinel */ },
};


static struct drm_framebuffer *
lsdc_drm_gem_fb_create(struct drm_device *ddev, struct drm_file *file,
			  const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct lsdc_device *ldev = to_lsdc(ddev);

	if (ldev->dirty_update)
		return drm_gem_fb_create_with_dirty(ddev, file, mode_cmd);

	return drm_gem_fb_create(ddev, file, mode_cmd);
}


static enum drm_mode_status lsdc_device_mode_valid(struct drm_device *ddev,
					const struct drm_display_mode *mode)
{
#ifdef CONFIG_DRM_LSDC_VRAM_DRIVER
	struct lsdc_device *ldev = to_lsdc(ddev);

	if (ldev->use_vram_helper == true)
		return drm_vram_helper_mode_valid(ddev, mode);
#endif

	return MODE_OK;
}


static const struct drm_mode_config_funcs lsdc_mode_config_funcs = {
	.fb_create = lsdc_drm_gem_fb_create,
	.output_poll_changed = drm_fb_helper_output_poll_changed,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
	.mode_valid = lsdc_device_mode_valid,
};


#ifdef CONFIG_DEBUG_FS
static int lsdc_show_pxlclock(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = (struct drm_info_node *)m->private;
	struct drm_device *ddev = node->minor->dev;
	struct lsdc_device *ldev = to_lsdc(ddev);
	struct drm_crtc *crtc;

	drm_for_each_crtc(crtc, ddev) {
		struct drm_crtc_state *state = crtc->state;
		int index = drm_crtc_index(crtc);
		struct lsdc_display_pipe *pipe = &ldev->disp_pipe[index];
		struct lsdc_pll *pixpll = &pipe->pixpll;
		const struct lsdc_pixpll_funcs *clkfun = pixpll->funcs;
		unsigned int clkrate = clkfun->get_clock_rate(pixpll);
		unsigned int mode_clock = crtc->mode.crtc_clock;

		seq_printf(m, "\n");
		seq_printf(m, "CRTC%u, %s, %s\n", index,
				state->active ? "active" : "non-active",
				state->enable ? "enabled" : "disabled");
		seq_printf(m, "hw clock: %u kHz\n", clkrate);
		seq_printf(m, "mode: %u kHz\n", mode_clock);

		seq_printf(m, "div_out=%u, loopc=%u, div_ref=%u\n",
				pixpll->core_params.div_out,
				pixpll->core_params.loopc,
				pixpll->core_params.div_ref);
	}

	return 0;
}


static int lsdc_mm_show(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *ddev = node->minor->dev;
	struct drm_printer p = drm_seq_file_printer(m);

	drm_mm_print(&ddev->vma_offset_manager->vm_addr_space_mm, &p);
	return 0;
}

static struct drm_info_list lsdc_debugfs_list[] = {
	{ "clocks", lsdc_show_pxlclock, 0 },
	{ "mm",     lsdc_mm_show,   0, NULL },
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
	struct lsdc_device *ldev = to_lsdc(ddev);
	struct drm_gem_cma_object *obj;

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj)
		return ERR_PTR(-ENOMEM);

	if (ldev->cached_coherent)
		obj->map_noncoherent = true;

	return &obj->base;
}


static int lsdc_gem_cma_dumb_create(struct drm_file *file,
				    struct drm_device *ddev,
				    struct drm_mode_create_dumb *args)
{
	unsigned int bytes_per_pixel = (args->bpp + 7) / 8;
	unsigned int pitch = bytes_per_pixel * args->width;

	/*
	 * loongson's display controller require the pitch be a multiple
	 * of 256 bytes, which is for optimize dma data transfer
	 */
	args->pitch = roundup(pitch, 256);

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


DEFINE_DRM_GEM_FOPS(lsdc_fops);


#ifdef CONFIG_DRM_LSDC_VRAM_DRIVER
static const struct drm_driver lsdc_vram_driver_stub = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,
	.fops = &lsdc_fops,

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,

	DRM_GEM_VRAM_DRIVER,
};
#endif


static int lsdc_modeset_init(struct lsdc_device *ldev, const uint32_t num_crtc)
{
	struct drm_device *ddev = &ldev->drm;
	struct lsdc_display_pipe *dispipe;
	struct lsdc_connector *lconn;
	unsigned int i;
	int ret;

	/* first find all of connector available */
	for (i = 0; i < num_crtc; i++) {
		lconn = lsdc_connector_init(ldev, i);
		dispipe = &ldev->disp_pipe[i];

		if (IS_ERR(lconn))
			return PTR_ERR(lconn);
		else if (lconn == NULL) {
			dispipe->lconn = NULL;
			dispipe->available = false;
			continue;
		}

		/* take a record */
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
	ddev->mode_config.prefer_shadow = 0;

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

	/* disable output polling */
	drm_kms_helper_poll_fini(ddev);

	drm_dev_unregister(ddev);

	devm_free_irq(ddev->dev, ldev->irq, ddev);

	/* shutdown all CRTC, for driver unloading */
	drm_atomic_helper_shutdown(ddev);

	drm_mode_config_cleanup(ddev);
}

/*
 * There are difference between the dc in ls7a1000 and the dc in ls2k1000,
 * ls7a1000 have two builtin gpio emulated i2c, but ls2k1000 don't have this.
 * ls2k1000 grab i2c adapter from other device driver, eithor hardware i2c or
 * external gpio-emulated i2c.
 *
 * Beside, the pixel pll unit is also different, thererfore we a function to
 * tell different chips apart.
 */
static int lsdc_determine_chip(struct lsdc_device *ldev)
{
	struct device_node *np;
	const char *model;
	const char *compat;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(lsdc_drm_of_match); ++i) {
		compat = lsdc_drm_of_match[i].compatible;

		np = of_find_compatible_node(NULL, NULL, compat);
		if (np) {

			/* additional information */
			of_property_read_string(np, "model", &model);

			of_node_put(np);

			ldev->desc = lsdc_drm_of_match[i].data;

			break;
		}
	}

	if (ldev->desc == NULL) {
		drm_err(&ldev->drm, "unknown dc ip core, abort\n");
		return -ENOENT;
	}

	drm_info(&ldev->drm, "%s found, model: %s\n", compat, model);

	return 0;
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
	/* lsdc is a pci device, but it don't have a dedicate vram bar
	 * because of historic reason. And simplefb node may have been
	 * located anywhere in memory.
	 */
	return drm_aperture_remove_conflicting_framebuffers(0, ~0, false, drv);
}


static int lsdc_vram_init(struct lsdc_device *ldev)
{
	struct drm_device *ddev = &ldev->drm;
	struct pci_dev *gpu;
	resource_size_t base, size;
	int ret;

	/* BAR 2 of LS7A1000's GPU contain VRAM */
	gpu = pci_get_device(PCI_VENDOR_ID_LOONGSON, PCI_DEVICE_ID_GPU, NULL);
	base = pci_resource_start(gpu, 2);
	size =  pci_resource_len(gpu, 2);

	drm_info(ddev, "vram start: 0x%llx, size: %lluMB\n", base, size >> 20);

	if (!request_mem_region(base, size, "lsdc_vram")) {
		drm_err(ddev, "can't reserve VRAM memory region\n");
		return -ENXIO;
	}

	if (ldev->use_vram_helper) {
#ifdef CONFIG_DRM_LSDC_VRAM_DRIVER
		ret = drmm_vram_helper_init(ddev, base, size);
		if (ret) {
			drm_err(ddev, "can't init vram helper\n");
			return ret;
		}
#endif
	} else if (ldev->dirty_update) {
		ldev->vram = devm_ioremap_wc(ddev->dev, base, size);
		if (ldev->vram == NULL)
			return -ENOMEM;

		drm_info(ddev, "vram virtual addr: 0x%llx\n", (u64)ldev->vram);
	}

	ldev->vram_base = base;
	ldev->vram_size = size;

	return 0;
}


static bool lsdc_should_vram_helper_based(void)
{
	static const char * const dc_compat[] = { "pci0014,7a06.0",
						  "pci0014,7a06" };
	bool ret = false;
	struct device_node *np;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(dc_compat); ++i) {
		np = of_find_compatible_node(NULL, NULL, dc_compat[i]);
		if (!np)
			continue;

		ret = of_property_read_bool(np, "use_vram_helper");
		of_node_put(np);
		break;
	}

	if (ret)
		DRM_INFO("using vram base solution dictated by device tree\n");

	return ret;
}


static int lsdc_pci_probe(struct pci_dev *pdev,
			  const struct pci_device_id * const ent)
{
	const struct drm_driver *driver = &lsdc_drm_driver_cma_stub;
	struct lsdc_device *ldev;
	struct drm_device *ddev;
	int ret;

	lsdc_remove_conflicting_framebuffers(driver);

	ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(&pdev->dev, "Set DMA Mask failed\n");
		return ret;
	}

	ret = pcim_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "Enable pci devive failed\n");
		return ret;
	}

	pci_set_master(pdev);

	/* Get the optional framebuffer memory resource */
	ret = of_reserved_mem_device_init(&pdev->dev);
	if (ret && ret != -ENODEV)
		return ret;

#ifdef CONFIG_DRM_LSDC_VRAM_DRIVER
	if ((lsdc_use_vram_helper > 0) || lsdc_should_vram_helper_based()) {
		driver = &lsdc_vram_driver_stub;
		DRM_INFO("using vram helper based solution\n");
	}
#endif

	ldev = devm_drm_dev_alloc(&pdev->dev,
				  driver,
				  struct lsdc_device,
				  drm);
	if (IS_ERR(ldev))
		return PTR_ERR(ldev);

	ddev = &ldev->drm;

	pci_set_drvdata(pdev, ddev);

	if (lsdc_use_vram_helper > 0)
		ldev->use_vram_helper = true;
	else {
		if (lsdc_cached_coherent > 0) {
			ldev->cached_coherent = true;
			drm_info(ddev, "with hardware maintained cached coherent\n");
		}

		if (lsdc_dirty_update > 0) {
			ldev->dirty_update = true;
			drm_info(ddev, "dirty update enabled\n");
		}
	}

	ret = lsdc_determine_chip(ldev);
	if (ret)
		return ret;

	/* BAR 0 contains registers */
	ldev->reg_base = devm_ioremap_resource(&pdev->dev, &pdev->resource[0]);
	if (IS_ERR(ldev->reg_base))
		return PTR_ERR(ldev->reg_base);

	/* LS2K1000/LS2K0500 is SoC, don't have a VRAM */
	if ((ldev->desc->chip == LSDC_CHIP_7A1000) &&
	    (ldev->use_vram_helper || ldev->dirty_update))
		lsdc_vram_init(ldev);

	ret = lsdc_mode_config_init(ldev);
	if (ret)
		goto err_out;


	ldev->irq = pdev->irq;

	dev_info(&pdev->dev, "irq = %d\n", ldev->irq);

	ret = devm_request_threaded_irq(&pdev->dev,
					pdev->irq,
					lsdc_irq_handler_cb,
					lsdc_irq_thread_cb,
					IRQF_ONESHOT,
					dev_name(&pdev->dev),
					ddev);

	if (ret) {
		dev_err(&pdev->dev, "Failed to register lsdc interrupt\n");
		goto err_out;
	}

	ret = drm_vblank_init(ddev, LSDC_MAX_CRTC);
	if (ret) {
		dev_err(&pdev->dev,
				"Fatal error during vblank init: %d\n", ret);
		goto err_out;
	}

	/* Initialize and enable output polling */
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
	.name = "lsdc",
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

	if (lsdc_modeset == 0)
		return -ENOENT;

	while ((pdev = pci_get_class(PCI_CLASS_DISPLAY_VGA << 8, pdev))) {
		/*
		 * Multiple video card workaround
		 *
		 * This integrated video driver will always be selected as
		 * default boot device by vgaarb system.
		 */
		if (pdev->vendor != PCI_VENDOR_ID_LOONGSON) {
			DRM_INFO("Discrete graphic card detected, abort\n");
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
