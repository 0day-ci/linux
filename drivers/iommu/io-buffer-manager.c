// SPDX-License-Identifier: GPL-2.0-only
/*
 * Manager which allocates and tracks bounce buffers and their IOVAs. Does
 * not actually manage the IOMMU mapping nor do the bounce copies.
 *
 * Copyright (C) 2021 Google, Inc.
 */

#include "io-buffer-manager.h"

#include <linux/slab.h>

struct io_buffer_node {
	struct rb_node node;
	struct io_bounce_buffer_info info;
	void *orig_buffer;
	int prot;
};

static void io_buffer_manager_free_pages(struct page **pages, int count)
{
	while (count--)
		__free_page(pages[count]);
	kfree(pages);
}

static struct page **io_buffer_manager_alloc_pages(int count, unsigned int nid)
{
	struct page **pages;
	unsigned int i;

	pages = kmalloc_array(count, sizeof(*pages), GFP_ATOMIC);
	if (!pages)
		return NULL;

	// The IOMMU can map highmem pages, but try to allocate non-highmem
	// pages first to make accessing the buffer cheaper.
	for (i = 0; i < count; i++) {
		pages[i] = alloc_pages_node(
			nid,
			GFP_ATOMIC | __GFP_ZERO | __GFP_NORETRY | __GFP_NOWARN,
			0);
		if (!pages[i]) {
			pages[i] = alloc_pages_node(
				nid, GFP_ATOMIC | __GFP_ZERO | __GFP_HIGHMEM,
				0);
			if (!pages[i]) {
				io_buffer_manager_free_pages(pages, i);
				return NULL;
			}
		}
	}

	return pages;
}

struct io_buffer_node *find_fallback_node(struct rb_root *root, dma_addr_t iova)
{
	struct rb_node *node = root->rb_node;

	while (node) {
		struct io_buffer_node *cur =
			container_of(node, struct io_buffer_node, node);

		if (iova < cur->info.iova)
			node = node->rb_left;
		else if (iova >= cur->info.iova + cur->info.size)
			node = node->rb_right;
		else
			return cur;
	}
	return NULL;
}

bool insert_fallback_node(struct rb_root *root, struct io_buffer_node *node)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;
	dma_addr_t node_end = node->info.iova + node->info.size;

	while (*new) {
		struct io_buffer_node *cur =
			container_of(*new, struct io_buffer_node, node);
		dma_addr_t cur_end = cur->info.iova + cur->info.size;

		parent = *new;
		if (node_end <= cur->info.iova)
			new = &((*new)->rb_left);
		else if (node->info.iova >= cur_end)
			new = &((*new)->rb_right);
		else {
			pr_crit("IOVA collision new=[%llx,%llx) old=[%llx,%llx)\n",
				node->info.iova, node_end, cur->info.iova,
				cur_end);
			return false;
		}
	}

	rb_link_node(&node->node, parent, new);
	rb_insert_color(&node->node, root);
	return true;
}

bool io_buffer_manager_alloc_buffer(struct io_buffer_manager *manager,
				    struct device *dev, void *orig_buffer,
				    size_t size, int prot, unsigned int nid,
				    struct io_bounce_buffer_info *info)
{
	struct iommu_domain *domain = iommu_get_dma_domain(dev);
	struct io_buffer_node *node;
	unsigned long flags;

	node = kzalloc(sizeof(*node), GFP_ATOMIC);
	if (!node)
		return false;

	size = PAGE_ALIGN(size);
	node->info.iova =
		__iommu_dma_alloc_iova(domain, size, dma_get_mask(dev), dev);
	if (!node->info.iova)
		goto free_node;

	node->info.bounce_buffer =
		io_buffer_manager_alloc_pages(size >> PAGE_SHIFT, nid);
	if (!node->info.bounce_buffer)
		goto free_iova;

	spin_lock_irqsave(&manager->fallback_lock, flags);
	if (!insert_fallback_node(&manager->fallback_buffers, node))
		goto fallback_lock_unlock;
	spin_unlock_irqrestore(&manager->fallback_lock, flags);

	node->orig_buffer = orig_buffer;
	node->prot = prot;
	node->info.size = size;

	*info = node->info;

	return true;

fallback_lock_unlock:
	spin_unlock_irqrestore(&manager->fallback_lock, flags);
free_iova:
	__iommu_dma_free_iova(domain->iova_cookie, node->info.iova, size, NULL);
free_node:
	kfree(node);
	return false;
}

bool io_buffer_manager_find_buffer(struct io_buffer_manager *manager,
				   dma_addr_t handle,
				   struct io_bounce_buffer_info *info,
				   void **orig_buffer, int *prot)
{
	struct io_buffer_node *node;
	unsigned long flags;

	spin_lock_irqsave(&manager->fallback_lock, flags);
	node = find_fallback_node(&manager->fallback_buffers, handle);
	spin_unlock_irqrestore(&manager->fallback_lock, flags);

	if (!node)
		return false;

	*info = node->info;
	*orig_buffer = node->orig_buffer;
	*prot = node->prot;
	return true;
}

bool io_buffer_manager_release_buffer(struct io_buffer_manager *manager,
				      struct iommu_domain *domain,
				      dma_addr_t handle, bool inited,
				      prerelease_cb cb, void *ctx)
{
	struct io_buffer_node *node;
	unsigned long flags;
	bool free_buffer;

	spin_lock_irqsave(&manager->fallback_lock, flags);
	node = find_fallback_node(&manager->fallback_buffers, handle);
	if (node)
		rb_erase(&node->node, &manager->fallback_buffers);
	spin_unlock_irqrestore(&manager->fallback_lock, flags);

	if (!node)
		return false;

	if (cb)
		cb(&node->info, node->prot, node->orig_buffer, ctx);

	if (inited)
		free_buffer = io_bounce_buffers_release_buffer_cb(
			manager, node->info.iova, node->info.size);
	else
		free_buffer = true;

	if (free_buffer) {
		io_buffer_manager_free_pages(node->info.bounce_buffer,
					     node->info.size >> PAGE_SHIFT);
		__iommu_dma_free_iova(domain->iova_cookie, node->info.iova,
				      node->info.size, NULL);
	} else {
		pr_warn("Bounce buffer release failed; leaking buffer\n");
	}

	kfree(node);
	return true;
}

int io_buffer_manager_init(struct io_buffer_manager *manager)
{
	manager->fallback_buffers = RB_ROOT;
	spin_lock_init(&manager->fallback_lock);

	return 0;
}
