/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __LOONGSON_DRV_H__
#define __LOONGSON_DRV_H__

#include <drm/drm_drv.h>
#include <drm/drm_gem.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_vram_helper.h>
#include <drm/drm_plane.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_encoder.h>
#include "loongson_i2c.h"

/* General customization:
 */
#define DRIVER_AUTHOR "Loongson graphics driver team"
#define DRIVER_NAME "loongson-drm"
#define DRIVER_DESC "Loongson LS7A DRM driver"
#define DRIVER_DATE "20200915"

#define to_loongson_crtc(x) container_of(x, struct loongson_crtc, base)
#define to_loongson_encoder(x) container_of(x, struct loongson_encoder, base)
#define to_loongson_connector(x) container_of(x, struct loongson_connector, base)

#define LS7A_CHIPCFG_REG_BASE (0x10010000)
#define PCI_DEVICE_ID_LOONGSON_DC 0x7a06
#define PCI_DEVICE_ID_LOONGSON_GPU 0x7a15
#define LS7A_PIX_PLL (0x04b0)
#define REG_OFFSET (0x10)
#define FB_CFG_REG (0x1240)
#define FB_ADDR0_REG (0x1260)
#define FB_ADDR1_REG (0x1580)
#define FB_STRI_REG (0x1280)
#define FB_DITCFG_REG (0x1360)
#define FB_DITTAB_LO_REG (0x1380)
#define FB_DITTAB_HI_REG (0x13a0)
#define FB_PANCFG_REG (0x13c0)
#define FB_PANTIM_REG (0x13e0)
#define FB_HDISPLAY_REG (0x1400)
#define FB_HSYNC_REG (0x1420)
#define FB_VDISPLAY_REG (0x1480)
#define FB_VSYNC_REG (0x14a0)

#define CFG_FMT GENMASK(2, 0)
#define CFG_FBSWITCH BIT(7)
#define CFG_ENABLE BIT(8)
#define CFG_FBNUM BIT(11)
#define CFG_GAMMAR BIT(12)
#define CFG_RESET BIT(20)

#define FB_PANCFG_DEF 0x80001311
#define FB_HSYNC_PULSE (1 << 30)
#define FB_VSYNC_PULSE (1 << 30)

/* PIX PLL */
#define LOOPC_MIN 24
#define LOOPC_MAX 161
#define FRE_REF_MIN 12
#define FRE_REF_MAX 32
#define DIV_REF_MIN 3
#define DIV_REF_MAX 5
#define PST_DIV_MAX 64

struct pix_pll {
	u32 l2_div;
	u32 l1_loopc;
	u32 l1_frefc;
};

struct loongson_crtc {
	struct drm_crtc base;
	struct loongson_device *ldev;
	u32 crtc_id;
	u32 reg_offset;
	u32 cfg_reg;
	struct drm_plane *plane;
};

struct loongson_encoder {
	struct drm_encoder base;
	struct loongson_device *ldev;
	struct loongson_crtc *lcrtc;
};

struct loongson_connector {
	struct drm_connector base;
	struct loongson_device *ldev;
	struct loongson_i2c *i2c;
	u16 id;
	u32 type;
	u16 i2c_id;
};

struct loongson_mode_info {
	struct loongson_device *ldev;
	struct loongson_crtc *crtc;
	struct loongson_encoder *encoder;
	struct loongson_connector *connector;
};

struct loongson_device {
	struct drm_device *dev;
	struct drm_atomic_state *state;

	void __iomem *mmio;
	void __iomem *io;
	u32 vram_start;
	u32 vram_size;

	u32 num_crtc;
	struct loongson_mode_info mode_info[2];
	struct pci_dev *gpu_pdev; /* LS7A gpu device info */

	struct loongson_i2c i2c_bus[LS_MAX_I2C_BUS];
	struct gpio_chip chip;
};

/* crtc */
int loongson_crtc_init(struct loongson_device *ldev, int index);

/* connector */
int loongson_connector_init(struct loongson_device *ldev, int index);

/* encoder */
int loongson_encoder_init(struct loongson_device *ldev, int index);

/* plane */
int loongson_plane_init(struct loongson_crtc *lcrtc);

/* i2c */
int loongson_dc_gpio_init(struct loongson_device *ldev);

/* device */
u32 loongson_gpu_offset(struct drm_plane_state *state);
u32 ls7a_mm_rreg(struct loongson_device *ldev, u32 offset);
void ls7a_mm_wreg(struct loongson_device *ldev, u32 offset, u32 val);
u32 ls7a_io_rreg(struct loongson_device *ldev, u32 offset);
void ls7a_io_wreg(struct loongson_device *ldev, u32 offset, u32 val);

#endif /* __LOONGSON_DRV_H__ */
