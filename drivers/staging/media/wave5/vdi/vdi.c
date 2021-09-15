// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Wave5 series multi-standard codec IP - low level access functions
 *
 * Copyright (C) 2021 CHIPS&MEDIA INC
 */
#include <linux/bug.h>
#include "vdi.h"
#include "../v4l2/vpu.h"
#include "../vpuapi/wave/wave5_regdefine.h"
#include <linux/delay.h>
#define VDI_SRAM_BASE_ADDR                  0x00

#define VDI_SYSTEM_ENDIAN                VDI_LITTLE_ENDIAN
#define VDI_128BIT_BUS_SYSTEM_ENDIAN     VDI_128BIT_LITTLE_ENDIAN

static int vdi_allocate_common_memory(struct device *dev)
{
	int ret;
	struct vpu_device *vpu_dev = dev_get_drvdata(dev);

	if (!vpu_dev->common_mem.vaddr) {
		vpu_dev->common_mem.size = SIZE_COMMON;
		ret = vdi_allocate_dma_memory(vpu_dev, &vpu_dev->common_mem);
		if (ret) {
			dev_err(dev, "unable to allocate common buffer\n");
			return ret;
		}
	}

	dev_dbg(dev, "common_mem: daddr=%pad size=%zu vaddr=0x%p\n",
		&vpu_dev->common_mem.daddr, vpu_dev->common_mem.size,
			vpu_dev->common_mem.vaddr);

	return 0;
}

int vdi_init(struct device *dev)
{
	struct vpu_platform_data *pdata = dev_get_platdata(dev);
	struct vpu_device *vpu_dev = dev_get_drvdata(dev);
	int i;

	if (vdi_allocate_common_memory(dev) < 0) {
		pr_err("[VDI] fail to get vpu common buffer from driver\n");
		goto ERR_VDI_INIT;
	}

	if (pdata && pdata->reset)
		pdata->reset(dev, vpu_dev->vdb_register.vaddr);

	if (pdata && pdata->pre_fw_init) {
		if (pdata->pre_fw_init(dev, vpu_dev->vdb_register.vaddr))
			goto ERR_VDI_INIT;
	}

	if (PRODUCT_CODE_W_SERIES(vpu_dev->product_code)) {
		// if BIT processor is not running.
		if (vdi_read_register(vpu_dev, W5_VCPU_CUR_PC) == 0) {
			for (i = 0; i < 64; i++)
				vdi_write_register(vpu_dev, (i * 4) + 0x100, 0x0);
		}
	} else {
		WARN_ONCE(1, "unsupported product code 0x%x\n", vpu_dev->product_code);
	}

	pr_info("[VDI] success to init driver\n");

	return 0;

ERR_VDI_INIT:

	vdi_release(dev);
	return -1;
}
EXPORT_SYMBOL(vdi_init);

int vdi_release(struct device *dev)
{
	struct vpu_device *vpu_dev = dev_get_drvdata(dev);

	memset(&vpu_dev->vdb_register, 0x00, sizeof(vpu_dev->vdb_register));
	vdi_free_dma_memory(vpu_dev, &vpu_dev->common_mem);

	return 0;
}
EXPORT_SYMBOL(vdi_release);

void vdi_write_register(struct vpu_device *vpu_dev, unsigned int addr, unsigned int data)
{
	struct vpu_platform_data *pdata;

	pdata = dev_get_platdata(vpu_dev->dev);
	if (pdata && pdata->write_register)
		return pdata->write_register(vpu_dev->dev, vpu_dev->vdb_register.vaddr, addr, data);

	writel(data, vpu_dev->vdb_register.vaddr + addr);
}

unsigned int vdi_read_register(struct vpu_device *vpu_dev, unsigned int addr)
{
	struct vpu_platform_data *pdata;

	pdata = dev_get_platdata(vpu_dev->dev);
	if (pdata && pdata->read_register)
		return pdata->read_register(vpu_dev->dev, vpu_dev->vdb_register.vaddr, addr);

	return readl(vpu_dev->vdb_register.vaddr + addr);
}
EXPORT_SYMBOL(vdi_read_register);

int vdi_buffer_sync(struct device *dev, struct vpu_buf *vb, int dir)
{
	struct vpu_device *vpu_dev = dev_get_drvdata(dev);
	struct vpu_platform_data *pdata;

	if (!vb || !vb->vaddr) {
		pr_err("%s(): unable to clear unmapped buffer\n", __func__);
		return -EINVAL;
	}

	pdata = dev_get_platdata(dev);
	if (pdata && pdata->buffer_sync)
		return pdata->buffer_sync(dev, vpu_dev->vdb_register.vaddr, vb, 0, vb->size, dir);

	return 0;
}
EXPORT_SYMBOL(vdi_buffer_sync);

int vdi_clear_memory(struct vpu_device *vpu_dev, struct vpu_buf *vb)
{
	struct vpu_platform_data *pdata;

	if (!vb || !vb->vaddr) {
		pr_err("%s(): unable to clear unmapped buffer\n", __func__);
		return -EINVAL;
	}

	memset(vb->vaddr, 0, vb->size);
	pdata = dev_get_platdata(vpu_dev->dev);
	if (pdata && pdata->buffer_sync)
		pdata->buffer_sync(vpu_dev->dev, vpu_dev->vdb_register.vaddr, vb, 0, vb->size, VPU_BUF_SYNC_TO_DEVICE);

	return vb->size;
}

static int swap_endian(struct vpu_device *vpu_dev, unsigned char *data, int len, int endian);

int vdi_write_memory(struct vpu_device *vpu_dev, struct vpu_buf *vb, size_t offset, unsigned char *data, int len, int endian)
{
	struct vpu_platform_data *pdata;

	if (!vb || !vb->vaddr) {
		pr_err("%s(): unable to write to unmapped buffer\n", __func__);
		return -EINVAL;
	}

	if ((offset + len) > vb->size) {
		pr_err("%s(): buffer too small\n", __func__);
		return -ENOSPC;
	}

	swap_endian(vpu_dev, data, len, endian);
	memcpy(vb->vaddr + offset, data, len);
	pdata = dev_get_platdata(vpu_dev->dev);
	if (pdata && pdata->buffer_sync)
		pdata->buffer_sync(vpu_dev->dev, vpu_dev->vdb_register.vaddr, vb, offset, len, VPU_BUF_SYNC_TO_DEVICE);

	return len;
}

int vdi_allocate_dma_memory(struct vpu_device *vpu_dev, struct vpu_buf *vb)
{
	void *vaddr;
	dma_addr_t daddr;
	struct vpu_platform_data *pdata;

	if (!vb->size) {
		dev_err(vpu_dev->dev, "%s(): requested size==0\n", __func__);
		return -EINVAL;
	}

	pdata = dev_get_platdata(vpu_dev->dev);
	if (pdata && pdata->buffer_alloc)
		return pdata->buffer_alloc(vpu_dev->dev, vb);

	vaddr = dma_alloc_coherent(vpu_dev->dev, vb->size, &daddr, GFP_KERNEL);
	if (!vaddr)
		return -ENOMEM;
	vb->vaddr = vaddr;
	vb->daddr = daddr;

	return 0;
}
EXPORT_SYMBOL(vdi_allocate_dma_memory);

void vdi_free_dma_memory(struct vpu_device *vpu_dev, struct vpu_buf *vb)
{
	struct vpu_platform_data *pdata;

	if (vb->size == 0)
		return;

	pdata = dev_get_platdata(vpu_dev->dev);
	if (pdata && pdata->buffer_free)
		return pdata->buffer_free(vpu_dev->dev, vb);

	if (!vb->vaddr)
		dev_err(vpu_dev->dev, "%s(): requested free of unmapped buffer\n", __func__);
	else
		dma_free_coherent(vpu_dev->dev, vb->size, vb->vaddr, vb->daddr);

	memset(vb, 0, sizeof(*vb));
}
EXPORT_SYMBOL(vdi_free_dma_memory);

int vdi_convert_endian(struct vpu_device *vpu_dev, unsigned int endian)
{
	if (PRODUCT_CODE_W_SERIES(vpu_dev->product_code)) {
		switch (endian) {
		case VDI_LITTLE_ENDIAN:
			endian = 0x00;
			break;
		case VDI_BIG_ENDIAN:
			endian = 0x0f;
			break;
		case VDI_32BIT_LITTLE_ENDIAN:
			endian = 0x04;
			break;
		case VDI_32BIT_BIG_ENDIAN:
			endian = 0x03;
			break;
		}
	}

	return (endian & 0x0f);
}

void byte_swap(unsigned char *data, int len)
{
	u8 temp;
	s32 i;

	for (i = 0; i < len; i += 2) {
		temp      = data[i];
		data[i]   = data[i + 1];
		data[i + 1] = temp;
	}
}

void word_swap(unsigned char *data, int len)
{
	u16  temp;
	u16 *ptr = (uint16_t *)data;
	s32   i, size = len / (int)sizeof(uint16_t);

	for (i = 0; i < size; i += 2) {
		temp     = ptr[i];
		ptr[i]   = ptr[i + 1];
		ptr[i + 1] = temp;
	}
}

void dword_swap(unsigned char *data, int len)
{
	u32  temp;
	u32 *ptr = (uint32_t *)data;
	s32   i, size = len / (int)sizeof(uint32_t);

	for (i = 0; i < size; i += 2) {
		temp     = ptr[i];
		ptr[i]   = ptr[i + 1];
		ptr[i + 1] = temp;
	}
}

void lword_swap(unsigned char *data, int len)
{
	u64  temp;
	u64 *ptr = (uint64_t *)data;
	s32   i, size = len / (int)sizeof(uint64_t);

	for (i = 0; i < size; i += 2) {
		temp     = ptr[i];
		ptr[i]   = ptr[i + 1];
		ptr[i + 1] = temp;
	}
}

static int swap_endian(struct vpu_device *vpu_dev, unsigned char *data, int len, int endian)
{
	int         changes;
	int         sys_endian;
	bool        byte_change, word_change, dword_change, lword_change;

	if (PRODUCT_CODE_W_SERIES(vpu_dev->product_code)) {
		sys_endian = VDI_128BIT_BUS_SYSTEM_ENDIAN;
	} else {
		pr_err("unknown product id : %08x\n", vpu_dev->product_code);
		return -1;
	}

	endian     = vdi_convert_endian(vpu_dev, endian);
	sys_endian = vdi_convert_endian(vpu_dev, sys_endian);
	if (endian == sys_endian)
		return 0;

	changes     = endian ^ sys_endian;
	byte_change  = changes & 0x01;
	word_change  = ((changes & 0x02) == 0x02);
	dword_change = ((changes & 0x04) == 0x04);
	lword_change = ((changes & 0x08) == 0x08);

	if (byte_change)
		byte_swap(data, len);
	if (word_change)
		word_swap(data, len);
	if (dword_change)
		dword_swap(data, len);
	if (lword_change)
		lword_swap(data, len);

	return 1;
}

