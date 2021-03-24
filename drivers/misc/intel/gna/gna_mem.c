// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2017-2021 Intel Corporation

#include <linux/device.h>
#include <linux/mm.h>
#include <linux/mmap_lock.h>
#include <linux/pagemap.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#include <linux/slab.h>
#include <linux/swap.h>

#include <uapi/misc/intel/gna.h>

#include "gna_device.h"
#include "gna_driver.h"
#include "gna_mem.h"

static void gna_mmu_init(struct gna_private *gna_priv)
{
	struct gna_mmu_object *mmu;
	dma_addr_t pagetable_dma;
	u32 *pgdirn;
	int i;

	mmu = &gna_priv->mmu;

	pgdirn = mmu->hwdesc->mmu.pagedir_n;

	for (i = 0; i < mmu->num_pagetables; i++) {
		pagetable_dma = mmu->pagetables_dma[i];
		pgdirn[i] = pagetable_dma >> PAGE_SHIFT;
	}

	for (; i < GNA_PGDIRN_LEN; i++)
		pgdirn[i] = GNA_PGDIR_INVALID;
}

/* descriptor and page tables allocation */
int gna_mmu_alloc(struct gna_private *gna_priv)
{
	struct gna_mmu_object *mmu;
	struct pci_dev *pcidev;
	int desc_size;
	int i;

	pcidev = gna_priv->pdev;

	if (gna_priv->info.num_pagetables > GNA_PGDIRN_LEN) {
		dev_err(&pcidev->dev, "too large number of pagetables requested\n");
		return -EINVAL;
	}

	mmu = &gna_priv->mmu;

	desc_size = round_up(gna_priv->info.desc_info.desc_size, PAGE_SIZE);

	mmu->hwdesc = dma_alloc_coherent(&pcidev->dev, desc_size, &mmu->hwdesc_dma,
					 GFP_KERNEL);
	if (!mmu->hwdesc)
		goto end;

	mmu->num_pagetables = gna_priv->info.num_pagetables;

	mmu->pagetables_dma = kmalloc_array(mmu->num_pagetables, sizeof(*mmu->pagetables_dma),
					    GFP_KERNEL);
	if (!mmu->pagetables_dma)
		goto err_free_descriptor;

	mmu->pagetables = kmalloc_array(mmu->num_pagetables, sizeof(*mmu->pagetables), GFP_KERNEL);

	if (!mmu->pagetables)
		goto err_free_pagetables_dma;

	for (i = 0; i < mmu->num_pagetables; i++) {
		mmu->pagetables[i] = dma_alloc_coherent(&pcidev->dev, PAGE_SIZE,
							&mmu->pagetables_dma[i], GFP_KERNEL);
		if (!mmu->pagetables[i])
			goto err_free_mmu;
	}

	gna_mmu_init(gna_priv);

	return 0;

err_free_mmu:
	while (i--) {
		pci_free_consistent(pcidev, PAGE_SIZE, mmu->pagetables[i],
				    mmu->pagetables_dma[i]);
		mmu->pagetables[i] = NULL;
		mmu->pagetables_dma[i] = 0;
	}

	kfree(mmu->pagetables);
	mmu->pagetables = NULL;
	mmu->num_pagetables = 0;

err_free_pagetables_dma:
	kfree(mmu->pagetables_dma);
	mmu->pagetables_dma = NULL;

err_free_descriptor:
	pci_free_consistent(pcidev, desc_size, mmu->hwdesc, mmu->hwdesc_dma);
	mmu->hwdesc = NULL;
	mmu->hwdesc_dma = 0;

end:
	return -ENOMEM;
}

void gna_mmu_free(struct gna_private *gna_priv)
{
	struct gna_mmu_object *mmu;
	int desc_size;
	int i;

	mmu = &gna_priv->mmu;
	mutex_lock(&gna_priv->mmu_lock);

	for (i = 0; i < mmu->num_pagetables; i++) {
		pci_free_consistent(gna_priv->pdev, PAGE_SIZE, mmu->pagetables[i],
				    mmu->pagetables_dma[i]);
		mmu->pagetables[i] = NULL;
		mmu->pagetables_dma[i] = 0;
	}

	kfree(mmu->pagetables);
	mmu->pagetables = NULL;

	kfree(mmu->pagetables_dma);
	mmu->pagetables_dma = NULL;

	desc_size = round_up(gna_priv->info.desc_info.desc_size, PAGE_SIZE);
	pci_free_consistent(gna_priv->pdev, desc_size, mmu->hwdesc, mmu->hwdesc_dma);
	mmu->hwdesc = NULL;
	mmu->hwdesc_dma = 0;

	mutex_unlock(&gna_priv->mmu_lock);
}

void gna_mmu_add(struct gna_private *gna_priv, struct gna_memory_object *mo)
{
	struct gna_mmu_object *mmu;
	struct scatterlist *sgl;
	dma_addr_t sg_page;
	int sg_page_len;
	u32 *pagetable;
	u32 mmu_page;
	int sg_pages;
	int i;
	int j;

	mmu = &gna_priv->mmu;
	mutex_lock(&gna_priv->mmu_lock);

	j = mmu->filled_pages;
	sgl = mo->sgt->sgl;
	if (!sgl) {
		dev_warn(&gna_priv->pdev->dev, "empty scatter list in memory object\n");
		goto warn_empty_sgl;
	}
	sg_page = sg_dma_address(sgl);
	sg_page_len = round_up(sg_dma_len(sgl), PAGE_SIZE) >> PAGE_SHIFT;
	sg_pages = 0;

	for (i = mmu->filled_pts; i < mmu->num_pagetables; i++) {
		if (!sgl)
			break;

		pagetable = mmu->pagetables[i];

		for (j = mmu->filled_pages; j < GNA_PT_LENGTH; j++) {
			mmu_page = sg_page >> PAGE_SHIFT;
			pagetable[j] = mmu_page;

			mmu->filled_pages++;
			sg_page += PAGE_SIZE;
			sg_pages++;
			if (sg_pages == sg_page_len) {
				sgl = sg_next(sgl);
				if (!sgl)
					break;

				sg_page = sg_dma_address(sgl);
				sg_page_len =
					round_up(sg_dma_len(sgl), PAGE_SIZE)
						>> PAGE_SHIFT;
				sg_pages = 0;
			}
		}

		if (j == GNA_PT_LENGTH) {
			mmu->filled_pages = 0;
			mmu->filled_pts++;
		}
	}

	mmu->hwdesc->mmu.vamaxaddr =
		(mmu->filled_pts * PAGE_SIZE * GNA_PGDIR_ENTRIES) +
		(mmu->filled_pages * PAGE_SIZE) - 1;
	dev_dbg(&gna_priv->pdev->dev, "vamaxaddr set to %u\n", mmu->hwdesc->mmu.vamaxaddr);

warn_empty_sgl:
	mutex_unlock(&gna_priv->mmu_lock);
}

void gna_mmu_clear(struct gna_private *gna_priv)
{
	struct gna_mmu_object *mmu;
	int i;

	mmu = &gna_priv->mmu;
	mutex_lock(&gna_priv->mmu_lock);

	for (i = 0; i < mmu->filled_pts; i++)
		memset(mmu->pagetables[i], 0, PAGE_SIZE);

	if (mmu->filled_pages > 0)
		memset(mmu->pagetables[mmu->filled_pts], 0, mmu->filled_pages * GNA_PT_ENTRY_SIZE);

	mmu->filled_pts = 0;
	mmu->filled_pages = 0;
	mmu->hwdesc->mmu.vamaxaddr = 0;

	mutex_unlock(&gna_priv->mmu_lock);
}

int gna_buffer_get_size(u64 offset, u64 size)
{
	u64 page_offset;

	page_offset = offset & ~PAGE_MASK;
	return round_up(page_offset + size, PAGE_SIZE);
}

/* must be called with gna_memory_object page_lock held */
static int gna_get_pages(struct gna_memory_object *mo, u64 offset, u64 size)
{
	struct gna_private *gna_priv;
	u64 effective_address;
	struct mm_struct *mm;
	struct sg_table *sgt;
	struct page **pages;
	int effective_size;
	int num_pinned;
	int num_pages;
	int skip_size;
	int ents;
	int ret;

	ret = 0;
	gna_priv = mo->gna_priv;

	if (mo->pages) {
		dev_warn(&gna_priv->pdev->dev, "pages are already pinned\n");
		return -EFAULT;
	}

	/* using vmalloc because num_pages can be large */
	skip_size = round_down(offset, PAGE_SIZE);
	effective_address = mo->user_address + skip_size;
	dev_dbg(&gna_priv->pdev->dev, "user address %llx\n", mo->user_address);
	dev_dbg(&gna_priv->pdev->dev, "effective user address %llx\n", effective_address);

	effective_size = gna_buffer_get_size(offset, size);

	num_pages = effective_size >> PAGE_SHIFT;
	dev_dbg(&gna_priv->pdev->dev, "allocating %d pages\n", num_pages);
	pages = kvmalloc_array(num_pages, sizeof(struct page *), GFP_KERNEL);
	if (!pages) {
		ret = -ENOMEM;
		goto err_exit;
	}

	get_task_struct(mo->task);
	mm = get_task_mm(mo->task);
	if (!mm) {
		ret = -ENOENT;
		goto err_put_task;
	}
	mmap_read_lock(mm);
	num_pinned = get_user_pages_remote(mm, effective_address, num_pages,
					   FOLL_WRITE, pages, NULL, NULL);
	mmap_read_unlock(mm);
	mmput(mm);

	if (num_pinned <= 0) {
		ret = num_pinned;
		dev_err(&gna_priv->pdev->dev, "function get_user_pages_remote() failed\n");
		goto err_free_pages;
	}
	if (num_pinned < num_pages) {
		ret = -EFAULT;
		dev_err(&gna_priv->pdev->dev,
			"get_user_pages_remote() pinned fewer pages number than requested\n");
		goto err_free_pages;
	}

	sgt = kmalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt) {
		ret = -ENOMEM;
		goto err_put_pages;
	}

	ret = sg_alloc_table_from_pages(sgt, pages, num_pinned, 0, mo->memory_size, GFP_KERNEL);
	if (ret) {
		dev_err(&gna_priv->pdev->dev, "could not alloc scatter list\n");
		goto err_free_sgt;
	}

	if (IS_ERR(sgt->sgl)) {
		dev_err(&gna_priv->pdev->dev, "sgl allocation failed\n");
		ret = PTR_ERR(sgt->sgl);
		goto err_free_sgt;
	}

	ents = pci_map_sg(gna_priv->pdev, sgt->sgl, sgt->nents, PCI_DMA_BIDIRECTIONAL);
	if (ents <= 0) {
		dev_err(&gna_priv->pdev->dev, "could not map scatter gather list\n");
		ret = -EIO;
		goto err_free_sgl;
	}

	mo->sgt = sgt;
	mo->pages = pages;
	mo->num_pinned = num_pinned;

	return 0;

err_free_sgl:
	sg_free_table(sgt);

err_free_sgt:
	kfree(sgt);

err_put_pages:
	release_pages(pages, num_pinned);

err_free_pages:
	kvfree(pages);

err_put_task:
	put_task_struct(mo->task);

err_exit:
	return ret;
}

/* must be called with gna_memory_object page_lock held */
static void gna_put_pages(struct gna_memory_object *mo)
{
	struct gna_private *gna_priv;
	struct sg_table *sgt;

	gna_priv = mo->gna_priv;

	if (!mo->pages) {
		dev_warn(&gna_priv->pdev->dev, "memory object has no pages %llu\n", mo->memory_id);
		return;
	}

	sgt = mo->sgt;

	pci_unmap_sg(gna_priv->pdev, sgt->sgl, sgt->nents, PCI_DMA_BIDIRECTIONAL);
	sg_free_table(sgt);
	kfree(sgt);
	mo->sgt = NULL;

	release_pages(mo->pages, mo->num_pinned);
	kvfree(mo->pages);
	mo->pages = NULL;
	mo->num_pinned = 0;

	put_task_struct(mo->task);
}

void gna_memory_free(struct gna_private *gna_priv, struct gna_memory_object *mo)
{
	mutex_lock(&gna_priv->memidr_lock);
	idr_remove(&gna_priv->memory_idr, mo->memory_id);
	mutex_unlock(&gna_priv->memidr_lock);

	cancel_work_sync(&mo->work);
	kfree(mo);
}

static void gna_memory_release(struct work_struct *work)
{
	struct gna_memory_object *mo;

	mo = container_of(work, struct gna_memory_object, work);

	mo->user_ptr = NULL;

	wake_up_interruptible(&mo->waitq);
}

static const struct gna_memory_operations memory_ops = {
	.get_pages = gna_get_pages,
	.put_pages = gna_put_pages,
};

int gna_map_memory(struct gna_file_private *file_priv, union gna_memory_map *gna_mem)
{
	struct gna_memory_object *mo;
	struct gna_private *gna_priv;
	int memory_id;
	int ret;

	ret = 0;

	gna_priv = file_priv->gna_priv;

	if (gna_mem->in.address & ~PAGE_MASK) {
		dev_err(&gna_priv->pdev->dev, "user pointer not page aligned\n");
		return -EINVAL;
	}

	if (!gna_mem->in.size) {
		dev_err(&gna_priv->pdev->dev, "invalid user memory size\n");
		return -EINVAL;
	}

	if (!access_ok(u64_to_user_ptr(gna_mem->in.address), gna_mem->in.size)) {
		dev_err(&gna_priv->pdev->dev, "invalid user pointer\n");
		return -EINVAL;
	}

	mo = kzalloc(sizeof(*mo), GFP_KERNEL);
	if (!mo)
		return -ENOMEM;

	mo->fd = file_priv->fd;
	mo->gna_priv = gna_priv;
	mo->ops = &memory_ops;
	mo->user_address = gna_mem->in.address;
	mo->memory_size = gna_mem->in.size;
	mo->user_ptr = u64_to_user_ptr(gna_mem->in.address);
	mo->num_pages = round_up(gna_mem->in.size, PAGE_SIZE) >> PAGE_SHIFT;
	mo->task = current;
	INIT_WORK(&mo->work, gna_memory_release);
	init_waitqueue_head(&mo->waitq);
	mutex_init(&mo->page_lock);

	mutex_lock(&gna_priv->memidr_lock);
	memory_id = idr_alloc(&gna_priv->memory_idr, mo, 1, 0, GFP_KERNEL);
	mutex_unlock(&gna_priv->memidr_lock);

	if (memory_id < 0) {
		dev_err(&gna_priv->pdev->dev, "idr allocation for memory failed\n");
		ret = -EFAULT;
		goto err_free_mo;
	}

	mo->memory_id = (u64)memory_id;

	mutex_lock(&file_priv->memlist_lock);
	list_add_tail(&mo->file_mem_list, &file_priv->memory_list);
	mutex_unlock(&file_priv->memlist_lock);

	gna_mem->out.memory_id = mo->memory_id;

	return 0;

err_free_mo:
	kfree(mo);
	return ret;
}
