// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Wave5 series multi-standard codec IP - helper functions
 *
 * Copyright (C) 2021 CHIPS&MEDIA INC
 */

#include <linux/bug.h>
#include "vpuapi.h"
#include "wave/wave5_regdefine.h"
#include "wave/wave5.h"

#define DECODE_ALL_TEMPORAL_LAYERS  -1
#define DECODE_ALL_SPATIAL_LAYERS   -1

void vpu_clear_interrupt_ex(struct vpu_instance *inst, int32_t intr_flag)
{
	wave5_vpu_clear_interrupt(inst, intr_flag);
}

static enum ret_code initialize_vpu(struct device *dev, const uint16_t *code, uint32_t size)
{
	enum ret_code     ret;
	struct vpu_device *vpu_dev = (struct vpu_device *)dev_get_drvdata(dev);

	if (mutex_lock_interruptible(&vpu_dev->hw_lock))
		return RETCODE_FAILURE;

	if (wave5_vpu_is_init(vpu_dev) != 0) {
		wave5_vpu_re_init(dev, (void *)code, size);
		ret = RETCODE_CALLED_BEFORE;
		goto err_out;
	}

	ret = wave5_vpu_reset(dev, SW_RESET_ON_BOOT);
	if (ret != RETCODE_SUCCESS)
		goto err_out;

	ret = wave5_vpu_init(dev, (void *)code, size);

err_out:
	mutex_unlock(&vpu_dev->hw_lock);
	return ret;
}

enum ret_code vpu_init_with_bitcode(struct device *dev, const uint16_t *code, uint32_t size)
{
	if (!code || size == 0)
		return RETCODE_INVALID_PARAM;

	return initialize_vpu(dev, code, size);
}

enum ret_code vpu_get_version_info(struct device *dev, uint32_t *version_info,
				   u32 *revision, uint32_t *product_id)
{
	enum ret_code  ret;
	struct vpu_device *vpu_dev = (struct vpu_device *)dev_get_drvdata(dev);

	if (mutex_lock_interruptible(&vpu_dev->hw_lock))
		return RETCODE_FAILURE;

	if (wave5_vpu_is_init(vpu_dev) == 0) {
		ret = RETCODE_NOT_INITIALIZED;
		goto err_out;
	}

	if (product_id)
		*product_id = vpu_dev->product;
	ret = wave5_vpu_get_version(vpu_dev, version_info, revision);

err_out:
	mutex_unlock(&vpu_dev->hw_lock);
	return ret;
}

static int check_dec_open_param(struct vpu_device *dev, struct dec_open_param *param)
{
	struct vpu_attr *p_attr;

	if (param == 0)
		return -EINVAL;

	p_attr     = &dev->attr;

	if (param->bitstream_buffer % 8)
		return -EINVAL;

	if (param->bitstream_mode == BS_MODE_INTERRUPT) {
		if (param->bitstream_buffer_size % 1024 || param->bitstream_buffer_size < 1024)
			return -EINVAL;
	}

	// check bitstream mode
	if ((p_attr->support_bitstream_mode & (1 << param->bitstream_mode)) == 0)
		return -EINVAL;

	/* check framebuffer endian */
	if ((p_attr->support_endian_mask & (1 << param->frame_endian)) == 0)
		return -EINVAL;

	/* check streambuffer endian */
	if ((p_attr->support_endian_mask & (1 << param->stream_endian)) == 0)
		return -EINVAL;

	return 0;
}

int vpu_dec_open_api(struct vpu_instance *vpu_inst, struct dec_open_param *pop)
{
	struct dec_info *p_dec_info;
	int     ret;
	struct vpu_device *vpu_dev = vpu_inst->dev;

	ret = check_dec_open_param(vpu_dev, pop);
	if (ret != 0)
		return ret;

	ret = mutex_lock_interruptible(&vpu_dev->hw_lock);
	if (ret)
		return ret;

	if (wave5_vpu_is_init(vpu_dev) == 0) {
		ret = -ENODEV;
		goto err_out;
	}

	vpu_inst->codec_info = kzalloc(sizeof(*vpu_inst->codec_info), GFP_KERNEL);
	if (!vpu_inst->codec_info) {
		ret = -ENOMEM;
		goto err_out;
	}

	vpu_inst->id = ida_alloc_max(&vpu_inst->dev->inst_ida, MAX_NUM_INSTANCE - 1, GFP_KERNEL);
	if (vpu_inst->id < 0) {
		dev_warn(vpu_inst->dev->dev, "unable to allocate instance ID: %d\n", vpu_inst->id);
		ret = vpu_inst->id;
		goto err_out;
	}
	p_dec_info = &vpu_inst->codec_info->dec_info;
	memcpy((void *)&p_dec_info->open_param, pop, sizeof(struct dec_open_param));

	if (vpu_inst->std == STD_UNKNOWN) {
		ret = -EINVAL;
		goto err_out;
	}

	p_dec_info->stream_wr_ptr        = pop->bitstream_buffer;
	p_dec_info->stream_rd_ptr        = pop->bitstream_buffer;
	p_dec_info->stream_buf_start_addr = pop->bitstream_buffer;
	p_dec_info->stream_buf_size      = pop->bitstream_buffer_size;
	p_dec_info->stream_buf_end_addr   = pop->bitstream_buffer + pop->bitstream_buffer_size;
	p_dec_info->reorder_enable      = TRUE;
	p_dec_info->mirror_direction    = MIRDIR_NONE;
	p_dec_info->temp_id_select_mode   = TEMPORAL_ID_MODE_ABSOLUTE;
	p_dec_info->target_temp_id       = DECODE_ALL_TEMPORAL_LAYERS;
	p_dec_info->target_spatial_id    = DECODE_ALL_SPATIAL_LAYERS;

	ret = wave5_vpu_build_up_dec_param(vpu_inst, pop);
	if (ret != RETCODE_SUCCESS)
		goto err_out;

	mutex_unlock(&vpu_dev->hw_lock);

	return 0;

err_out:
	if (vpu_inst->id >= 0)
		ida_free(&vpu_inst->dev->inst_ida, vpu_inst->id);
	kfree(vpu_inst->codec_info);
	mutex_unlock(&vpu_dev->hw_lock);

	return ret;
}

enum ret_code vpu_dec_close(struct vpu_instance *inst)
{
	struct dec_info *p_dec_info;
	enum ret_code ret;
	struct vpu_device *vpu_dev = inst->dev;
	int i;

	if (!inst->codec_info)
		return RETCODE_INVALID_HANDLE;
	p_dec_info   = &inst->codec_info->dec_info;

	if (mutex_lock_interruptible(&vpu_dev->hw_lock))
		return RETCODE_FAILURE;

	ret = wave5_vpu_dec_fini_seq(inst);
	if (ret != RETCODE_SUCCESS) {
		dev_warn(inst->dev->dev, "dec sec end timed out\n");

		if (ret == RETCODE_VPU_STILL_RUNNING) {
			mutex_unlock(&vpu_dev->hw_lock);
			return ret;
		}
	}

	dev_dbg(inst->dev->dev, "dec seq end complete\n");

	if (p_dec_info->vb_work.size)
		vdi_free_dma_memory(vpu_dev, &p_dec_info->vb_work);

	for (i = 0 ; i < MAX_REG_FRAME; i++) {
		if (p_dec_info->vb_mv[i].size)
			vdi_free_dma_memory(vpu_dev, &p_dec_info->vb_mv[i]);
		if (p_dec_info->vb_fbc_y_tbl[i].size)
			vdi_free_dma_memory(vpu_dev, &p_dec_info->vb_fbc_y_tbl[i]);
		if (p_dec_info->vb_fbc_c_tbl[i].size)
			vdi_free_dma_memory(vpu_dev, &p_dec_info->vb_fbc_c_tbl[i]);
	}

	if (p_dec_info->vb_task.size)
		vdi_free_dma_memory(vpu_dev, &p_dec_info->vb_task);

	if (inst->id >= 0)
		ida_free(&inst->dev->inst_ida, inst->id);

	mutex_unlock(&vpu_dev->hw_lock);

	kfree(inst->codec_info);

	return ret;
}

enum ret_code vpu_dec_issue_seq_init(struct vpu_instance *inst)
{
	enum ret_code     ret;
	struct vpu_device *vpu_dev = inst->dev;

	if (mutex_lock_interruptible(&vpu_dev->hw_lock))
		return RETCODE_FAILURE;

	ret = wave5_vpu_dec_init_seq(inst);

	mutex_unlock(&vpu_dev->hw_lock);

	return ret;
}

enum ret_code vpu_dec_complete_seq_init(struct vpu_instance *inst, struct dec_initial_info *info)
{
	struct dec_info *p_dec_info;
	enum ret_code     ret;
	struct vpu_device *vpu_dev = inst->dev;

	if (info == 0)
		return RETCODE_INVALID_PARAM;

	p_dec_info = &inst->codec_info->dec_info;

	if (mutex_lock_interruptible(&vpu_dev->hw_lock))
		return RETCODE_FAILURE;

	ret = wave5_vpu_dec_get_seq_info(inst, info);
	if (ret == RETCODE_SUCCESS)
		p_dec_info->initial_info_obtained = 1;

	info->rd_ptr = wave5_vpu_dec_get_rd_ptr(inst);
	info->wr_ptr = p_dec_info->stream_wr_ptr;

	p_dec_info->initial_info = *info;

	mutex_unlock(&vpu_dev->hw_lock);

	return ret;
}

enum ret_code vpu_dec_register_frame_buffer_ex(struct vpu_instance *inst,
					       int num_fbs_for_decoding, int num_fbs_for_wtl,
					       int stride, int height, int map_type)
{
	struct dec_info *p_dec_info;
	enum ret_code         ret;
	struct vpu_device *vpu_dev = inst->dev;
	struct frame_buffer *fb;

	if (num_fbs_for_decoding > MAX_GDI_IDX)
		return RETCODE_INVALID_PARAM;

	if (num_fbs_for_wtl > MAX_GDI_IDX)
		return RETCODE_INVALID_PARAM;

	p_dec_info                    = &inst->codec_info->dec_info;
	p_dec_info->num_fbs_for_decoding = num_fbs_for_decoding;
	p_dec_info->num_fbs_for_wtl      = num_fbs_for_wtl;
	p_dec_info->stride            = stride;

	if (!p_dec_info->initial_info_obtained)
		return RETCODE_WRONG_CALL_SEQUENCE;

	if (stride < p_dec_info->initial_info.pic_width || (stride % 8 != 0) ||
	    height < p_dec_info->initial_info.pic_height)
		return RETCODE_INVALID_STRIDE;

	if (mutex_lock_interruptible(&vpu_dev->hw_lock))
		return RETCODE_FAILURE;

	fb = inst->frame_buf;
	ret = wave5_vpu_dec_register_framebuffer(inst, &fb[p_dec_info->num_fbs_for_decoding],
						 LINEAR_FRAME_MAP, p_dec_info->num_fbs_for_wtl);
	if (ret != RETCODE_SUCCESS)
		goto err_out;

	ret = wave5_vpu_dec_register_framebuffer(inst, &fb[0],
						 COMPRESSED_FRAME_MAP, p_dec_info->num_fbs_for_decoding);

err_out:
	mutex_unlock(&vpu_dev->hw_lock);

	return ret;
}

enum ret_code vpu_dec_get_bitstream_buffer(struct vpu_instance *inst, dma_addr_t *prd_ptr,
					   dma_addr_t *pwr_ptr, uint32_t *size)
{
	struct dec_info *p_dec_info;
	dma_addr_t rd_ptr;
	dma_addr_t wr_ptr;
	dma_addr_t temp_ptr;
	int             room;
	struct vpu_attr *p_attr;
	struct vpu_device *vpu_dev = inst->dev;

	p_attr = &vpu_dev->attr;
	p_dec_info = &inst->codec_info->dec_info;

	if (mutex_lock_interruptible(&vpu_dev->hw_lock))
		return RETCODE_FAILURE;
	rd_ptr = wave5_vpu_dec_get_rd_ptr(inst);
	mutex_unlock(&vpu_dev->hw_lock);

	wr_ptr = p_dec_info->stream_wr_ptr;

	temp_ptr = rd_ptr;

	if (p_dec_info->open_param.bitstream_mode != BS_MODE_PIC_END) {
		if (wr_ptr < temp_ptr)
			room = temp_ptr - wr_ptr;
		else
			room = (p_dec_info->stream_buf_end_addr - wr_ptr) +
				(temp_ptr - p_dec_info->stream_buf_start_addr);
		room--;
	} else {
		room = (p_dec_info->stream_buf_end_addr - wr_ptr);
	}

	if (prd_ptr)
		*prd_ptr = temp_ptr;
	if (pwr_ptr)
		*pwr_ptr = wr_ptr;
	if (size)
		*size   = room;

	return RETCODE_SUCCESS;
}

enum ret_code vpu_dec_update_bitstream_buffer(struct vpu_instance *inst, int size)
{
	struct dec_info *p_dec_info;
	dma_addr_t wr_ptr;
	dma_addr_t rd_ptr;
	enum ret_code         ret;
	struct vpu_device *vpu_dev = inst->dev;

	if (!inst->codec_info)
		return RETCODE_INVALID_HANDLE;

	p_dec_info   = &inst->codec_info->dec_info;
	wr_ptr      = p_dec_info->stream_wr_ptr;
	rd_ptr = p_dec_info->stream_rd_ptr;

	if (size > 0) {
		if (wr_ptr < rd_ptr && rd_ptr <= wr_ptr + size)
			return RETCODE_INVALID_PARAM;

		wr_ptr += size;

		if (p_dec_info->open_param.bitstream_mode != BS_MODE_PIC_END) {
			if (wr_ptr > p_dec_info->stream_buf_end_addr) {
				u32 room = wr_ptr - p_dec_info->stream_buf_end_addr;

				wr_ptr = p_dec_info->stream_buf_start_addr;
				wr_ptr += room;
			} else if (wr_ptr == p_dec_info->stream_buf_end_addr) {
				wr_ptr = p_dec_info->stream_buf_start_addr;
			}
		}

		p_dec_info->stream_wr_ptr = wr_ptr;
		p_dec_info->stream_rd_ptr = rd_ptr;
	}

	if (mutex_lock_interruptible(&vpu_dev->hw_lock))
		return RETCODE_FAILURE;
	ret = wave5_vpu_dec_set_bitstream_flag(inst, (size == 0));
	mutex_unlock(&vpu_dev->hw_lock);

	return ret;
}

enum ret_code vpu_dec_start_one_frame(struct vpu_instance *inst, struct dec_param *param)
{
	struct dec_info *p_dec_info;
	enum ret_code     ret     = RETCODE_SUCCESS;
	struct vpu_device *vpu_dev = inst->dev;

	p_dec_info   = &inst->codec_info->dec_info;

	if (p_dec_info->stride == 0) // this means frame buffers have not been registered.
		return RETCODE_WRONG_CALL_SEQUENCE;

	if (mutex_lock_interruptible(&vpu_dev->hw_lock))
		return RETCODE_FAILURE;

	p_dec_info->frame_start_pos = p_dec_info->stream_rd_ptr;

	ret = wave5_vpu_decode(inst, param);

	mutex_unlock(&vpu_dev->hw_lock);

	return ret;
}

enum ret_code vpu_dec_get_output_info(struct vpu_instance *inst, struct dec_output_info *info)
{
	struct dec_info *p_dec_info;
	enum ret_code     ret;
	struct vpu_rect     rect_info;
	u32      val;
	s32       decoded_index;
	s32       disp_idx;
	u32      max_dec_index;
	struct vpu_device *vpu_dev = inst->dev;

	if (info == 0)
		return RETCODE_INVALID_PARAM;

	p_dec_info    = &inst->codec_info->dec_info;

	if (mutex_lock_interruptible(&vpu_dev->hw_lock))
		return RETCODE_FAILURE;

	memset((void *)info, 0x00, sizeof(struct dec_output_info));

	ret = wave5_vpu_dec_get_result(inst, info);
	if (ret != RETCODE_SUCCESS) {
		info->rd_ptr = p_dec_info->stream_rd_ptr;
		info->wr_ptr = p_dec_info->stream_wr_ptr;
		goto err_out;
	}

	decoded_index = info->index_frame_decoded;

	// calculate display frame region
	val = 0;
	if (decoded_index >= 0 && decoded_index < MAX_GDI_IDX) {
		//default value
		rect_info.left   = 0;
		rect_info.right  = info->dec_pic_width;
		rect_info.top    = 0;
		rect_info.bottom = info->dec_pic_height;

		if (inst->std == W_HEVC_DEC || inst->std == W_AVC_DEC)
			rect_info = p_dec_info->initial_info.pic_crop_rect;

		if (inst->std == W_HEVC_DEC)
			p_dec_info->dec_out_info[decoded_index].decoded_poc = info->decoded_poc;

		if (inst->std == W_AVS2_DEC)
			p_dec_info->dec_out_info[decoded_index].avs2_info.decoded_poi =
				info->avs2_info.decoded_poi;

		info->rc_decoded.left   = rect_info.left;
		p_dec_info->dec_out_info[decoded_index].rc_decoded.left   = rect_info.left;
		info->rc_decoded.right  = rect_info.right;
		p_dec_info->dec_out_info[decoded_index].rc_decoded.right  = rect_info.right;
		info->rc_decoded.top    = rect_info.top;
		p_dec_info->dec_out_info[decoded_index].rc_decoded.top    = rect_info.top;
		info->rc_decoded.bottom = rect_info.bottom;
		p_dec_info->dec_out_info[decoded_index].rc_decoded.bottom = rect_info.bottom;
	} else {
		info->rc_decoded.left   = 0;
		info->rc_decoded.right  = info->dec_pic_width;
		info->rc_decoded.top    = 0;
		info->rc_decoded.bottom = info->dec_pic_height;
	}

	disp_idx = info->index_frame_display;
	if (info->index_frame_display >= 0 && info->index_frame_display < MAX_GDI_IDX) {
		if (p_dec_info->rotation_enable) {
			switch (p_dec_info->rotation_angle) {
			case 90:
				info->rc_display.left =
					p_dec_info->dec_out_info[disp_idx].rc_decoded.top;
				info->rc_display.right  =
					p_dec_info->dec_out_info[disp_idx].rc_decoded.bottom;
				info->rc_display.top    =
					p_dec_info->dec_out_info[disp_idx].dec_pic_width -
					p_dec_info->dec_out_info[disp_idx].rc_decoded.right;
					info->rc_display.bottom =
						p_dec_info->dec_out_info[disp_idx].dec_pic_width -
						p_dec_info->dec_out_info[disp_idx].rc_decoded.left;
				break;
			case 270:
				info->rc_display.left =
					p_dec_info->dec_out_info[disp_idx].dec_pic_height -
					p_dec_info->dec_out_info[disp_idx].rc_decoded.bottom;
				info->rc_display.right =
					p_dec_info->dec_out_info[disp_idx].dec_pic_height -
					p_dec_info->dec_out_info[disp_idx].rc_decoded.top;
				info->rc_display.top =
					p_dec_info->dec_out_info[disp_idx].rc_decoded.left;
				info->rc_display.bottom =
					p_dec_info->dec_out_info[disp_idx].rc_decoded.right;
				break;
			case 180:
				info->rc_display.left   =
					p_dec_info->dec_out_info[disp_idx].rc_decoded.left;
				info->rc_display.right  =
					p_dec_info->dec_out_info[disp_idx].rc_decoded.right;
				info->rc_display.top    =
					p_dec_info->dec_out_info[disp_idx].dec_pic_height -
					p_dec_info->dec_out_info[disp_idx].rc_decoded.bottom;
				info->rc_display.bottom =
					p_dec_info->dec_out_info[disp_idx].dec_pic_height -
					p_dec_info->dec_out_info[disp_idx].rc_decoded.top;
				break;
			default:
				info->rc_display.left   =
					p_dec_info->dec_out_info[disp_idx].rc_decoded.left;
				info->rc_display.right  =
					p_dec_info->dec_out_info[disp_idx].rc_decoded.right;
				info->rc_display.top    =
					p_dec_info->dec_out_info[disp_idx].rc_decoded.top;
				info->rc_display.bottom =
					p_dec_info->dec_out_info[disp_idx].rc_decoded.bottom;
				break;
			}

		} else {
			info->rc_display.left   = p_dec_info->dec_out_info[disp_idx].rc_decoded.left;
			info->rc_display.right  = p_dec_info->dec_out_info[disp_idx].rc_decoded.right;
			info->rc_display.top    = p_dec_info->dec_out_info[disp_idx].rc_decoded.top;
			info->rc_display.bottom = p_dec_info->dec_out_info[disp_idx].rc_decoded.bottom;
		}

		if (p_dec_info->mirror_enable) {
			u32 temp;
			u32 width  = (p_dec_info->rotation_angle == 90 ||
					p_dec_info->rotation_angle == 270) ? info->dec_pic_height :
				info->dec_pic_width;
			u32 height = (p_dec_info->rotation_angle == 90 ||
					p_dec_info->rotation_angle == 270) ? info->dec_pic_width :
				info->dec_pic_height;

			if (p_dec_info->mirror_direction & MIRDIR_VER) {
				temp = info->rc_display.top;
				info->rc_display.top    = height - info->rc_display.bottom;
				info->rc_display.bottom = height - temp;
			}
			if (p_dec_info->mirror_direction & MIRDIR_HOR) {
				temp = info->rc_display.left;
				info->rc_display.left  = width - info->rc_display.right;
				info->rc_display.right = width - temp;
			}
		}

		switch (inst->std) {
		case W_HEVC_DEC:
			info->display_poc = p_dec_info->dec_out_info[disp_idx].decoded_poc;
			break;
		case W_AVS2_DEC:
			info->avs2_info.display_poi = p_dec_info->dec_out_info[disp_idx].avs2_info.decoded_poi;
			break;
		case W_AVC_DEC:
			info->display_poc = p_dec_info->dec_out_info[disp_idx].decoded_poc;
			break;
		default:
			break;
		}

		if (info->index_frame_display == info->index_frame_decoded) {
			info->disp_pic_width =  info->dec_pic_width;
			info->disp_pic_height = info->dec_pic_height;
		} else {
			/*
			 * when index_frame_decoded < 0, and index_frame_display >= 0
			 * info->dec_pic_width and info->dec_pic_height are still valid
			 * but those of p_dec_info->dec_out_info[disp_idx] are invalid in VP9
			 */
			info->disp_pic_width  = p_dec_info->dec_out_info[disp_idx].dec_pic_width;
			info->disp_pic_height = p_dec_info->dec_out_info[disp_idx].dec_pic_height;
		}

	} else {
		info->rc_display.left   = 0;
		info->rc_display.right  = 0;
		info->rc_display.top    = 0;
		info->rc_display.bottom = 0;

		if (p_dec_info->rotation_enable || p_dec_info->mirror_enable ||
		    p_dec_info->dering_enable) {
			info->disp_pic_width  = info->dec_pic_width;
			info->disp_pic_height = info->dec_pic_height;
		} else {
			info->disp_pic_width = 0;
			info->disp_pic_height = 0;
		}
	}

	p_dec_info->stream_rd_ptr      = wave5_vpu_dec_get_rd_ptr(inst);
	p_dec_info->frame_display_flag = vpu_read_reg(vpu_dev, W5_RET_DEC_DISP_IDC);
	if (inst->std == W_VP9_DEC)
		p_dec_info->frame_display_flag  &= 0xFFFF;
	p_dec_info->frame_end_pos      = p_dec_info->stream_rd_ptr;

	if (p_dec_info->frame_end_pos < p_dec_info->frame_start_pos)
		info->consumed_byte = p_dec_info->frame_end_pos + p_dec_info->stream_buf_size -
			p_dec_info->frame_start_pos;
	else
		info->consumed_byte = p_dec_info->frame_end_pos - p_dec_info->frame_start_pos;

	if (p_dec_info->dering_enable || p_dec_info->mirror_enable || p_dec_info->rotation_enable) {
		info->disp_frame        = p_dec_info->rotator_output;
		info->disp_frame.stride = p_dec_info->rotator_stride;
	} else {
		val = p_dec_info->num_fbs_for_decoding; //fb_offset

		max_dec_index = (p_dec_info->num_fbs_for_decoding > p_dec_info->num_fbs_for_wtl) ?
			p_dec_info->num_fbs_for_decoding : p_dec_info->num_fbs_for_wtl;

		if (info->index_frame_display >= 0 && info->index_frame_display < (int)max_dec_index)
			info->disp_frame = inst->frame_buf[val + info->index_frame_display];
	}

	info->rd_ptr            = p_dec_info->stream_rd_ptr;
	info->wr_ptr            = p_dec_info->stream_wr_ptr;
	info->frame_display_flag = p_dec_info->frame_display_flag;

	info->sequence_no = p_dec_info->initial_info.sequence_no;
	if (decoded_index >= 0 && decoded_index < MAX_GDI_IDX)
		p_dec_info->dec_out_info[decoded_index] = *info;

	if (disp_idx >= 0 && disp_idx < MAX_GDI_IDX) {
		info->num_of_tot_m_bs_in_display = p_dec_info->dec_out_info[disp_idx].num_of_tot_m_bs;
		info->num_of_err_m_bs_in_display = p_dec_info->dec_out_info[disp_idx].num_of_err_m_bs;
		info->disp_frame.sequence_no = info->sequence_no;
	} else {
		info->num_of_tot_m_bs_in_display = 0;
		info->num_of_err_m_bs_in_display = 0;
	}

	if (info->sequence_changed != 0) {
		if ((info->sequence_changed & SEQ_CHANGE_INTER_RES_CHANGE) != SEQ_CHANGE_INTER_RES_CHANGE)
			p_dec_info->initial_info.sequence_no++;
	}

err_out:
	mutex_unlock(&vpu_dev->hw_lock);

	return RETCODE_SUCCESS;
}

enum ret_code vpu_dec_clr_disp_flag(struct vpu_instance *inst, int index)
{
	struct dec_info *p_dec_info;
	enum ret_code     ret         = RETCODE_SUCCESS;
	s32       end_index;
	struct vpu_device *vpu_dev = inst->dev;

	p_dec_info   = &inst->codec_info->dec_info;

	end_index = p_dec_info->num_fbs_for_wtl;

	if (index < 0 || index > end_index - 1)
		return RETCODE_INVALID_PARAM;

	if (mutex_lock_interruptible(&vpu_dev->hw_lock))
		return RETCODE_FAILURE;
	ret = wave5_dec_clr_disp_flag(inst, index);
	mutex_unlock(&vpu_dev->hw_lock);

	return ret;
}

enum ret_code vpu_dec_give_command(struct vpu_instance *inst, enum codec_command cmd, void *param)
{
	struct dec_info *p_dec_info;

	p_dec_info = &inst->codec_info->dec_info;
	switch (cmd) {
	case DEC_GET_QUEUE_STATUS:
		{
			struct queue_status_info *queue_info = (struct queue_status_info *)param;

			queue_info->instance_queue_count = p_dec_info->instance_queue_count;
			queue_info->report_queue_count    = p_dec_info->report_queue_count;
			break;
		}

	case ENABLE_DEC_THUMBNAIL_MODE:
		{
			p_dec_info->thumbnail_mode = 1;
			break;
		}
	default:
		return RETCODE_INVALID_COMMAND;
	}

	return 0;
}

int vpu_enc_open_api(struct vpu_instance *vpu_inst, struct enc_open_param *pop)
{
	struct enc_info *p_enc_info;
	int     ret;
	struct vpu_device *vpu_dev = vpu_inst->dev;

	ret = wave5_vpu_enc_check_open_param(vpu_inst, pop);
	if (ret != RETCODE_SUCCESS)
		return ret;

	ret = mutex_lock_interruptible(&vpu_dev->hw_lock);
	if (ret)
		return ret;

	if (wave5_vpu_is_init(vpu_dev) == 0) {
		ret = -ENODEV;
		goto err_out;
	}

	vpu_inst->codec_info = kzalloc(sizeof(vpu_inst->codec_info), GFP_KERNEL);
	if (!vpu_inst->codec_info) {
		ret = -ENOMEM;
		goto err_out;
	}

	vpu_inst->id = ida_alloc_max(&vpu_inst->dev->inst_ida, MAX_NUM_INSTANCE - 1, GFP_KERNEL);
	if (vpu_inst->id < 0) {
		dev_warn(vpu_inst->dev->dev, "unable to allocate instance ID: %d\n", vpu_inst->id);
		ret = vpu_inst->id;
		goto err_out;
	}
	p_enc_info = &vpu_inst->codec_info->enc_info;
	p_enc_info->open_param = *pop;

	ret = wave5_vpu_build_up_enc_param(vpu_dev->dev, vpu_inst, pop);
	if (ret != RETCODE_SUCCESS)
		goto err_out;
	mutex_unlock(&vpu_dev->hw_lock);

	return ret;
err_out:
	if (vpu_inst->id >= 0)
		ida_free(&vpu_inst->dev->inst_ida, vpu_inst->id);
	kfree(vpu_inst->codec_info);
	mutex_unlock(&vpu_dev->hw_lock);

	return ret;
}

enum ret_code vpu_enc_close(struct vpu_instance *inst)
{
	struct enc_info *p_enc_info;
	enum ret_code     ret;
	struct vpu_device *vpu_dev = inst->dev;

	if (!inst->codec_info)
		return RETCODE_INVALID_HANDLE;
	p_enc_info = &inst->codec_info->enc_info;

	if (mutex_lock_interruptible(&vpu_dev->hw_lock))
		return RETCODE_FAILURE;

	ret = wave5_vpu_enc_fini_seq(inst);
	if (ret != RETCODE_SUCCESS) {
		dev_warn(inst->dev->dev, "enc seq end timed out\n");

		if (ret == RETCODE_VPU_STILL_RUNNING) {
			mutex_unlock(&vpu_dev->hw_lock);
			return ret;
		}
	}

	dev_dbg(inst->dev->dev, "enc seq end timed out\n");

	if (p_enc_info->vb_work.size)
		vdi_free_dma_memory(vpu_dev, &p_enc_info->vb_work);

	if (inst->std == W_HEVC_ENC || inst->std == W_AVC_ENC) {
		if (p_enc_info->vb_sub_sam_buf.size)
			vdi_free_dma_memory(vpu_dev, &p_enc_info->vb_sub_sam_buf);

		if (p_enc_info->vb_mv.size)
			vdi_free_dma_memory(vpu_dev, &p_enc_info->vb_mv);

		if (p_enc_info->vb_fbc_y_tbl.size)
			vdi_free_dma_memory(vpu_dev, &p_enc_info->vb_fbc_y_tbl);

		if (p_enc_info->vb_fbc_c_tbl.size)
			vdi_free_dma_memory(vpu_dev, &p_enc_info->vb_fbc_c_tbl);
	}

	if (p_enc_info->vb_task.size)
		vdi_free_dma_memory(vpu_dev, &p_enc_info->vb_task);

	if (inst->id >= 0)
		ida_free(&inst->dev->inst_ida, inst->id);

	mutex_unlock(&vpu_dev->hw_lock);

	kfree(inst->codec_info);

	return ret;
}

enum ret_code vpu_enc_register_frame_buffer(struct vpu_instance *inst, int num, int stride,
					    int height, enum tiled_map_type map_type)
{
	struct enc_info *p_enc_info;
	enum ret_code         ret;
	struct enc_open_param *open_param;
	struct frame_buffer *fb;
	struct vpu_device *vpu_dev = inst->dev;
	u32 size_luma, size_chroma;
	int i;

	p_enc_info = &inst->codec_info->enc_info;
	open_param = &p_enc_info->open_param;

	if (p_enc_info->stride)
		return RETCODE_CALLED_BEFORE;

	if (!p_enc_info->initial_info_obtained)
		return RETCODE_WRONG_CALL_SEQUENCE;

	if (num < p_enc_info->initial_info.min_frame_buffer_count)
		return RETCODE_INSUFFICIENT_FRAME_BUFFERS;

	if (stride == 0 || (stride % 8 != 0) || stride < 0)
		return RETCODE_INVALID_STRIDE;

	if (height == 0 || height < 0)
		return RETCODE_INVALID_PARAM;

	if (mutex_lock_interruptible(&vpu_dev->hw_lock))
		return RETCODE_FAILURE;

	p_enc_info->num_frame_buffers   = num;
	p_enc_info->stride            = stride;

	fb = inst->frame_buf;
	size_luma   = stride * height;
	size_chroma = ALIGN(stride / 2, 16) * height;

	for (i = 0; i < num; i++) {
		if (fb[i].update_fb_info) {
			fb[i].update_fb_info = FALSE;
			fb[i].stride         = stride;
			fb[i].height         = height;
			fb[i].map_type        = COMPRESSED_FRAME_MAP;
			fb[i].cbcr_interleave = true;
			fb[i].endian         = VDI_128BIT_LITTLE_ENDIAN;
			fb[i].buf_y_size       = size_luma;
			fb[i].buf_cb          = fb[i].buf_y + size_luma;
			fb[i].buf_cb_size      = size_chroma;
			fb[i].buf_cr_size      = 0;
		}
	}

	ret = wave5_vpu_enc_register_framebuffer(inst->dev->dev, inst, &fb[0],
						 COMPRESSED_FRAME_MAP, p_enc_info->num_frame_buffers);

	mutex_unlock(&vpu_dev->hw_lock);

	return ret;
}

static enum ret_code check_enc_param(struct vpu_instance *inst, struct enc_param *param)
{
	struct enc_info *p_enc_info;

	p_enc_info = &inst->codec_info->enc_info;

	if (param == 0)
		return RETCODE_INVALID_PARAM;

	if (param->skip_picture != 0 && param->skip_picture != 1)
		return RETCODE_INVALID_PARAM;
	if (param->skip_picture == 0) {
		if (param->source_frame == 0)
			return RETCODE_INVALID_FRAME_BUFFER;
	}
	if (p_enc_info->open_param.bit_rate == 0) { // no rate control
		if (inst->std == W_HEVC_ENC) {
			if (param->force_pic_qp_enable == 1) {
				if (param->force_pic_qp_i < 0 || param->force_pic_qp_i > 63)
					return RETCODE_INVALID_PARAM;

				if (param->force_pic_qp_p < 0 || param->force_pic_qp_p > 63)
					return RETCODE_INVALID_PARAM;

				if (param->force_pic_qp_b < 0 || param->force_pic_qp_b > 63)
					return RETCODE_INVALID_PARAM;
			}
			if (p_enc_info->ring_buffer_enable == 0) {
				if (param->pic_stream_buffer_addr % 16 || param->pic_stream_buffer_size == 0)
					return RETCODE_INVALID_PARAM;
			}
		}
	}
	if (p_enc_info->ring_buffer_enable == 0) {
		if (param->pic_stream_buffer_addr % 8 || param->pic_stream_buffer_size == 0)
			return RETCODE_INVALID_PARAM;
	}

	return RETCODE_SUCCESS;
}

static uint64_t get_timestamp(struct vpu_instance *vpu_inst)
{
	struct enc_info *p_enc_info   = NULL;
	u64      pts;
	u32      fps;

	if (!vpu_inst->codec_info)
		return 0;

	p_enc_info   = &vpu_inst->codec_info->enc_info;
	fps        = p_enc_info->open_param.frame_rate_info;
	if (fps == 0)
		fps    = 30;        /* 30 fps */

	pts        = p_enc_info->cur_pts;
	p_enc_info->cur_pts += 90000 / fps; /* 90_k_hz/fps */

	return pts;
}

enum ret_code vpu_enc_start_one_frame(struct vpu_instance *inst, struct enc_param *param)
{
	struct enc_info *p_enc_info;
	enum ret_code             ret;
	struct vpu_device *vpu_dev = inst->dev;

	p_enc_info   = &inst->codec_info->enc_info;

	if (p_enc_info->stride == 0) // this means frame buffers have not been registered.
		return RETCODE_WRONG_CALL_SEQUENCE;

	ret = check_enc_param(inst, param);
	if (ret != RETCODE_SUCCESS)
		return ret;

	if (mutex_lock_interruptible(&vpu_dev->hw_lock))
		return RETCODE_FAILURE;

	p_enc_info->pts_map[param->src_idx] = p_enc_info->open_param.enable_pts ? get_timestamp(inst) : param->pts;

	ret = wave5_vpu_encode(inst, param);

	mutex_unlock(&vpu_dev->hw_lock);

	return ret;
}

enum ret_code vpu_enc_get_output_info(struct vpu_instance *inst, struct enc_output_info *info)
{
	struct enc_info *p_enc_info;
	enum ret_code     ret;
	struct vpu_device *vpu_dev = inst->dev;

	if (info == 0)
		return RETCODE_INVALID_PARAM;

	p_enc_info   = &inst->codec_info->enc_info;

	if (mutex_lock_interruptible(&vpu_dev->hw_lock))
		return RETCODE_FAILURE;

	ret = wave5_vpu_enc_get_result(inst, info);

	if (ret == RETCODE_SUCCESS) {
		if (info->enc_src_idx >= 0 && info->recon_frame_index >= 0)
			info->pts = p_enc_info->pts_map[info->enc_src_idx];
	} else {
		info->pts = 0LL;
	}

	mutex_unlock(&vpu_dev->hw_lock);

	return ret;
}

enum ret_code vpu_enc_give_command(struct vpu_instance *inst, enum codec_command cmd, void *param)
{
	struct enc_info *p_enc_info;

	p_enc_info = &inst->codec_info->enc_info;
	switch (cmd) {
	case ENABLE_ROTATION:
		{
			p_enc_info->rotation_enable = 1;
		}
		break;
	case ENABLE_MIRRORING:
		{
			p_enc_info->mirror_enable = 1;
		}
		break;
	case SET_MIRROR_DIRECTION:
		{
			enum mirror_direction mir_dir;

			if (param == 0)
				return RETCODE_INVALID_PARAM;

			mir_dir = *(enum mirror_direction *)param;
			if (!(mir_dir == MIRDIR_NONE) && !(mir_dir == MIRDIR_HOR) &&
			    !(mir_dir == MIRDIR_VER) && !(mir_dir == MIRDIR_HOR_VER))
				return RETCODE_INVALID_PARAM;
			p_enc_info->mirror_direction = mir_dir;
		}
		break;
	case SET_ROTATION_ANGLE:
		{
			int angle;

			if (param == 0)
				return RETCODE_INVALID_PARAM;
			angle = *(int *)param;
			if (angle != 0 && angle != 90 && angle != 180 && angle != 270)
				return RETCODE_INVALID_PARAM;
			if (p_enc_info->initial_info_obtained && (angle == 90 || angle == 270))
				return RETCODE_INVALID_PARAM;
			p_enc_info->rotation_angle = angle;
		}
		break;
	case ENC_GET_QUEUE_STATUS:
		{
			struct queue_status_info *queue_info = (struct queue_status_info *)param;

			queue_info->instance_queue_count = p_enc_info->instance_queue_count;
			queue_info->report_queue_count    = p_enc_info->report_queue_count;
			break;
		}
	default:
		return RETCODE_INVALID_COMMAND;
	}
	return RETCODE_SUCCESS;
}

enum ret_code vpu_enc_issue_seq_init(struct vpu_instance *inst)
{
	enum ret_code     ret;
	struct vpu_device *vpu_dev = inst->dev;

	if (mutex_lock_interruptible(&vpu_dev->hw_lock))
		return RETCODE_FAILURE;

	ret = wave5_vpu_enc_init_seq(inst);

	mutex_unlock(&vpu_dev->hw_lock);

	return ret;
}

enum ret_code vpu_enc_complete_seq_init(struct vpu_instance *inst, struct enc_initial_info *info)
{
	struct enc_info *p_enc_info;
	enum ret_code     ret;
	struct vpu_device *vpu_dev = inst->dev;

	if (info == 0)
		return RETCODE_INVALID_PARAM;

	p_enc_info = &inst->codec_info->enc_info;

	if (mutex_lock_interruptible(&vpu_dev->hw_lock))
		return RETCODE_FAILURE;

	ret = wave5_vpu_enc_get_seq_info(inst, info);
	if (ret == RETCODE_SUCCESS)
		p_enc_info->initial_info_obtained = 1;

	p_enc_info->initial_info = *info;

	mutex_unlock(&vpu_dev->hw_lock);

	return ret;
}

