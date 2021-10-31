// SPDX-License-Identifier: GPL-2.0+
/*
 * Framebuffer driver for Visualize FX cards commonly found in PA-RISC machines
 *
 * Copyright (c) 2021 Sven Schnelle <svens@stackframe.org>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/fb.h>
#include <linux/delay.h>
#include <linux/rational.h>

#define VISFX_VRAM_ENDIANESS		0xa4303c
#define VISFX_VRAM_ENDIANESS_BIG	0xe4e4e4e4
#define VISFX_VRAM_ENDIANESS_LITTLE	0x1b1b1b1b

#define VISFX_STATUS			0x641400

#define VISFX_COLOR_MASK		0x800018
#define VISFX_COLOR_INDEX		0x800020
#define VISFX_COLOR_VALUE		0x800024

#define VISFX_SYNC_POLARITY		0x800044
#define VISFX_SYNC_VISIBLE_SIZE		0x80005c
#define VISFX_SYNC_HORIZ_CONF		0x800060
#define VISFX_SYNC_VERT_CONF		0x800068
#define VISFX_SYNC_MASTER_PLL		0x8000a0
#define VISFX_SYNC_PLL_STATUS		0x8000b8

#define VISFX_VRAM_WRITE_MODE		0xa00808
#define VISFX_VRAM_MASK			0xa0082c
#define VISFX_FGCOLOR			0xa0083c
#define VISFX_BGCOLOR			0xa00844
#define VISFX_WRITE_MASK		0xa0084c
#define VISFX_VRAM_WRITE_DATA_INCRX	0xa60000
#define VISFX_VRAM_WRITE_DATA_INCRY	0xa68000
#define VISFX_SCREEN_SIZE		0xac1054
#define VISFX_VRAM_WRITE_DEST		0xac1000

#define VISFX_START			0xb3c000
#define VISFX_SIZE			0xb3c808
#define VISFX_HEIGHT			0xb3c008
#define VISFX_DST			0xb3cc00

#define VISFX_DFP_ENABLE		0x10000
#define VISFX_HSYNC_POSITIVE		0x40000
#define VISFX_VSYNC_POSITIVE		0x80000

#define VISFX_SYNC_PLL_BASE		49383 /* 20.25MHz in ps */

#define VISFX_CURSOR_POS		0x400000
#define VISFX_CURSOR_INDEX		0x400004
#define VISFX_CURSOR_DATA		0x400008
#define VISFX_CURSOR_COLOR		0x400010
#define VISFX_CURSOR_ENABLE		0x80000000

#define VISFX_VRAM_WRITE_MODE_BITMAP	0x02000000
#define VISFX_VRAM_WRITE_MODE_COLOR	0x050004c0
#define VISFX_VRAM_WRITE_MODE_FILL	0x05000080

#define VISFX_FB_LENGTH			0x01000000
#define VISFX_FB_OFFSET			0x01000000
#define NR_PALETTE 256

struct visfx_par {
	u32 pseudo_palette[256];
	unsigned long debug_reg;
	void __iomem *reg_base;
	unsigned long reg_size;
	int open_count;
};

static u32 visfx_readl(struct fb_info *info, int reg)
{
	struct visfx_par *par = info->par;

	return le32_to_cpu(readl(par->reg_base + reg));
}

static void visfx_writel(struct fb_info *info, int reg, u32 val)
{
	struct visfx_par *par = info->par;

	return writel(cpu_to_le32(val), par->reg_base + reg);
}

static void visfx_write_vram(struct fb_info *info, int reg, u32 val)
{
	struct visfx_par *par = info->par;

	return writel(val, par->reg_base + reg);
}

static void visfx_bmove_wait(struct fb_info *info)
{
	while (visfx_readl(info, VISFX_STATUS));
}

static ssize_t visfx_sysfs_show_reg(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct fb_info *info = pci_get_drvdata(container_of(dev, struct pci_dev, dev));
	struct visfx_par *par = info->par;

	return sprintf(buf, "%08x\n", visfx_readl(info, par->debug_reg));
}

static ssize_t visfx_sysfs_store_reg(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct fb_info *info = pci_get_drvdata(container_of(dev, struct pci_dev, dev));
	struct visfx_par *par = info->par;
	unsigned long data;
	char *p;

	p = strchr(buf, '=');
	if (p)
		*p = '\0';

	if (kstrtoul(buf, 16, &par->debug_reg))
		return -EINVAL;

	if (par->debug_reg > par->reg_size)
		return -EINVAL;

	if (p) {
		if (kstrtoul(p+1, 16, &data))
			return -EINVAL;
		visfx_writel(info, par->debug_reg, data);
	}
	return count;
}

static DEVICE_ATTR(reg, 0600, visfx_sysfs_show_reg, visfx_sysfs_store_reg);

static void visfx_set_vram_addr(struct fb_info *info, int x, int y)
{
	visfx_writel(info, VISFX_VRAM_WRITE_DEST, (y << 16) | x);
}

static void visfx_set_bmove_color(struct fb_info *info, int fg, int bg)
{
	visfx_writel(info, VISFX_BGCOLOR, 0x01010101 * bg);
	visfx_writel(info, VISFX_FGCOLOR, 0x01010101 * fg);
}

static void visfx_imageblit_mono(struct fb_info *info, const char *data, int dx, int dy,
				 int width, int height, int fg_color, int bg_color)
{
	int _width, x, y;
	u32 tmp;

	visfx_set_bmove_color(info, fg_color, bg_color);
	visfx_writel(info, VISFX_VRAM_WRITE_MODE, VISFX_VRAM_WRITE_MODE_COLOR);
	visfx_writel(info, VISFX_VRAM_MASK, 0xffffffff);

	for (x = 0, _width = width; _width > 0; _width -= 32, x += 4) {
		visfx_set_vram_addr(info, dx + x * 8, dy);
		if (_width >= 32) {
			for (y = 0; y < height; y++) {
				memcpy(&tmp, &data[y * (width / 8) + x], 4);
				visfx_write_vram(info, VISFX_VRAM_WRITE_DATA_INCRY, tmp);
			}
		} else {
			visfx_writel(info, VISFX_VRAM_MASK, GENMASK(31, 31 - _width + 1));
			for (y = 0; y < height; y++) {
				tmp = 0;
				memcpy(&tmp, &data[y * (width / 8) + x], ((_width-1)/8)+1);
				visfx_write_vram(info, VISFX_VRAM_WRITE_DATA_INCRY, tmp);
			}
		}
	}
}

static void visfx_setup_unknown(struct fb_info *info)
{
	visfx_writel(info, 0xb08044, 0x1b);
	visfx_writel(info, 0xb08048, 0x1b);
	visfx_writel(info, 0x920860, 0xe4);
	visfx_writel(info, 0xa00818, 0);
	visfx_writel(info, 0xa00404, 0);
	visfx_writel(info, 0x921110, 0);
	visfx_writel(info, 0x9211d8, 0);
	visfx_writel(info, 0xa0086c, 0);
	visfx_writel(info, 0x921114, 0);
	visfx_writel(info, 0xac1050, 0);
	visfx_writel(info, 0xa00858, 0xb0);

	visfx_writel(info, VISFX_VRAM_WRITE_MODE, VISFX_VRAM_WRITE_MODE_BITMAP);
	visfx_writel(info, VISFX_WRITE_MASK, 0xffffffff);
	visfx_writel(info, VISFX_VRAM_MASK, 0xffffffff);
#ifdef __BIG_ENDIAN
	visfx_writel(info, VISFX_VRAM_ENDIANESS, VISFX_VRAM_ENDIANESS_BIG);
#else
	visfx_writel(info, VISFX_VRAM_ENDIANESS, VISFX_VRAM_ENDIANESS_LITTLE);
#endif
}

static void visfx_imageblit(struct fb_info *info, const struct fb_image *image)
{
	int x, y;

	visfx_bmove_wait(info);
	visfx_writel(info, VISFX_WRITE_MASK, 0xffffffff);

	switch (image->depth) {
	case 1:
		visfx_imageblit_mono(info, image->data, image->dx, image->dy,
				     image->width, image->height,
				     image->fg_color, image->bg_color);
		break;
	case 8:
		visfx_writel(info, VISFX_VRAM_WRITE_MODE, VISFX_VRAM_WRITE_MODE_BITMAP);

		for (y = 0; y < image->height; y++) {
			u32 data = 0;
			int pos = 0;

			visfx_writel(info, VISFX_WRITE_MASK, 0xffffffff);
			visfx_set_vram_addr(info, image->dx, image->dy + y);

			for (x = 0; x < image->width; x++) {
				pos = x & 3;
				data |= ((u8 *)image->data)[y * image->height + x] << (pos * 8);
				if (pos == 3) {
					visfx_write_vram(info, VISFX_VRAM_WRITE_DATA_INCRX, data);
					data = 0;
				}
			}

			if (x && pos != 3) {
				visfx_write_vram(info, VISFX_WRITE_MASK, (1 << ((pos+1) * 8))-1);
				visfx_write_vram(info, VISFX_VRAM_WRITE_DATA_INCRX, data);
			}
		}
		break;

	default:
		break;
	}
}

static void visfx_fillrect(struct fb_info *info, const struct fb_fillrect *fr)
{
	visfx_bmove_wait(info);
	visfx_writel(info, VISFX_WRITE_MASK, 0xffffffff);
	visfx_writel(info, VISFX_VRAM_WRITE_MODE, VISFX_VRAM_WRITE_MODE_FILL);
	visfx_set_bmove_color(info, fr->color, 0);
	visfx_writel(info, VISFX_START, (fr->dx << 16) | fr->dy);
	visfx_writel(info, VISFX_SIZE, (fr->width << 16) | fr->height);
}

static u32 visfx_cmap_entry(struct fb_cmap *cmap, int color)
{
	return (((cmap->blue[color] & 0xff)) |
		((cmap->green[color] & 0xff) << 8) |
		(cmap->red[color] & 0xff) << 16);
}

static int visfx_setcmap(struct fb_cmap *cmap, struct fb_info *info)
{
	int i;

	visfx_writel(info, VISFX_COLOR_INDEX, cmap->start);

	for (i = 0; i < cmap->len; i++)
		visfx_writel(info, VISFX_COLOR_VALUE, visfx_cmap_entry(cmap, i));

	visfx_writel(info, VISFX_COLOR_MASK, 0xff);
	visfx_writel(info, 0x80004c, 0xc);
	visfx_writel(info, 0x800000, 0);
	return 0;
}

static void visfx_get_video_mode(struct fb_info *info)
{
	struct fb_var_screeninfo *var = &info->var;
	unsigned long n, d;
	u32 tmp;

	tmp = visfx_readl(info, VISFX_SYNC_VISIBLE_SIZE);
	var->xres = (tmp & 0xffff) + 1;
	var->yres = (tmp >> 16) + 1;

	tmp = visfx_readl(info, VISFX_SYNC_MASTER_PLL);
	n = (tmp & 0xff) + 1;
	d = ((tmp >> 8) & 0xff) + 1;
	var->pixclock = (VISFX_SYNC_PLL_BASE / d) * n;

	tmp = visfx_readl(info, VISFX_SYNC_HORIZ_CONF);
	var->left_margin = ((tmp >> 20) & 0x1ff) + 1;
	var->hsync_len = (((tmp >> 12) & 0xff) + 1) * 4;
	var->right_margin = (tmp & 0x1ff) + 1;

	tmp = visfx_readl(info, VISFX_SYNC_VERT_CONF);
	var->upper_margin = ((tmp >> 16) & 0xff) + 1;
	var->vsync_len = ((tmp >> 8) & 0xff) + 1;
	var->lower_margin = (tmp & 0xff) + 1;

	tmp = visfx_readl(info, VISFX_SYNC_POLARITY);
	if (tmp & VISFX_HSYNC_POSITIVE)
		var->sync |= FB_SYNC_HOR_HIGH_ACT;
	if (tmp & VISFX_VSYNC_POSITIVE)
		var->sync |= FB_SYNC_VERT_HIGH_ACT;

	var->red.length = 8;
	var->green.length = 8;
	var->blue.length = 8;
	var->bits_per_pixel = 8;
	var->grayscale = 0;
	var->xres_virtual = var->xres;
	var->yres_virtual = var->yres;
	info->screen_size = 2048 * var->yres;
}

static void visfx_set_pll(struct fb_info *info, unsigned long clock)
{
	unsigned long n, d, tmp;

	rational_best_approximation(clock, VISFX_SYNC_PLL_BASE, 0x3f, 0x3f, &n, &d);
	tmp = (((d * 4) - 1) << 8) | ((n * 4) - 1);
	visfx_writel(info, VISFX_SYNC_MASTER_PLL, 0x520000 | tmp);
	while (visfx_readl(info, VISFX_SYNC_PLL_STATUS) & 0xffffff)
		udelay(10);
	visfx_writel(info, VISFX_SYNC_MASTER_PLL, 0x530000 | tmp);
	while (visfx_readl(info, VISFX_SYNC_PLL_STATUS) & 0xffffff)
		udelay(10);
}

static int visfx_set_par(struct fb_info *info)
{
	u32 xres, yres, hbp, hsw, hfp, vbp, vsw, vfp, tmp;
	struct fb_var_screeninfo *var = &info->var;


	xres = var->xres;
	yres = var->yres;
	hsw = var->hsync_len / 4 - 1;
	hfp = var->right_margin - 1;
	hbp = var->left_margin - 1;
	vsw = var->vsync_len - 1;
	vfp = var->lower_margin - 1;
	vbp = var->upper_margin - 1;

	visfx_set_pll(info, var->pixclock);
	visfx_writel(info, VISFX_SYNC_VISIBLE_SIZE, ((yres - 1) << 16) | (xres - 1));
	visfx_writel(info, VISFX_SYNC_HORIZ_CONF, (hbp << 20) | (hsw << 12) | (0xc << 8) | hfp);
	visfx_writel(info, VISFX_SYNC_VERT_CONF, (vbp << 16) | (vsw << 8) | vfp);
	visfx_writel(info, VISFX_SCREEN_SIZE, (xres << 16) | yres);

	tmp = VISFX_DFP_ENABLE;
	if (var->sync & FB_SYNC_HOR_HIGH_ACT)
		tmp |= VISFX_HSYNC_POSITIVE;
	if (var->sync & FB_SYNC_VERT_HIGH_ACT)
		tmp |= VISFX_VSYNC_POSITIVE;
	visfx_writel(info, VISFX_SYNC_POLARITY, tmp);

	visfx_writel(info, VISFX_VRAM_WRITE_MODE, VISFX_VRAM_WRITE_MODE_BITMAP);

	visfx_get_video_mode(info);
	return 0;
}

static int visfx_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	if (var->pixclock > VISFX_SYNC_PLL_BASE ||
	    var->left_margin > 512 ||
	    var->right_margin > 512 ||
	    var->hsync_len > 512 ||
	    var->lower_margin > 256 ||
	    var->upper_margin > 256 ||
	    var->vsync_len > 256 ||
		var->xres > 2048 ||
		var->yres > 2048)
		return -EINVAL;
	return 0;
}

static void visfx_update_cursor_image_line(struct fb_info *info,
					   struct fb_cursor *cursor, int y)
{
	unsigned int x, bytecnt;
	u32 data[2] = { 0 };
	u8 d, m;

	bytecnt = cursor->image.width / 8;

	for (x = 0; x < bytecnt && x < 8; x++) {
		m = cursor->mask[y * bytecnt + x];
		d = cursor->image.data[y * bytecnt + x];

		if (cursor->rop == ROP_XOR)
			((u8 *)data)[x] = d ^ m;
		else
			((u8 *)data)[x] = d & m;
	}

	visfx_writel(info, VISFX_CURSOR_DATA, data[0]);
	visfx_writel(info, VISFX_CURSOR_DATA, data[1]);
}

static void visfx_update_cursor_image(struct fb_info *info,
				      struct fb_cursor *cursor)
{
	int y, height = cursor->image.height;

	if (height > 128)
		height = 128;

	visfx_writel(info, VISFX_CURSOR_INDEX, 0);
	for (y = 0; y < height; y++)
		visfx_update_cursor_image_line(info, cursor, y);

	for (; y < 256; y++)
		visfx_writel(info, VISFX_CURSOR_DATA, 0);
}

static int visfx_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
	u32 tmp;

	if (cursor->set & (FB_CUR_SETIMAGE|FB_CUR_SETSHAPE))
		visfx_update_cursor_image(info, cursor);

	if (cursor->set & FB_CUR_SETCMAP) {
		tmp = visfx_cmap_entry(&info->cmap, cursor->image.fg_color);
		visfx_writel(info, VISFX_CURSOR_COLOR, tmp);
	}

	tmp = (cursor->image.dx << 16) | (cursor->image.dy & 0xffff);
	if (cursor->enable)
		tmp |= VISFX_CURSOR_ENABLE;
	visfx_writel(info, VISFX_CURSOR_POS, tmp);
	return 0;
}

static int visfx_open(struct fb_info *info, int user)
{
	struct visfx_par *par = info->par;

	if (user && par->open_count++ == 0)
		visfx_writel(info, VISFX_VRAM_WRITE_MODE, VISFX_VRAM_WRITE_MODE_BITMAP);

	return 0;
}

static int visfx_release(struct fb_info *info, int user)
{
	struct visfx_par *par = info->par;

	if (user)
		par->open_count--;

	return 0;
}

static const struct fb_ops visfx_ops = {
	.owner		= THIS_MODULE,
	.fb_open	= visfx_open,
	.fb_release	= visfx_release,
	.fb_setcmap	= visfx_setcmap,
	.fb_fillrect	= visfx_fillrect,
	.fb_imageblit	= visfx_imageblit,
	.fb_set_par	= visfx_set_par,
	.fb_check_var	= visfx_check_var,
	.fb_cursor	= visfx_cursor,
};

static struct fb_fix_screeninfo visfx_fix = {
	.type = FB_TYPE_PACKED_PIXELS,
	.visual = FB_VISUAL_PSEUDOCOLOR,
	.id = "Visualize FX",
};

static int visfx_probe(struct pci_dev *pdev,
		       const struct pci_device_id *ent)
{
	struct visfx_par *par;
	struct fb_info *info;
	int ret;

	info = framebuffer_alloc(sizeof(struct visfx_par), &pdev->dev);
	if (!info)
		return -ENOMEM;

	par = info->par;

	ret = pci_enable_device(pdev);
	if (ret)
		goto err_out_free;

	ret = pci_request_regions(pdev, KBUILD_MODNAME);
	if (ret)
		goto err_out_disable;

	par->reg_size = pci_resource_len(pdev, 0);
	par->reg_base = pci_ioremap_bar(pdev, 0);
	par->open_count = 0;

	if (!par->reg_base) {
		ret = -ENOMEM;
		goto err_out_release;
	}

	pci_set_drvdata(pdev, info);

	info->fbops = &visfx_ops;
	info->flags = FBINFO_DEFAULT | FBINFO_HWACCEL_FILLRECT | FBINFO_HWACCEL_IMAGEBLIT;
	info->fix = visfx_fix;
	info->pseudo_palette = par->pseudo_palette;
	info->fix.smem_start = pci_resource_start(pdev, 0) + VISFX_FB_OFFSET;
	info->fix.smem_len = VISFX_FB_LENGTH;
	info->screen_base = par->reg_base + VISFX_FB_OFFSET;
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_PSEUDOCOLOR;
	info->fix.line_length = 2048;
	info->fix.accel = FB_ACCEL_NONE;

	visfx_setup_unknown(info);
	visfx_get_video_mode(info);
	info->var.accel_flags = info->flags;

	ret = device_create_file(&pdev->dev, &dev_attr_reg);
	if (ret)
		goto err_out_iounmap;

	ret = fb_alloc_cmap(&info->cmap, NR_PALETTE, 0);
	if (ret)
		goto err_out_remove;

	ret = register_framebuffer(info);
	if (ret)
		goto err_out_dealloc_cmap;
	return 0;

err_out_dealloc_cmap:
	fb_dealloc_cmap(&info->cmap);
err_out_remove:
	device_remove_file(&pdev->dev, &dev_attr_reg);
err_out_iounmap:
	pci_iounmap(pdev, par->reg_base);
err_out_release:
	pci_release_regions(pdev);
err_out_disable:
	pci_disable_device(pdev);
err_out_free:
	framebuffer_release(info);
	return ret;
}

static void __exit visfx_remove(struct pci_dev *pdev)
{
	struct fb_info *info = pci_get_drvdata(pdev);
	struct visfx_par *par = info->par;

	device_remove_file(&pdev->dev, &dev_attr_reg);
	unregister_framebuffer(info);
	pci_iounmap(pdev, par->reg_base);
	framebuffer_release(info);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static const struct pci_device_id visfx_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_HP, 0x1008) },
	{ 0 },
};
MODULE_DEVICE_TABLE(pci, visfx_pci_tbl);

static struct pci_driver visfx_driver = {
	.name      = KBUILD_MODNAME,
	.id_table  = visfx_pci_tbl,
	.probe     = visfx_probe,
	.remove    = visfx_remove,
};

static int __init visfx_init(void)
{
	return pci_register_driver(&visfx_driver);
}
module_init(visfx_init);

static void __exit visfx_exit(void)
{
	pci_unregister_driver(&visfx_driver);
}
module_exit(visfx_exit);

MODULE_AUTHOR("Sven Schnelle <svens@stackframe.org>");
MODULE_DESCRIPTION("Framebuffer driver for HP Visualize FX cards");
MODULE_LICENSE("GPL");
