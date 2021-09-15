// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Wave5 series multi-standard codec IP - platform driver
 *
 * Copyright (C) 2021 CHIPS&MEDIA INC
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/firmware.h>
#include <linux/interrupt.h>
#include "vpu.h"
#include "vpu_dec.h"
#include "vpu_enc.h"
#include "../vpuapi/wave/wave5_regdefine.h"
#include "../vpuapi/vpuconfig.h"
#include "../vpuapi/wave/wave5.h"

#define VPU_PLATFORM_DEVICE_NAME    "vdec"
#define VPU_CLK_NAME                "vcodec"

/* if the platform driver knows this driver */
/* the definition of VPU_REG_BASE_ADDR and VPU_REG_SIZE are not meaningful */
#define VPU_REG_BASE_ADDR    0x75000000
#define VPU_REG_SIZE         0x4000

struct wave5_match_data {
	bool decoder;
	bool encoder;
	const char *fw_name;
};

const struct wave5_match_data default_match_data = {
	.decoder = true,
	.encoder = true,
	.fw_name = "chagall.bin",
};

unsigned int vpu_debug = 1;
module_param(vpu_debug, uint, 0644);

int vpu_wait_interrupt(struct vpu_instance *inst, unsigned int timeout)
{
	int ret;

	ret = wait_for_completion_timeout(&inst->dev->irq_done,
					  msecs_to_jiffies(timeout));
	if (!ret)
		return -ETIMEDOUT;

	reinit_completion(&inst->dev->irq_done);

	return 0;
}

static irqreturn_t vpu_irq(int irq, void *dev_id)
{
	struct vpu_device   *dev = (struct vpu_device *)dev_id;
	unsigned int irq_status;

	if (vdi_read_register(dev, W5_VPU_VPU_INT_STS)) {
		irq_status = vdi_read_register(dev, W5_VPU_VINT_REASON);
		vdi_write_register(dev, W5_VPU_VINT_REASON_CLR, irq_status);
		vdi_write_register(dev, W5_VPU_VINT_CLEAR, 0x1);

		kfifo_in(&dev->irq_status, &irq_status, sizeof(int));

		return IRQ_WAKE_THREAD;
	}

	return IRQ_HANDLED;
}

static irqreturn_t vpu_irq_thread(int irq, void *dev_id)
{
	struct vpu_device   *dev = (struct vpu_device *)dev_id;
	struct vpu_instance *inst;
	unsigned int irq_status, val;
	int ret;

	while (kfifo_len(&dev->irq_status)) {
		inst = v4l2_m2m_get_curr_priv(dev->v4l2_m2m_dev);
		if (inst) {
			inst->ops->finish_process(inst);
		} else {
			ret = kfifo_out(&dev->irq_status, &irq_status, sizeof(int));
			if (!ret)
				break;
			DPRINTK(dev, 1, "irq_status: 0x%x\n", irq_status);
			val = vdi_read_register(dev, W5_VPU_VINT_REASON_USR);
			val &= ~irq_status;
			vdi_write_register(dev, W5_VPU_VINT_REASON_USR, val);
			complete(&dev->irq_done);
		}
	}

	return IRQ_HANDLED;
}

static void vpu_device_run(void *priv)
{
	struct vpu_instance *inst = priv;

	DPRINTK(inst->dev, 1, "inst type=%d state=%d\n",
		inst->type, inst->state);

	inst->ops->start_process(inst);
}

static int vpu_job_ready(void *priv)
{
	struct vpu_instance *inst = priv;

	DPRINTK(inst->dev, 1, "inst type=%d state=%d\n",
		inst->type, inst->state);

	if (inst->state == VPU_INST_STATE_STOP)
		return 0;

	return 1;
}

static void vpu_job_abort(void *priv)
{
	struct vpu_instance *inst = priv;

	DPRINTK(inst->dev, 1, "inst type=%d state=%d\n",
		inst->type, inst->state);

	inst->ops->stop_process(inst);
}

static const struct v4l2_m2m_ops vpu_m2m_ops = {
	.device_run = vpu_device_run,
	.job_ready  = vpu_job_ready,
	.job_abort  = vpu_job_abort,
};

static int vpu_load_firmware(struct device *dev, const char *fw_name)
{
	const struct firmware *fw;
	enum ret_code ret = RETCODE_SUCCESS;
	u32 version;
	u32 revision;
	u32 product_id;

	if (request_firmware(&fw, fw_name, dev)) {
		pr_err("request_firmware fail\n");
		return -1;
	}

	ret = vpu_init_with_bitcode(dev, (unsigned short *)fw->data, fw->size / 2);
	if (ret != RETCODE_SUCCESS) {
		pr_err("vpu_init_with_bitcode fail\n");
		return -1;
	}
	release_firmware(fw);

	ret = vpu_get_version_info(dev, &version, &revision, &product_id);
	if (ret != RETCODE_SUCCESS) {
		pr_err("vpu_get_version_info fail\n");
		return -1;
	}

	pr_err("enum product_id : %08x\n", product_id);
	pr_err("fw_version : %08x(r%d)\n", version, revision);

	return 0;
}

static int vpu_probe(struct platform_device *pdev)
{
	int err = 0;
	struct vpu_device *dev;
	struct resource *res = NULL;
	const struct wave5_match_data *match_data;

	match_data = device_get_match_data(&pdev->dev);
	if (!match_data)
		match_data = &default_match_data;

	/* physical addresses limited to 32 bits */
	dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));
	dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "unable to get mem resource\n");
		return -EINVAL;
	}
	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->vdb_register.daddr = res->start;
	dev->vdb_register.size  = resource_size(res);
	dev->vdb_register.vaddr = devm_ioremap(&pdev->dev, dev->vdb_register.daddr, dev->vdb_register.size);
	ida_init(&dev->inst_ida);

	dev_dbg(&pdev->dev, "REGISTER BASE daddr=%pad vaddr=%p size=%zu\n",
		&dev->vdb_register.daddr, dev->vdb_register.vaddr, dev->vdb_register.size);

	mutex_init(&dev->dev_lock);
	mutex_init(&dev->hw_lock);
	init_completion(&dev->irq_done);
	dev_set_drvdata(&pdev->dev, dev);
	dev->dev = &pdev->dev;
	dev->product_code = vdi_read_register(dev, VPU_PRODUCT_CODE_REGISTER);
	err = vdi_init(&pdev->dev);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to init vdi: %d\n", err);
		return err;
	}
	dev->product = wave_vpu_get_product_id(dev);

	err = v4l2_device_register(&pdev->dev, &dev->v4l2_dev);
	if (err) {
		dev_err(&pdev->dev, "v4l2_device_register fail: %d\n", err);
		goto err_v4l2_dev_reg;
	}

	dev->v4l2_m2m_dev = v4l2_m2m_init(&vpu_m2m_ops);
	if (IS_ERR(dev->v4l2_m2m_dev)) {
		dev_err(&pdev->dev, "v4l2_m2m_init fail: %ld\n", PTR_ERR(dev->v4l2_m2m_dev));
		err = PTR_ERR(dev->v4l2_m2m_dev);
		goto err_m2m_init;
	}

	if (match_data->decoder) {
		err = vpu_dec_register_device(dev);
		if (err) {
			dev_err(&pdev->dev, "vpu_dec_register_device fail: %d\n", err);
			goto err_dec_reg;
		}
	}
	if (match_data->encoder) {
		err = vpu_enc_register_device(dev);
		if (err) {
			dev_err(&pdev->dev, "vpu_enc_register_device fail: %d\n", err);
			goto err_enc_reg;
		}
	}

	dev->clk = devm_clk_get(&pdev->dev, VPU_CLK_NAME);
	if (IS_ERR(dev->clk)) {
		dev_warn(&pdev->dev, "unable to get clock: %ld\n", PTR_ERR(dev->clk));
		/* continue wihtout clock, assume externally managed */
		dev->clk = NULL;
	}

	err = clk_prepare_enable(dev->clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable clock: %d\n", err);
		goto err_clk_prep_en;
	}

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(&pdev->dev, "failed to get irq resource\n");
		err = -ENXIO;
		goto err_get_irq;
	}
	dev->irq = res->start;

	if (kfifo_alloc(&dev->irq_status, 16 * sizeof(int), GFP_KERNEL)) {
		dev_err(&pdev->dev, "failed to allocate fifo\n");
		goto err_kfifo_alloc;
	}

	err = devm_request_threaded_irq(&pdev->dev, dev->irq, vpu_irq, vpu_irq_thread, 0, "vpu_irq", dev);
	if (err) {
		dev_err(&pdev->dev, "fail to register interrupt handler: %d\n", err);
		goto err_request_irq;
	}

	err = vpu_load_firmware(&pdev->dev, match_data->fw_name);
	if (err) {
		dev_err(&pdev->dev, "failed to vpu_load_firmware: %d\n", err);
		goto err_load_fw;
	}

	return 0;

err_load_fw:
err_request_irq:
	kfifo_free(&dev->irq_status);
err_kfifo_alloc:
err_get_irq:
	clk_disable_unprepare(dev->clk);
err_clk_prep_en:
	if (match_data->encoder)
		vpu_enc_unregister_device(dev);
err_enc_reg:
	if (match_data->decoder)
		vpu_dec_unregister_device(dev);
err_dec_reg:
	v4l2_m2m_release(dev->v4l2_m2m_dev);
err_m2m_init:
	v4l2_device_unregister(&dev->v4l2_dev);
err_v4l2_dev_reg:
	vdi_release(&pdev->dev);

	return err;
}

static int vpu_remove(struct platform_device *pdev)
{
	struct vpu_device *dev = dev_get_drvdata(&pdev->dev);

	clk_disable_unprepare(dev->clk);
	vpu_enc_unregister_device(dev);
	vpu_dec_unregister_device(dev);
	v4l2_m2m_release(dev->v4l2_m2m_dev);
	v4l2_device_unregister(&dev->v4l2_dev);
	kfifo_free(&dev->irq_status);
	vdi_release(&pdev->dev);

	return 0;
}

#ifdef CONFIG_OF
const struct wave5_match_data wave511_data = {
	.decoder = true,
	.encoder = false,
	.fw_name = "wave511_dec_fw.bin",
};

const struct wave5_match_data wave521_data = {
	.decoder = false,
	.encoder = true,
	.fw_name = "wave521_enc_fw.bin",
};

const struct wave5_match_data wave521c_data = {
	.decoder = true,
	.encoder = true,
	.fw_name = "wave521c_codec_fw.bin",
};

// TODO: move this file to wave5 layer and convert runtime paramer filling to
// predefined structure used as data here.
static const struct of_device_id wave5_dt_ids[] = {
	{ .compatible = "cnm,cm511-vpu", .data = &wave511_data },
	{ .compatible = "cnm,cm517-vpu", .data = &default_match_data },
	{ .compatible = "cnm,cm521-vpu", .data = &wave521_data },
	{ .compatible = "cnm,cm521c-vpu", .data = &wave521c_data },
	{ .compatible = "cnm,cm521c-dual-vpu", .data = &wave521c_data },
	{ .compatible = "cnm,cm521e1-vpu", .data = &default_match_data },
	{ .compatible = "cnm,cm537-vpu", .data = &default_match_data },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, wave5_dt_ids);
#endif

static struct platform_driver vpu_driver = {
	.driver = {
		.name = VPU_PLATFORM_DEVICE_NAME,
		.of_match_table = of_match_ptr(wave5_dt_ids),
		},
	.probe = vpu_probe,
	.remove = vpu_remove,
	//.suspend = vpu_suspend,
	//.resume = vpu_resume,
};

static int __init vpu_init(void)
{
	int ret;

	ret = platform_driver_register(&vpu_driver);

	return ret;
}

static void __exit vpu_exit(void)
{
	platform_driver_unregister(&vpu_driver);
}

MODULE_DESCRIPTION("chips&media VPU V4L2 driver");
MODULE_LICENSE("GPL");

module_init(vpu_init);
module_exit(vpu_exit);

