// SPDX-License-Identifier: GPL-2.0+
/*
*
* Zhouyi AI Accelerator driver
*
* Copyright (C) 2020 Arm (China) Ltd.
* Copyright (C) 2021 Cai Huoqing
*/

/**
 * @file zynpu_mm.c
 * Implementations of the ZYNPU memory management supports Address Space Extension (ASE)
 */

#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <asm/div64.h>
#include "zynpu.h"
#include "zynpu_mm.h"

#define ZYNPU_CONFIG_SRAM_DATA_ASID ZYNPU_MM_DATA_TYPE_REUSE

static inline int get_asid(struct zynpu_memory_manager *mm, enum zynpu_mm_data_type type)
{
	int asid = ZYNPU_ASE_ID_NONE;

	if (!mm)
		return asid;

	switch (type) {
	case ZYNPU_MM_DATA_TYPE_TEXT:
	case ZYNPU_MM_DATA_TYPE_RO_STACK:
		return ZYNPU_ASE_ID_0;
	case ZYNPU_MM_DATA_TYPE_STATIC:
		return ZYNPU_ASE_ID_1;
	case ZYNPU_MM_DATA_TYPE_REUSE:
		return ZYNPU_ASE_ID_2;
	case ZYNPU_MM_DATA_TYPE_NONE:
		return ZYNPU_ASE_ID_ALL;
	default:
		return asid;
	}
}

static inline void *zynpu_remap_region_nocache(struct zynpu_memory_manager *mm, u64 base, u64 bytes)
{
	if ((!mm) || (!bytes))
		return NULL;

	return memremap(base, bytes, MEMREMAP_WT);
}

static inline void zynpu_unmap_region_nocache(void *va)
{
	if (va)
		memunmap(va);
}

static void *zynpu_alloc_cma_region_nocache(struct zynpu_memory_manager *mm, u64 *base, u64 bytes)
{
	int ret = 0;
	void *va = NULL;

	if ((!mm) || (!bytes))
		return va;

	ret = dma_set_mask(mm->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(mm->dev, "DMA set mask failed!\n");
		goto finish;
	}

	ret = dma_set_coherent_mask(mm->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(mm->dev, "DMA set coherent mask failed!\n");
		goto finish;
	}

	va = dma_alloc_coherent(mm->dev, bytes, (dma_addr_t *)base, GFP_KERNEL);
	if (!va) {
		dev_err(mm->dev, "DMA alloc coherent failed: pa 0x%llx, bytes = 0x%llx\n",
			*base, bytes);
	} else {
		dev_info(mm->dev, "DMA alloc coherent failed: pa 0x%llx, bytes = 0x%llx\n",
			 *base, bytes);
	}

finish:
	return va;
}

static void zynpu_free_cma_region_nocache(struct zynpu_memory_manager *mm, struct zynpu_mem_region *region)
{
	if ((!mm) || (!region))
		return;

	dma_free_coherent(mm->dev, region->tot_bytes, region->va, region->pa);
}

static struct zynpu_block *create_block(u64 base, u64 bytes, int tid, enum zynpu_mm_data_type type,
	enum zynpu_blk_state state)
{
	struct zynpu_block *blk = NULL;

	blk = kzalloc(sizeof(struct zynpu_block), GFP_KERNEL);
	if (!blk)
		return blk;

	blk->pa = base;
	blk->bytes = bytes;
	blk->tid = tid;
	blk->type = type;
	blk->state = state;
	INIT_LIST_HEAD(&blk->list);

	return blk;
}

static inline struct zynpu_block *create_block_list_head(u64 base, u64 bytes)
{
	return create_block(base, bytes, 0, ZYNPU_MM_DATA_TYPE_NONE, ZYNPU_BLOCK_STATE_FREE);
}

static int zynpu_mm_find_block_candidate_no_lock(struct zynpu_block *head, u64 bytes, u64 alignment,
	int data_type, struct zynpu_block **found, u64 *pa)
{
	struct zynpu_block *blk_cand = NULL;
	u64 start = 0;
	u64 end = 0;
	int ret = -ENOMEM;
	u64 result = 0;

	if (!found)
		return -EINVAL;

	if ((!head) || (!bytes) || (!alignment) || (alignment % PAGE_SIZE)) {
		ret = -EINVAL;
		goto failed;
	}

	/**
	 * allocate for text/ro/stack togetherly in non-reverse direction because
	 * for the same job, they must be allocated in the same ASE0 region and
	 * we wish to make them as closer as possible to make RW access control
	 * in ASE.
	 */
	if ((data_type == ZYNPU_MM_DATA_TYPE_TEXT) ||
	    (data_type == ZYNPU_MM_DATA_TYPE_RO_STACK)) {
		list_for_each_entry(blk_cand, &head->list, list) {
			if (blk_cand->state != ZYNPU_BLOCK_STATE_ALLOCATED) {
				start = ALIGN(blk_cand->pa, alignment);
				end = start + bytes;
				if (end <= (blk_cand->pa + blk_cand->bytes))
					goto success;
			}
		}
	} else {
		list_for_each_entry_reverse(blk_cand, &head->list, list) {
			if (blk_cand->state != ZYNPU_BLOCK_STATE_ALLOCATED) {
				result = blk_cand->pa + blk_cand->bytes - bytes;
				do_div(result, alignment);
				start = result * alignment;
				end = start + bytes;
				if ((start >= blk_cand->pa) &&
				    (end <= (blk_cand->pa + blk_cand->bytes)))
					goto success;
			}
		}
	}

failed:
	*found = NULL;
	*pa = 0;
	return ret;

success:
	*found = blk_cand;
	*pa = start;
	return 0;
}

static int zynpu_mm_split_block_no_lock(struct zynpu_block *target, u64 alloc_base, u64 alloc_bytes,
	enum zynpu_mm_data_type type)
{
	u64 alloc_start = alloc_base;
	u64 alloc_end = alloc_start + alloc_bytes - 1;
	u64 target_start = target->pa;
	u64 target_end = target->pa + target->bytes - 1;
	struct zynpu_block *alloc_blk = NULL;
	struct zynpu_block *remaining_blk = target;

	if ((!target) || (!alloc_bytes) || (alloc_end < target_start) ||
	    (alloc_end > target_end))
		return -EINVAL;

	if ((alloc_start == target_start) && (alloc_end == target_end)) {
		/*
		  alloc block:	      |<-----------alloc------------>|
		  equals to
		  target block to be split: |<----------target------------>|
		*/
		alloc_blk = target;
		alloc_blk->tid = task_pid_nr(current);
		alloc_blk->type = type;
		alloc_blk->state = ZYNPU_BLOCK_STATE_ALLOCATED;
	} else {
		alloc_blk = create_block(alloc_start, alloc_bytes, task_pid_nr(current),
			type, ZYNPU_BLOCK_STATE_ALLOCATED);
		if (!alloc_blk)
			return -ENOMEM;
		if ((alloc_start == target_start) && (alloc_end < target_end)) {
			/*
			  alloc block:	      |<---alloc--->|<--remaining-->|
			  smaller than and start from base of
			  target block to be split: |<----------target----------->|
			*/
			remaining_blk->pa += alloc_blk->bytes;
			remaining_blk->bytes -= alloc_blk->bytes;
			list_add_tail(&alloc_blk->list, &remaining_blk->list);
		} else if ((alloc_start > target_start) && (alloc_end == target_end)) {
			/*
			  alloc block:	      |<--remaining-->|<---alloc--->|
			  smaller than and end at end of
			  target block to be split: |<----------target----------->|
			*/
			remaining_blk->bytes -= alloc_blk->bytes;
			list_add(&alloc_blk->list, &remaining_blk->list);
		} else {
			/*
			  alloc block:	      |<-fr_remaining->|<--alloc-->|<-bk_remaining->|
			  insides of
			  target block to be split: |<-------------------target------------------>|
			*/
			/* front remaining */
			remaining_blk->bytes = alloc_start - remaining_blk->pa;
			list_add(&alloc_blk->list, &remaining_blk->list);
			/* back remaining */
			remaining_blk = create_block(alloc_end + 1, target_end - alloc_end,
				task_pid_nr(current), type, ZYNPU_BLOCK_STATE_FREE);
			list_add(&remaining_blk->list, &alloc_blk->list);
		}
	}

	return 0;
}

static int zynpu_mm_alloc_in_region_compact_no_lock(struct zynpu_memory_manager *mm,
	struct zynpu_mem_region *region, struct buf_request *buf_req, struct zynpu_buffer *buf)
{
	int ret = 0;
	u64 compact_bytes = 0;
	u64 alignment = 0;
	u64 alloc_pa = 0;
	struct zynpu_block *blk_cand = NULL;

	if ((!region) || (!buf_req) || (!buf))
		return -EINVAL;

	/**
	 * Compact allocation means that the buffer size is round up as page size
	 * therefore the buffer size allocated is less than the region specified
	 * in ASE therefore memory is saved compared with strict allocation but
	 * the RW control is less strict
	 */

	/**
	 * Compact alloc:
	 * roundup block size = roundup_pow_of_two(requested size)
	 * alloc block size = roundup_page_size(requested size)
	 * For example:
	 *	      |<-------------requested (9KB)----------->|
	 *	      |<--------------------------roundup (16KB)--------------------------->|
	 *	      |<----------------alloc (12KB)----------------->|<--remaining (4KB)-->|
	 *	 0x10_0000_4000						     0x10_0000_8000
	 *
	 * Buffer returned to UMD: alloc block
	 *     PA: 0x10_0000_4000
	 *     Size: 0x3000
	 *
	 * ASE Registers:
	 *     ASE_X_Control[7:0]: 3 (16KB)
	 *     ASE_X_High_Base[31:0]: 0x10
	 *     ASE_X_Low_Base[31:0]: 0x4000
	 *
	 * Base address used to calculate absolute address of buffer(s) inside alloc block: 0x0
	 *
	 * The 4KB remaining block is free to be allocated later.
	 */
	compact_bytes = ALIGN(buf_req->bytes, PAGE_SIZE);
	alignment = buf_req->align_in_page * 4 * 1024;
	ret = zynpu_mm_find_block_candidate_no_lock(region->blk_head,
		compact_bytes, alignment, buf_req->data_type, &blk_cand, &alloc_pa);
	if (ret)
		goto finish;

	/* found matching block candidate: update block list */
	if (zynpu_mm_split_block_no_lock(blk_cand, alloc_pa, compact_bytes,
	    (enum zynpu_mm_data_type)buf_req->data_type)) {
		ret = -ENOMEM;
		goto finish;
	}

	/* success */
	buf->pa = alloc_pa;
	buf->va = (void *)((unsigned long)region->va + alloc_pa - region->pa);
	buf->bytes = compact_bytes;
	buf_req->errcode = 0;

finish:
	return ret;
}

static int zynpu_init_region(int id, struct zynpu_memory_manager *mm, u64 base, u64 bytes,
	enum zynpu_mem_type type, struct zynpu_mem_region *region)
{
	struct zynpu_block *new_blk = NULL;

	if ((!mm) || (!bytes) || (!region))
		return -EINVAL;

	region->id = id;

	region->blk_head = create_block_list_head(0, 0);
	new_blk = create_block_list_head(base, bytes);
	list_add(&new_blk->list, &region->blk_head->list);

	mutex_init(&region->lock);
	region->pa = base;
	region->tot_bytes = bytes;
	region->tot_free_bytes = bytes;
	region->type = type;

	region->alloc_in_region = zynpu_mm_alloc_in_region_compact_no_lock;

	INIT_LIST_HEAD(&region->list);

	return 0;
}

static int zynpu_update_mm_regions(struct zynpu_memory_manager *mm, struct zynpu_mem_region *head,
	int *region_cnt, struct zynpu_mem_region *new_region)
{
	if ((!head) || (!region_cnt) || (!new_region))
		return -EINVAL;

	list_add(&new_region->list, &head->list);
	(*region_cnt)++;

	return 0;
}

static struct zynpu_mem_region *create_region_list_head(void)
{
	struct zynpu_mem_region *region = NULL;

	region = kzalloc(sizeof(struct zynpu_mem_region), GFP_KERNEL);
	if (!region)
		return region;

	mutex_init(&region->lock);
	INIT_LIST_HEAD(&region->list);
	return region;
}

static int zynpu_mm_try_alloc_in_region(struct zynpu_memory_manager *mm, struct zynpu_mem_region *region,
	struct buf_request *buf_req, struct zynpu_buffer *buf)
{
	int ret = 0;

	if ((!region) || (!buf_req) || (!buf))
		return -EINVAL;

	mutex_lock(&region->lock);
	ret = region->alloc_in_region(mm, region, buf_req, buf);
	if (!ret) {
		region->tot_free_bytes -= buf->bytes;
		buf->region_id = region->id;
		buf->type = region->type;
		pr_debug("[zynpu_mm_try_alloc_in_region] alloc done: PA 0x%llx, size 0x%llx",
			buf->pa, buf->bytes);
	}
	mutex_unlock(&region->lock);

	return ret;
}

static int zynpu_mm_free_in_region(struct zynpu_mem_region *region, struct buf_desc *buf)
{
	int ret = 0;
	int found = 0;
	struct zynpu_block *target = NULL;
	struct zynpu_block *prev = NULL;
	struct zynpu_block *next = NULL;

	if ((!region) || (!buf))
		return -EINVAL;

	mutex_lock(&region->lock);

	list_for_each_entry(target, &region->blk_head->list, list) {
		if ((target->pa == buf->pa) && (target->bytes == buf->bytes)) {
			found = 1;
			break;
		}
	}

	if (!found) {
		ret = -EINVAL;
		goto unlock;
	}

	/* update target block to be free state */
	target->tid = 0;
	target->type = ZYNPU_MM_DATA_TYPE_NONE;
	target->state = ZYNPU_BLOCK_STATE_FREE;

	/*
	    merge prev block and next block if they are free/aligned

	    block list: ... <=> |<--prev-->| <=> |<--target-->| <=> |<--next-->| <=> ...
				    free	      free	   free/aligned

	    block list: ... <=> |<------------merged new block--------------->| <=> ...
						    free
	*/
	prev = list_prev_entry(target, list);
	next = list_next_entry(target, list);

	if ((prev->bytes != 0) && (prev->state == ZYNPU_BLOCK_STATE_FREE)) {
		prev->bytes += target->bytes;
		list_del(&target->list);
		kfree(target);
		target = prev;
	}

	if ((next->bytes != 0) && (next->state != ZYNPU_BLOCK_STATE_ALLOCATED)) {
		target->bytes += next->bytes;
		list_del(&next->list);
		kfree(next);
		next = NULL;
	}

	region->tot_free_bytes += buf->bytes;

unlock:
	mutex_unlock(&region->lock);
	return ret;
}

static int zynpu_mm_scan_regions_alloc(struct zynpu_memory_manager *mm, struct zynpu_mem_region *head,
	struct buf_request *buf_req, struct zynpu_buffer *buf)
{
	int ret = -ENOMEM;
	struct zynpu_mem_region *region = NULL;

	if ((!mm) || (!head) || (!buf_req) || (!buf))
		return -EINVAL;

	/**
	 * Z2:
	 * Find target region for ro/stack directly by matching region ID because
	 * they must be allocated in the same region as text region allocated before.
	 * Note: text should be requested to allocate first!
	 */
	if ((mm->version == ZYNPU_VERSION_ZHOUYI_V2) &&
	    (buf_req->data_type == ZYNPU_MM_DATA_TYPE_RO_STACK)) {
		list_for_each_entry(region, &head->list, list) {
			if (buf_req->region_id == region->id) {
				ret = zynpu_mm_try_alloc_in_region(mm, region, buf_req, buf);
				if (!ret)
					break;
			}
		}
	} else {
		list_for_each_entry(region, &head->list, list) {
			if (region->tot_free_bytes >= ALIGN(buf_req->bytes, PAGE_SIZE)) {
				ret = zynpu_mm_try_alloc_in_region(mm, region, buf_req, buf);
				if (!ret)
					break;
			}
		}
	}

	return ret;
}

static struct zynpu_mem_region *zynpu_mm_find_region(struct zynpu_mem_region *head, u64 pa, u64 bytes)
{
	struct zynpu_mem_region *region = NULL;

	if ((!head) || (!bytes))
		return region;

	list_for_each_entry(region, &head->list, list) {
		if ((pa >= region->pa) &&
		    ((pa + bytes) <= (region->pa + region->tot_bytes)))
			return region;
	}

	return NULL;
}

static int zynpu_mm_deinit_region(struct zynpu_memory_manager *mm, struct zynpu_mem_region *region)
{
	struct zynpu_block *prev = NULL;
	struct zynpu_block *next = NULL;

	if (!region)
		return -EINVAL;

	mutex_lock(&region->lock);

	list_for_each_entry_safe(prev, next, &region->blk_head->list, list) {
		kfree(prev);
		prev = NULL;
	}
	kfree(region->blk_head);
	region->blk_head = NULL;

	if (region->type == ZYNPU_MEM_TYPE_SRAM)
		zynpu_unmap_region_nocache(region->va);
	else if (region->type == ZYNPU_MEM_TYPE_CMA)
		zynpu_free_cma_region_nocache(mm, region);
	else if (region->type == ZYNPU_MEM_TYPE_RESERVED)
		zynpu_unmap_region_nocache(region->va);

	region->pa = 0;
	region->va = NULL;
	region->tot_bytes = 0;
	region->tot_free_bytes = 0;

	mutex_unlock(&region->lock);
	//mutex_destroy(&region->lock);

	return 0;
}

int zynpu_init_mm(struct zynpu_memory_manager *mm, struct device *dev, int version)
{
	if ((!mm) || (!dev))
		return -EINVAL;

	mm->sram_head = create_region_list_head();
	mm->sram_cnt = 0;
	mm->ddr_head = create_region_list_head();
	mm->ddr_cnt = 0;
	mm->sram_global = get_asid(mm, ZYNPU_CONFIG_SRAM_DATA_ASID);
	mm->dev = dev;
	mm->version = version;

	/* success */
	return 0;
}

void zynpu_deinit_mm(struct zynpu_memory_manager *mm)
{
	struct zynpu_mem_region *region = NULL;

	if (!mm)
	       return;

	if (mm->sram_head) {
		list_for_each_entry(region, &mm->sram_head->list, list) {
			zynpu_mm_deinit_region(mm, region);
		}
	}

	if (mm->ddr_head) {
		list_for_each_entry(region, &mm->ddr_head->list, list) {
			zynpu_mm_deinit_region(mm, region);
		}
	}
	memset(mm, 0, sizeof(struct zynpu_memory_manager));
}

int zynpu_mm_add_region(struct zynpu_memory_manager *mm, u64 base, u64 bytes,
	enum zynpu_mem_type type)
{
	int ret = 0;
	int region_id = 0;
	struct zynpu_mem_region *region = NULL;

	if ((!mm) || (!bytes))
		return -EINVAL;

	region = devm_kzalloc(mm->dev, sizeof(struct zynpu_mem_region), GFP_KERNEL);
	if (!region)
		return -ENOMEM;

	if (type == ZYNPU_MEM_TYPE_SRAM)
		region->va = zynpu_remap_region_nocache(mm, base, bytes);
	else if (type == ZYNPU_MEM_TYPE_CMA)
		region->va = zynpu_alloc_cma_region_nocache(mm, &base, bytes);
	else if (type == ZYNPU_MEM_TYPE_RESERVED)
		region->va = zynpu_remap_region_nocache(mm, base, bytes);

	if (!region->va)
		return -ENOMEM;

	region_id = mm->sram_cnt + mm->ddr_cnt;
	ret = zynpu_init_region(region_id, mm, base, bytes, type, region);
	if (ret)
		return ret;

	if (type == ZYNPU_MEM_TYPE_SRAM)
		ret = zynpu_update_mm_regions(mm, mm->sram_head, &mm->sram_cnt, region);
	else
		ret = zynpu_update_mm_regions(mm, mm->ddr_head, &mm->ddr_cnt, region);
	if (ret)
		return ret;

	return 0;
}

int zynpu_mm_alloc(struct zynpu_memory_manager *mm, struct buf_request *buf_req,
	struct zynpu_buffer *buf)
{
	int ret = 0;
	int asid = ZYNPU_ASE_ID_NONE;

	if ((!mm) || (!buf_req) || (!buf))
		return -EINVAL;

	if ((!buf_req->bytes) || (!buf_req->align_in_page))
		return -EINVAL;

	memset(buf, 0, sizeof(struct zynpu_buffer));
	asid = get_asid(mm, buf_req->data_type);

	if ((!mm->sram_cnt) && (!mm->ddr_cnt))
		return -ENOMEM;

	/**
	 * Try to alloc from SRAM first if the ASID is compatible; if failed then
	 * fall back to alloc from DDR.
	 */
	if (mm->sram_global & asid) {
		ret = zynpu_mm_scan_regions_alloc(mm, mm->sram_head, buf_req, buf);
		if (!ret)
			goto finish;
	}

	ret = zynpu_mm_scan_regions_alloc(mm, mm->ddr_head, buf_req, buf);
	if (ret) {
		buf_req->errcode = ZYNPU_ERRCODE_NO_MEMORY;
		dev_err(mm->dev, "[MM] buffer allocation failed for: bytes 0x%llx, page align %d\n",
			buf_req->bytes, buf_req->align_in_page);
	}

finish:
	return ret;
}

int zynpu_mm_free(struct zynpu_memory_manager *mm, struct buf_desc *buf)
{
	int ret = 0;
	struct zynpu_mem_region *region = NULL;

	if ((!mm) || (!buf))
		return -EINVAL;

	region = zynpu_mm_find_region(mm->sram_head, buf->pa, buf->bytes);
	if (!region) {
		region = zynpu_mm_find_region(mm->ddr_head, buf->pa, buf->bytes);
		if (!region) {
			dev_err(mm->dev, "[MM] buffer to free not exists in any region: \
				pa 0x%llx, bytes 0x%llx\n",
				buf->pa, buf->bytes);
			return -EINVAL;
		}
	}

	ret = zynpu_mm_free_in_region(region, buf);
	if (ret) {
		dev_err(mm->dev, "[MM] buffer to free not exists in target region: \
			pa 0x%llx, bytes 0x%llx\n",
			buf->pa, buf->bytes);
	}

	return ret;
}

int zynpu_mm_free_session_buffers(struct zynpu_memory_manager *mm,
	struct zynpu_session *session)
{
	int ret = 0;
	struct zynpu_buffer *buf = NULL;
	struct buf_desc desc;

	while (NULL != (buf = zynpu_get_session_sbuf_head(session))) {
		desc.pa = buf->pa;
		desc.bytes = buf->bytes;
		ret = zynpu_mm_free(mm, &desc);
		if (ret)
			goto finish;
		ret = zynpu_session_detach_buf(session, &desc);
		if (ret)
			goto finish;
	}

finish:
	return ret;
}