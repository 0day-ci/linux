// SPDX-License-Identifier: GPL-2.0+

/*
 * Copyright 2017-2020 NXP
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/firmware/imx/svc/misc.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <dt-bindings/firmware/imx/rsrc.h>

#include "dpu.h"
#include "dpu-dprc.h"
#include "dpu-prg.h"

#define SET					0x4
#define CLR					0x8
#define TOG					0xc

#define SYSTEM_CTRL0				0x00
#define  BCMD2AXI_MASTR_ID_CTRL			BIT(16)
#define  SW_SHADOW_LOAD_SEL			BIT(4)
#define  SHADOW_LOAD_EN				BIT(3)
#define  REPEAT_EN				BIT(2)
#define  SOFT_RESET				BIT(1)
#define  RUN_EN					BIT(0)	/* self-clearing */

#define IRQ_MASK				0x20
#define IRQ_MASK_STATUS				0x30
#define IRQ_NONMASK_STATUS			0x40
#define  DPR2RTR_FIFO_LOAD_BUF_RDY_UV_ERROR	BIT(7)
#define  DPR2RTR_FIFO_LOAD_BUF_RDY_YRGB_ERROR	BIT(6)
#define  DPR2RTR_UV_FIFO_OVFL			BIT(5)
#define  DPR2RTR_YRGB_FIFO_OVFL			BIT(4)
#define  IRQ_AXI_READ_ERROR			BIT(3)
#define  IRQ_DPR_SHADOW_LOADED_MASK		BIT(2)
#define  IRQ_DPR_RUN				BIT(1)
#define  IRQ_DPR_CRTL_DONE			BIT(0)
#define  IRQ_CTRL_MASK				0x7

#define MODE_CTRL0				0x50
#define  A_COMP_SEL(byte)			(((byte) & 0x3) << 16)
#define  R_COMP_SEL(byte)			(((byte) & 0x3) << 14)
#define  G_COMP_SEL(byte)			(((byte) & 0x3) << 12)
#define  B_COMP_SEL(byte)			(((byte) & 0x3) << 10)
#define  PIX_UV_SWAP				BIT(9)
#define  PIX_LUMA_UV_SWAP			BIT(8)
#define  PIX_SIZE_8BIT				(0 << 6)
#define  PIX_SIZE_16BIT				(1 << 6)
#define  PIX_SIZE_32BIT				(2 << 6)
#define  COMP_2PLANE_EN				BIT(5)
#define  YUV_EN					BIT(4)
#define  LINEAR_TILE				(0 << 2)
#define  GPU_STANDARD_TILE			(1 << 2)
#define  GPU_SUPER_TILE				(2 << 2)
#define  VPU_TILE				(3 << 2)
#define  LINE4					BIT(1)
#define  LINE8					0
#define  BUF3					BIT(0)
#define  BUF2					0

#define FRAME_CTRL0				0x70
#define  PITCH(n)				(((n) & 0xffff) << 16)
#define  ROT_FIRST				BIT(4)
#define  FLIP_FIRST				0
#define  ROT_ENC_MASK				0xc
#define  ROT_ENC_0				0x0
#define  ROT_ENC_90				0x4
#define  ROT_ENC_270				0xc
#define  DEGREE(n)				((((n) / 90) & 0x3) << 2)
#define  VFLIP_EN				BIT(1)
#define  HFLIP_EN				BIT(0)

#define FRAME_1P_CTRL0				0x90
#define FRAME_2P_CTRL0				0xe0
#define  BYTE_64				0x0
#define  BYTE_128				0x1
#define  BYTE_256				0x2
#define  BYTE_512				0x3
#define  BYTE_1K				0x4
#define  BYTE_2K				0x5
#define  BYTE_4K				0x6

#define FRAME_1P_PIX_X_CTRL			0xa0
#define  NUM_X_PIX_WIDE(n)			((n) & 0xffff)

#define FRAME_1P_PIX_Y_CTRL			0xb0
#define  NUM_Y_PIX_HIGH(n)			((n) & 0xffff)

#define FRAME_1P_BASE_ADDR_CTRL0		0xc0

#define FRAME_PIX_X_ULC_CTRL			0xf0
#define  CROP_ULC_X(n)				((n) & 0xffff)

#define FRAME_PIX_Y_ULC_CTRL			0x100
#define  CROP_ULC_Y(n)				((n) & 0xffff)

#define FRAME_2P_BASE_ADDR_CTRL0		0x110

#define STATUS_CTRL0				0x130
#define STATUS_CTRL1				0x140

#define RTRAM_CTRL0				0x200
#define  ABORT					BIT(7)
#define  STALL					0
#define  THRES_LOW(n)				(((n) & 0x7) << 4)
#define  THRES_HIGH(n)				(((n) & 0x7) << 1)
#define  ROWS_0_6				BIT(0)
#define  ROWS_0_4				0

#define DPU_DRPC_MAX_STRIDE			0x10000
#define DPU_DPRC_MAX_RTRAM_WIDTH		2880

struct dpu_dprc {
	struct device *dev;
	void __iomem *base;
	struct list_head list;
	struct clk *clk_apb;
	struct clk *clk_b;
	struct clk *clk_rtram;
	struct imx_sc_ipc *ipc_handle;
	spinlock_t spin_lock;
	u32 sc_resource;
	bool is_blit;

	/* The second one, if non-NULL, is auxiliary for UV buffer. */
	struct dpu_prg *prgs[2];
	bool has_aux_prg;
	bool use_aux_prg;
};

static DEFINE_MUTEX(dpu_dprc_list_mutex);
static LIST_HEAD(dpu_dprc_list);

static inline u32 dpu_dprc_read(struct dpu_dprc *dprc, unsigned int offset)
{
	return readl(dprc->base + offset);
}

static inline void
dpu_dprc_write(struct dpu_dprc *dprc, unsigned int offset, u32 value)
{
	writel(value, dprc->base + offset);
}

static inline void
dpu_dprc_set_stream_id(struct dpu_dprc *dprc, unsigned int stream_id)
{
	int ret;

	ret = imx_sc_misc_set_control(dprc->ipc_handle,
		dprc->sc_resource, IMX_SC_C_KACHUNK_SEL, stream_id);
	if (ret)
		dev_warn(dprc->dev, "failed to set KACHUNK_SEL: %d\n", ret);
}

static inline void
dpu_dprc_set_prg_sel(struct dpu_dprc *dprc, u32 resource, bool enable)
{
	int ret;

	ret = imx_sc_misc_set_control(dprc->ipc_handle,
				resource, IMX_SC_C_SEL0, enable);
	if (ret)
		dev_warn(dprc->dev, "failed to set SEL0: %d\n", ret);
}

static void dpu_dprc_reset(struct dpu_dprc *dprc)
{
	dpu_dprc_write(dprc, SYSTEM_CTRL0 + SET, SOFT_RESET);
	usleep_range(10, 20);
	dpu_dprc_write(dprc, SYSTEM_CTRL0 + CLR, SOFT_RESET);
}

static void dpu_dprc_enable(struct dpu_dprc *dprc)
{
	dpu_prg_enable(dprc->prgs[0]);
	if (dprc->use_aux_prg)
		dpu_prg_enable(dprc->prgs[1]);
}

static void dpu_dprc_reg_update(struct dpu_dprc *dprc)
{
	dpu_prg_reg_update(dprc->prgs[0]);
	if (dprc->use_aux_prg)
		dpu_prg_reg_update(dprc->prgs[1]);
}

static void dpu_dprc_enable_ctrl_done_irq(struct dpu_dprc *dprc)
{
	unsigned long flags;

	spin_lock_irqsave(&dprc->spin_lock, flags);
	dpu_dprc_write(dprc, IRQ_MASK + CLR, IRQ_DPR_CRTL_DONE);
	spin_unlock_irqrestore(&dprc->spin_lock, flags);
}

void dpu_dprc_configure(struct dpu_dprc *dprc, unsigned int stream_id,
			unsigned int width, unsigned int height,
			unsigned int x_offset, unsigned int y_offset,
			unsigned int stride,
			const struct drm_format_info *format, u64 modifier,
			dma_addr_t baddr, dma_addr_t uv_baddr,
			bool start, bool interlace_frame)
{
	struct device *dev = dprc->dev;
	unsigned int dprc_width = width + x_offset;
	unsigned int dprc_height;
	unsigned int p1_w, p1_h;
	unsigned int prg_stride = width * format->cpp[0];
	unsigned int bpp = 8 * format->cpp[0];
	unsigned int preq;
	unsigned int mt_w = 0, mt_h = 0;	/* micro-tile width/height */
	u32 val;

	dprc->use_aux_prg = false;

	if (start && !dprc->is_blit)
		dpu_dprc_set_stream_id(dprc, stream_id);

	if (interlace_frame) {
		height /= 2;
		y_offset /= 2;
	}

	dprc_height = height + y_offset;

	if (format->num_planes > 1) {
		p1_w = round_up(dprc_width, modifier ? 8 : 64);
		p1_h = round_up(dprc_height, 8);

		preq = modifier ? BYTE_64 : BYTE_1K;

		dpu_dprc_write(dprc, FRAME_2P_CTRL0, preq);
		if (dprc->sc_resource == IMX_SC_R_DC_0_BLIT1 ||
		    dprc->sc_resource == IMX_SC_R_DC_1_BLIT1) {
			dpu_dprc_set_prg_sel(dprc,
				dprc->sc_resource == IMX_SC_R_DC_0_BLIT1 ?
				IMX_SC_R_DC_0_BLIT0 : IMX_SC_R_DC_1_BLIT0,
				true);
			dpu_prg_set_auxiliary(dprc->prgs[1]);
			dprc->has_aux_prg = true;
		}
		dpu_dprc_write(dprc, FRAME_2P_BASE_ADDR_CTRL0, uv_baddr);
	} else {
		switch (dprc->sc_resource) {
		case IMX_SC_R_DC_0_BLIT0:
		case IMX_SC_R_DC_1_BLIT0:
			dpu_dprc_set_prg_sel(dprc, dprc->sc_resource, false);
			dpu_prg_set_primary(dprc->prgs[0]);
			break;
		case IMX_SC_R_DC_0_BLIT1:
		case IMX_SC_R_DC_1_BLIT1:
			dprc->has_aux_prg = false;
			break;
		default:
			break;
		}

		switch (modifier) {
		case DRM_FORMAT_MOD_VIVANTE_TILED:
			p1_w = round_up(dprc_width,
					format->cpp[0] == 2 ? 8 : 4);
			break;
		case DRM_FORMAT_MOD_VIVANTE_SUPER_TILED:
			if (dprc->is_blit)
				p1_w = round_up(dprc_width,
						format->cpp[0] == 2 ? 8 : 4);
			else
				p1_w = round_up(dprc_width, 64);
			break;
		default:
			p1_w = round_up(dprc_width,
					format->cpp[0] == 2 ? 32 : 16);
			break;
		}
		p1_h = round_up(dprc_height, 4);
	}

	dpu_dprc_write(dprc, FRAME_CTRL0, PITCH(stride));

	switch (modifier) {
	case DRM_FORMAT_MOD_VIVANTE_TILED:
		preq = BYTE_256;
		mt_w = bpp == 16 ? 8 : 4;
		mt_h = 4;
		break;
	case DRM_FORMAT_MOD_VIVANTE_SUPER_TILED:
		if (bpp == 16) {
			preq = BYTE_64;
			mt_w = 8;
		} else {
			preq = BYTE_128;
			mt_w = 4;
		}
		mt_h = 4;
		break;
	default:
		preq = BYTE_1K;
		break;
	}
	dpu_dprc_write(dprc, FRAME_1P_CTRL0, preq);
	dpu_dprc_write(dprc, FRAME_1P_PIX_X_CTRL, NUM_X_PIX_WIDE(p1_w));
	dpu_dprc_write(dprc, FRAME_1P_PIX_Y_CTRL, NUM_Y_PIX_HIGH(p1_h));
	dpu_dprc_write(dprc, FRAME_1P_BASE_ADDR_CTRL0, baddr);
	if (modifier) {
		dpu_dprc_write(dprc, FRAME_PIX_X_ULC_CTRL,
					CROP_ULC_X(round_down(x_offset, mt_w)));
		dpu_dprc_write(dprc, FRAME_PIX_Y_ULC_CTRL,
					CROP_ULC_Y(round_down(y_offset, mt_h)));
	} else {
		dpu_dprc_write(dprc, FRAME_PIX_X_ULC_CTRL, CROP_ULC_X(0));
		dpu_dprc_write(dprc, FRAME_PIX_Y_ULC_CTRL, CROP_ULC_Y(0));
	}

	dpu_dprc_write(dprc, RTRAM_CTRL0, THRES_LOW(3) | THRES_HIGH(7));

	switch (modifier) {
	case DRM_FORMAT_MOD_NONE:
		val = 0;
		break;
	case DRM_FORMAT_MOD_VIVANTE_TILED:
		val = GPU_STANDARD_TILE;
		break;
	case DRM_FORMAT_MOD_VIVANTE_SUPER_TILED:
		val = GPU_SUPER_TILE;
		break;
	default:
		dev_err(dev, "unsupported modifier 0x%016llx\n", modifier);
		return;
	}
	val |= format->num_planes > 1 ? LINE8 : LINE4;
	val |= BUF2;
	switch (format->format) {
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_RGBA8888:
	case DRM_FORMAT_RGBX8888:
	case DRM_FORMAT_BGRA8888:
	case DRM_FORMAT_BGRX8888:
		/*
		 * It turns out pixel components are mapped directly
		 * without position change via DPR processing with
		 * the following color component configurations.
		 * Leave the pixel format to be handled by the
		 * display controllers.
		 */
		val |= A_COMP_SEL(3) | R_COMP_SEL(2) |
		       G_COMP_SEL(1) | B_COMP_SEL(0);
		val |= PIX_SIZE_32BIT;
		break;
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_UYVY:
		val |= YUV_EN;
		fallthrough;
	case DRM_FORMAT_RGB565:
		val |= PIX_SIZE_16BIT;
		break;
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
		dprc->use_aux_prg = true;

		val |= COMP_2PLANE_EN;
		val |= YUV_EN;
		val |= PIX_SIZE_8BIT;
		break;
	default:
		dev_err(dev, "unsupported format 0x%08x\n", format->format);
		return;
	}
	dpu_dprc_write(dprc, MODE_CTRL0, val);

	if (dprc->is_blit) {
		val = SW_SHADOW_LOAD_SEL | RUN_EN | SHADOW_LOAD_EN;
		dpu_dprc_write(dprc, SYSTEM_CTRL0, val);
	} else if (start) {
		/* software shadow load for the first frame */
		val = SW_SHADOW_LOAD_SEL | SHADOW_LOAD_EN;
		dpu_dprc_write(dprc, SYSTEM_CTRL0, val);

		/* and then, run... */
		val |= RUN_EN | REPEAT_EN;
		dpu_dprc_write(dprc, SYSTEM_CTRL0, val);
	}

	dpu_prg_configure(dprc->prgs[0], width, height, x_offset, y_offset,
			  prg_stride, bpp, baddr, format, modifier, start);
	if (dprc->use_aux_prg)
		dpu_prg_configure(dprc->prgs[1], width, height,
				  x_offset, y_offset,
				  prg_stride, 8, uv_baddr, format, modifier,
				  start);

	dpu_dprc_enable(dprc);

	dpu_dprc_reg_update(dprc);

	if (!dprc->is_blit && start)
		dpu_dprc_enable_ctrl_done_irq(dprc);

	dev_dbg(dev, "w-%u, h-%u, s-%u, fmt-0x%08x, mod-0x%016llx\n",
			width, height, stride, format->format, modifier);
}

void dpu_dprc_disable_repeat_en(struct dpu_dprc *dprc)
{
	dpu_dprc_write(dprc, SYSTEM_CTRL0 + CLR, REPEAT_EN);

	dev_dbg(dprc->dev, "disable repeat_en\n");
}

static void dpu_dprc_ctrl_done_handle(struct dpu_dprc *dprc)
{
	if (dprc->is_blit)
		return;

	dpu_dprc_write(dprc, SYSTEM_CTRL0, REPEAT_EN);

	dpu_prg_shadow_enable(dprc->prgs[0]);
	if (dprc->use_aux_prg)
		dpu_prg_shadow_enable(dprc->prgs[1]);

	dev_dbg(dprc->dev, "ctrl done handle\n");
}

static irqreturn_t dpu_dprc_irq_handler(int irq, void *data)
{
	struct dpu_dprc *dprc = data;
	struct device *dev = dprc->dev;
	u32 mask, status;

	spin_lock(&dprc->spin_lock);

	/* cache valid irq status */
	mask = dpu_dprc_read(dprc, IRQ_MASK);
	mask = ~mask;
	status = dpu_dprc_read(dprc, IRQ_MASK_STATUS);
	status &= mask;

	/* mask the irqs being handled */
	dpu_dprc_write(dprc, IRQ_MASK + SET, status);

	/* clear status register */
	dpu_dprc_write(dprc, IRQ_MASK_STATUS, status);

	if (status & DPR2RTR_FIFO_LOAD_BUF_RDY_UV_ERROR)
		dev_err(dev, "DPR to RTRAM FIFO load UV buffer ready error\n");

	if (status & DPR2RTR_FIFO_LOAD_BUF_RDY_YRGB_ERROR)
		dev_err(dev, "DPR to RTRAM FIFO load YRGB buffer ready error\n");

	if (status & DPR2RTR_UV_FIFO_OVFL)
		dev_err(dev, "DPR to RTRAM FIFO UV FIFO overflow\n");

	if (status & DPR2RTR_YRGB_FIFO_OVFL)
		dev_err(dev, "DPR to RTRAM FIFO YRGB FIFO overflow\n");

	if (status & IRQ_AXI_READ_ERROR)
		dev_err(dev, "AXI read error\n");

	if (status & IRQ_DPR_CRTL_DONE)
		dpu_dprc_ctrl_done_handle(dprc);

	spin_unlock(&dprc->spin_lock);

	return IRQ_HANDLED;
}

bool dpu_dprc_rtram_width_supported(struct dpu_dprc *dprc, unsigned int width)
{
	return width <= DPU_DPRC_MAX_RTRAM_WIDTH;
}

bool dpu_dprc_stride_supported(struct dpu_dprc *dprc,
			       unsigned int stride, unsigned int uv_stride,
			       unsigned int width, unsigned int x_offset,
			       const struct drm_format_info *format,
			       u64 modifier,
			       dma_addr_t baddr, dma_addr_t uv_baddr)
{
	unsigned int prg_stride = width * format->cpp[0];
	unsigned int bpp = 8 * format->cpp[0];

	if (stride > DPU_DRPC_MAX_STRIDE)
		return false;

	if (format->num_planes > 1 && stride != uv_stride)
		return false;

	if (!dpu_prg_stride_supported(dprc->prgs[0], x_offset, bpp,
				      modifier, prg_stride, baddr))
		return false;

	if (format->num_planes > 1 &&
	    !dpu_prg_stride_supported(dprc->prgs[1], x_offset, bpp,
				      modifier, prg_stride, uv_baddr))
		return false;

	return true;
}

struct dpu_dprc *
dpu_dprc_lookup_by_of_node(struct device *dev, struct device_node *dprc_node)
{
	struct dpu_dprc *dprc;

	mutex_lock(&dpu_dprc_list_mutex);
	list_for_each_entry(dprc, &dpu_dprc_list, list) {
		if (dprc_node == dprc->dev->of_node) {
			mutex_unlock(&dpu_dprc_list_mutex);
			device_link_add(dev, dprc->dev,
					DL_FLAG_PM_RUNTIME |
					DL_FLAG_AUTOREMOVE_CONSUMER);
			return dprc;
		}
	}
	mutex_unlock(&dpu_dprc_list_mutex);

	return NULL;
}

static const struct of_device_id dpu_dprc_dt_ids[] = {
	{ .compatible = "fsl,imx8qm-dpr-channel", },
	{ .compatible = "fsl,imx8qxp-dpr-channel", },
	{ /* sentinel */ },
};

static int dpu_dprc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct resource *res;
	struct dpu_dprc *dprc;
	int ret, irq, i;

	dprc = devm_kzalloc(dev, sizeof(*dprc), GFP_KERNEL);
	if (!dprc)
		return -ENOMEM;

	ret = imx_scu_get_handle(&dprc->ipc_handle);
	if (ret) {
		dev_err_probe(dev, ret, "failed to get SCU ipc handle\n");
		return ret;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dprc->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(dprc->base))
		return PTR_ERR(dprc->base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -ENODEV;

	dprc->clk_apb = devm_clk_get(dev, "apb");
	if (IS_ERR(dprc->clk_apb)) {
		ret = PTR_ERR(dprc->clk_apb);
		dev_err_probe(dev, ret, "failed to get apb clock\n");
		return ret;
	}

	dprc->clk_b = devm_clk_get(dev, "b");
	if (IS_ERR(dprc->clk_b)) {
		ret = PTR_ERR(dprc->clk_b);
		dev_err_probe(dev, ret, "failed to get b clock\n");
		return ret;
	}

	dprc->clk_rtram = devm_clk_get(dev, "rtram");
	if (IS_ERR(dprc->clk_rtram)) {
		ret = PTR_ERR(dprc->clk_rtram);
		dev_err_probe(dev, ret, "failed to get rtram clock\n");
		return ret;
	}

	ret = of_property_read_u32(np, "fsl,sc-resource", &dprc->sc_resource);
	if (ret) {
		dev_err(dev, "cannot get SC resource %d\n", ret);
		return ret;
	}

	switch (dprc->sc_resource) {
	case IMX_SC_R_DC_0_BLIT1:
	case IMX_SC_R_DC_1_BLIT1:
		dprc->has_aux_prg = true;
		fallthrough;
	case IMX_SC_R_DC_0_BLIT0:
	case IMX_SC_R_DC_1_BLIT0:
		dprc->is_blit = true;
		fallthrough;
	case IMX_SC_R_DC_0_FRAC0:
	case IMX_SC_R_DC_1_FRAC0:
		break;
	case IMX_SC_R_DC_0_VIDEO0:
	case IMX_SC_R_DC_0_VIDEO1:
	case IMX_SC_R_DC_1_VIDEO0:
	case IMX_SC_R_DC_1_VIDEO1:
	case IMX_SC_R_DC_0_WARP:
	case IMX_SC_R_DC_1_WARP:
		dprc->has_aux_prg = true;
		break;
	default:
		dev_err(dev, "wrong SC resource %u\n", dprc->sc_resource);
		return -EINVAL;
	}

	for (i = 0; i < 2; i++) {
		if (i == 1 && !dprc->has_aux_prg)
			break;

		dprc->prgs[i] = dpu_prg_lookup_by_phandle(dev, "fsl,prgs", i);
		if (!dprc->prgs[i])
			return -EPROBE_DEFER;

		if (i == 1)
			dpu_prg_set_auxiliary(dprc->prgs[i]);
	}

	dprc->dev = dev;
	spin_lock_init(&dprc->spin_lock);

	ret = devm_request_irq(dev, irq, dpu_dprc_irq_handler, IRQF_SHARED,
							dev_name(dev), dprc);
	if (ret < 0) {
		dev_err(dev, "failed to request irq(%u): %d\n", irq, ret);
		return ret;
	}

	platform_set_drvdata(pdev, dprc);

	pm_runtime_enable(dev);

	mutex_lock(&dpu_dprc_list_mutex);
	list_add(&dprc->list, &dpu_dprc_list);
	mutex_unlock(&dpu_dprc_list_mutex);

	return 0;
}

static int dpu_dprc_remove(struct platform_device *pdev)
{
	struct dpu_dprc *dprc = platform_get_drvdata(pdev);

	mutex_lock(&dpu_dprc_list_mutex);
	list_del(&dprc->list);
	mutex_unlock(&dpu_dprc_list_mutex);

	pm_runtime_disable(&pdev->dev);

	return 0;
}

static int __maybe_unused dpu_dprc_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dpu_dprc *dprc = platform_get_drvdata(pdev);

	clk_disable_unprepare(dprc->clk_rtram);
	clk_disable_unprepare(dprc->clk_b);
	clk_disable_unprepare(dprc->clk_apb);

	return 0;
}

static int __maybe_unused dpu_dprc_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dpu_dprc *dprc = platform_get_drvdata(pdev);
	int ret;

	ret = clk_prepare_enable(dprc->clk_apb);
	if (ret) {
		dev_err(dev, "failed to enable apb clock: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(dprc->clk_b);
	if (ret) {
		dev_err(dev, "failed to enable b clock: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(dprc->clk_rtram);
	if (ret) {
		dev_err(dev, "failed to enable rtram clock: %d\n", ret);
		return ret;
	}

	dpu_dprc_reset(dprc);

	/* disable all control irqs and enable all error irqs */
	spin_lock(&dprc->spin_lock);
	dpu_dprc_write(dprc, IRQ_MASK, IRQ_CTRL_MASK);
	spin_unlock(&dprc->spin_lock);

	return ret;
}

static const struct dev_pm_ops dpu_dprc_pm_ops = {
	SET_RUNTIME_PM_OPS(dpu_dprc_runtime_suspend,
			   dpu_dprc_runtime_resume, NULL)
};

struct platform_driver dpu_dprc_driver = {
	.probe = dpu_dprc_probe,
	.remove = dpu_dprc_remove,
	.driver = {
		.pm = &dpu_dprc_pm_ops,
		.name = "dpu-dpr-channel",
		.of_match_table = dpu_dprc_dt_ids,
	},
};
