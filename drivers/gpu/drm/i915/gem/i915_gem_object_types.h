/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2016 Intel Corporation
 */

#ifndef __I915_GEM_OBJECT_TYPES_H__
#define __I915_GEM_OBJECT_TYPES_H__

#include <linux/mmu_notifier.h>

#include <drm/drm_gem.h>
#include <drm/ttm/ttm_bo_api.h>
#include <uapi/drm/i915_drm.h>

#include "i915_active.h"
#include "i915_selftest.h"

struct drm_i915_gem_object;
struct intel_fronbuffer;
struct intel_memory_region;

/*
 * struct i915_lut_handle tracks the fast lookups from handle to vma used
 * for execbuf. Although we use a radixtree for that mapping, in order to
 * remove them as the object or context is closed, we need a secondary list
 * and a translation entry (i915_lut_handle).
 */
struct i915_lut_handle {
	struct list_head obj_link;
	struct i915_gem_context *ctx;
	u32 handle;
};

struct drm_i915_gem_object_ops {
	unsigned int flags;
#define I915_GEM_OBJECT_IS_SHRINKABLE	BIT(1)
#define I915_GEM_OBJECT_IS_PROXY	BIT(2)
#define I915_GEM_OBJECT_NO_MMAP		BIT(3)

	/* Interface between the GEM object and its backing storage.
	 * get_pages() is called once prior to the use of the associated set
	 * of pages before to binding them into the GTT, and put_pages() is
	 * called after we no longer need them. As we expect there to be
	 * associated cost with migrating pages between the backing storage
	 * and making them available for the GPU (e.g. clflush), we may hold
	 * onto the pages after they are no longer referenced by the GPU
	 * in case they may be used again shortly (for example migrating the
	 * pages to a different memory domain within the GTT). put_pages()
	 * will therefore most likely be called when the object itself is
	 * being released or under memory pressure (where we attempt to
	 * reap pages for the shrinker).
	 */
	int (*get_pages)(struct drm_i915_gem_object *obj);
	void (*put_pages)(struct drm_i915_gem_object *obj,
			  struct sg_table *pages);
	void (*truncate)(struct drm_i915_gem_object *obj);
	void (*writeback)(struct drm_i915_gem_object *obj);

	int (*pread)(struct drm_i915_gem_object *obj,
		     const struct drm_i915_gem_pread *arg);
	int (*pwrite)(struct drm_i915_gem_object *obj,
		      const struct drm_i915_gem_pwrite *arg);
	u64 (*mmap_offset)(struct drm_i915_gem_object *obj);

	int (*dmabuf_export)(struct drm_i915_gem_object *obj);

	/**
	 * adjust_lru - notify that the madvise value was updated
	 * @obj: The gem object
	 *
	 * The madvise value may have been updated, or object was recently
	 * referenced so act accordingly (Perhaps changing an LRU list etc).
	 */
	void (*adjust_lru)(struct drm_i915_gem_object *obj);

	/**
	 * delayed_free - Override the default delayed free implementation
	 */
	void (*delayed_free)(struct drm_i915_gem_object *obj);

	/**
	 * migrate - Migrate object to a different region either for
	 * pinning or for as long as the object lock is held.
	 */
	int (*migrate)(struct drm_i915_gem_object *obj,
		       struct intel_memory_region *mr);

	void (*release)(struct drm_i915_gem_object *obj);

	const struct vm_operations_struct *mmap_ops;
	const char *name; /* friendly name for debug, e.g. lockdep classes */
};

/**
 * enum i915_cache_level - The supported GTT caching values for system memory
 * pages.
 *
 * These translate to some special GTT PTE bits when binding pages into some
 * address space. It also determines whether an object, or rather its pages are
 * coherent with the GPU, when also reading or writing through the CPU cache
 * with those pages.
 *
 * Userspace can also control this through struct drm_i915_gem_caching.
 */
enum i915_cache_level {
	/**
	 * @I915_CACHE_NONE:
	 *
	 * Not coherent with the CPU cache. If the cache is dirty and we need
	 * the underlying pages to be coherent with some later GPU access then
	 * we need to manually flush the pages.
	 *
	 * Note that on shared-LLC platforms reads through the CPU cache are
	 * still coherent even with this setting. See also
	 * I915_BO_CACHE_COHERENT_FOR_READ for more details.
	 */
	I915_CACHE_NONE = 0,
	/**
	 * @I915_CACHE_LLC:
	 *
	 * Coherent with the CPU cache. If the cache is dirty, then the GPU will
	 * ensure that access remains coherent, when both reading and writing
	 * through the CPU cache.
	 *
	 * Applies to both platforms with shared-LLC(HAS_LLC), and snooping
	 * based platforms(HAS_SNOOP).
	 */
	I915_CACHE_LLC,
	/**
	 * @I915_CACHE_L3_LLC:
	 *
	 * gen7+, L3 sits between the domain specifc caches, eg sampler/render
	 * caches, and the large Last-Level-Cache. LLC is coherent with the CPU,
	 * but L3 is only visible to the GPU.
	 */
	I915_CACHE_L3_LLC,
	/**
	 * @I915_CACHE_WT:
	 *
	 * hsw:gt3e Write-through for scanout buffers.
	 */
	I915_CACHE_WT,
};

enum i915_map_type {
	I915_MAP_WB = 0,
	I915_MAP_WC,
#define I915_MAP_OVERRIDE BIT(31)
	I915_MAP_FORCE_WB = I915_MAP_WB | I915_MAP_OVERRIDE,
	I915_MAP_FORCE_WC = I915_MAP_WC | I915_MAP_OVERRIDE,
};

enum i915_mmap_type {
	I915_MMAP_TYPE_GTT = 0,
	I915_MMAP_TYPE_WC,
	I915_MMAP_TYPE_WB,
	I915_MMAP_TYPE_UC,
};

struct i915_mmap_offset {
	struct drm_vma_offset_node vma_node;
	struct drm_i915_gem_object *obj;
	enum i915_mmap_type mmap_type;

	struct rb_node offset;
};

struct i915_gem_object_page_iter {
	struct scatterlist *sg_pos;
	unsigned int sg_idx; /* in pages, but 32bit eek! */

	struct radix_tree_root radix;
	struct mutex lock; /* protects this cache */
};

/**
 * struct i915_page_sizes - Track the various pieces we need to
 * both track and construct huge GTT entries, when binding the
 * object.
 */
struct i915_page_sizes {
	/**
	 * @phys:
	 *
	 * The sg mask of the pages sg_table. i.e the
	 * mask of of the lengths for each sg entry.
	 */
	unsigned int phys;

	/**
	 * @sg:
	 *
	 * The gtt page sizes we are allowed to use given
	 * the sg mask and the supported page sizes. This will
	 * express the smallest unit we can use for the whole
	 * object, as well as the larger sizes we may be able to
	 * use opportunistically.
	 */
	unsigned int sg;

	/**
	 * @gtt:
	 *
	 * The actual gtt page size usage. Since we can
	 * have multiple vma associated with this object we need
	 * to prevent any trampling of state, hence a copy of
	 * this struct also lives in each vma, therefore the gtt
	 * value here should only be read/write through the vma.
	 */
	unsigned int gtt;
};

/**
 * struct drm_i915_gem_object - Our core GEM object which extends the base
 * struct drm_gem_object behaviour.
 */
struct drm_i915_gem_object {
	union {
		/** @base: The base DRM GEM object. */
		struct drm_gem_object base;

		/**
		 * @__do_not_access:
		 *
		 * The base TTM object, if we are using the TTM backend. Note
		 * that this also embeds its own DRM_GEM base object.
		 *
		 * We might have reason to revisit the below since it wastes a
		 * lot of space for non-ttm gem objects.  In any case, always
		 * use the accessors for the ttm_buffer_object when accessing
		 * it.
		 */
		struct ttm_buffer_object __do_not_access;
	};

	/**
	 * @ops: The struct drm_i915_gem_object_ops interface implemented by the
	 * object instance.
	 */
	const struct drm_i915_gem_object_ops *ops;

	/** @vma: Track all the struct i915_vma instances for this object. */
	struct {
		/** @vma.lock: protect the list/tree of vmas */
		spinlock_t lock;

		/**
		 * @vma.list: List of VMAs backed by this object
		 *
		 * The VMA on this list are ordered by type, all GGTT vma are
		 * placed at the head and all ppGTT vma are placed at the tail.
		 * The different types of GGTT vma are unordered between
		 * themselves, use the @vma.tree (which has a defined order
		 * between all VMA) to quickly find an exact match.
		 */
		struct list_head list;

		/**
		 * @vma.tree: Ordered tree of VMAs backed by this object
		 *
		 * All VMA created for this object are placed in the @vma.tree
		 * for fast retrieval via a binary search in
		 * i915_vma_instance(). They are also added to @vma.list for
		 * easy iteration.
		 */
		struct rb_root tree;
	} vma;

	/**
	 * @lut_list: List of vma lookup entries in use for this object.
	 *
	 * If this object is closed, we need to remove all of its VMA from
	 * the fast lookup index in associated contexts; @lut_list provides
	 * this translation from object to context->handles_vma.
	 */
	struct list_head lut_list;

	/** @lut_lock: Guards the lut_list */
	spinlock_t lut_lock;

	/**
	 * @obj_link: Link into @i915_gem_ww_ctx.obj_list
	 *
	 * When we lock this object through i915_gem_object_lock() with a
	 * context, we add it to the list to ensure we can unlock everything
	 * when i915_gem_ww_ctx_backoff() or i915_gem_ww_ctx_fini() are called.
	 */
	struct list_head obj_link;

	/** @shares_resv_from: The object shares the resv from this vm. */
	struct i915_address_space *shares_resv_from;

	union {
		/** @rcu: Embedded rcu_head */
		struct rcu_head rcu;

		/**
		 * @freed:
		 *
		 * When objects need to be destroyed we batch them together into
		 * an llist, for a separate worker thread to then pick up and
		 * process.
		 */
		struct llist_node freed;
	};

	/**
	 * @userfault_count: Whether the object is currently in the GGTT mmap.
	 */
	unsigned int userfault_count;
	/**
	 * @userfault_link:
	 *
	 * We need to maintain the list of all objects which might have been
	 * faulted into the GGTT mappable aperture, for easy revocation later.
	 */
	struct list_head userfault_link;

	/**
	 * @mmo: Track the mmap_offset nodes for this object.
	 */
	struct {
		/** @lock: Protects access to mmo offsets. */
		spinlock_t lock;

		/** @offsets: The rb-tree of mmo offsets. */
		struct rb_root offsets;
	} mmo;

	/** @st_link: SELFTEST ONLY */
	struct list_head st_link;

	/**
	 * @flags: The object flags
	 *
	 * The currently supported I915_BO_ALLOC_FLAGS. Note that these can only
	 * be set at object creation, after which they should be considered
	 * immutable. Also some of these largely depend on whether the backend
	 * supports it.
	 *
	 * I915_BO_ALLOC_CONTIGUOUS:
	 *
	 * Allocate the physical pages for the object as one contiguous block or
	 * page. Currently only supported for device local-memory.
	 *
	 * I915_BO_ALLOC_VOLATILE:
	 *
	 * Volatile here refers to the volatility of the allocated pages when
	 * unpinned. This effectively just sets the @madv hint to
	 * I915_MADV_DONTNEED while the pages are pinned/allocated. This way as
	 * soon as the pages become unpinned the shrinker is free to discard the
	 * pages if needed.  This is only intended for kernel internal objects
	 * where they are often short lived anyway, and don't require any kind
	 * of persistence.
	 *
	 * I915_BO_ALLOC_CPU_CLEAR:
	 *
	 * After allocating the pages, zero them using a simple memset. This is
	 * very specialised and is only intended for kernel internal objects
	 * where we are unable(too early during probe) or prefer not to use a
	 * normal accelerated blitter clear.
	 *
	 * I915_BO_ALLOC_USER:
	 *
	 * All normal userspace objects are allocated with this flag. This is
	 * useful where the kernel needs to know if the object is merely kernel
	 * internal, or was created by userspace, where slightly different rules
	 * might be needed.
	 *
	 * Other special flags, note that these might be mutable:
	 *
	 * I915_BO_READONLY:
	 *
	 * Should this object be marked as read-only. This applies to both CPU
	 * and GPU access, when dealing with userspace objects, at least where
	 * it can be enforced. From a userspace perspective this only exposed
	 * for userptr objects.
	 *
	 * When dealing with kernel internal objects this *only* applies to GPU
	 * access, usually where we need to prevent userspace access to some
	 * security critical object, which might need to share the user visible
	 * ppGTT address space.
	 *
	 * Note that for GPU access the HW needs to actually support the
	 * read-only bit in the ppGTT PTE field. On some newer hardware this
	 * support is completely busted. So whether this is actually supported
	 * depends on the vm. Currently the caller is expected to check this
	 * first before marking the object as readonly, if they really do need
	 * it, since it just gets silently ignored when setting up the PTEs,
	 * during i915_vma_pin().
	 *
	 * FIXME: Note that this might be a slight wart in the api. Once idea
	 * could be to move this to I915_BO_ALLOC_FLAGS, that way it becomes
	 * immutable, and then we don't have to worry about unbinding and
	 * rebinding objects on the fly if the object suddenly becomes readonly.
	 * The final piece is to make i915_vma_pin() fall over if the vm doesn't
	 * have read-only support, when the object is marked as readonly. The
	 * callers should then be updated to account for this.
	 *
	 * I915_TILING_QUIRK_BIT:
	 *
	 * Tiled objects with unknown swizzling need special care. For example,
	 * we are not allowed to swap the pages out if this is set, otherwise we
	 * may see corruption.
	 */
	unsigned long flags;
#define I915_BO_ALLOC_CONTIGUOUS BIT(0)
#define I915_BO_ALLOC_VOLATILE   BIT(1)
#define I915_BO_ALLOC_CPU_CLEAR  BIT(2)
#define I915_BO_ALLOC_USER       BIT(3)
#define I915_BO_ALLOC_FLAGS (I915_BO_ALLOC_CONTIGUOUS | \
			     I915_BO_ALLOC_VOLATILE | \
			     I915_BO_ALLOC_CPU_CLEAR | \
			     I915_BO_ALLOC_USER)
#define I915_BO_READONLY         BIT(4)
#define I915_TILING_QUIRK_BIT    5 /* unknown swizzling; do not release! */

	/**
	 * @mem_flags: Mutable placement-related flags
	 *
	 * These are flags that indicate specifics of the memory region
	 * the object is currently in. As such they are only stable
	 * either under the object lock or if the object is pinned.
	 *
	 * Possible values:
	 *
	 * I915_BO_FLAG_STRUCT_PAGE:
	 *
	 * Object backed by struct pages, aka system memory
	 *
	 * I915_BO_FLAG_IOMEM:
	 *
	 * Object backed by device memory, aka local memory
	 */
	unsigned int mem_flags;
#define I915_BO_FLAG_STRUCT_PAGE BIT(0)
#define I915_BO_FLAG_IOMEM       BIT(1)

	/**
	 * @cache_level: The desired GTT caching level.
	 *
	 * See enum i915_cache_level for possible values, along with what
	 * each does.
	 */
	unsigned int cache_level:3;

	/**
	 * @cache_coherent:
	 *
	 * Track whether the pages are coherent with the GPU if reading or
	 * writing through the CPU cache.
	 *
	 * This largely depends on the @cache_level, for example if the object
	 * is marked as I915_CACHE_LLC, then GPU access is coherent for both
	 * reads and writes through the CPU cache.
	 *
	 * Note that on platforms with shared-LLC support(HAS_LLC) reads through
	 * the CPU cache are always coherent, regardless of the @cache_level. On
	 * snooping based platforms this is not the case, unless the full
	 * I915_CACHE_LLC or similar setting is used.
	 *
	 * As a result of this we need to track coherency separately for reads
	 * and writes, in order to avoid superfluous flushing on shared-LLC
	 * platforms, for reads.
	 *
	 * I915_BO_CACHE_COHERENT_FOR_READ:
	 *
	 * When reading through the CPU cache, the GPU is still coherent. Note
	 * that no data has actually been modified here, so it might seem
	 * strange that we care about this.
	 *
	 * As an example, if some object is mapped on the CPU with write-back
	 * caching, and we read some page, then the cache likely now contains
	 * the data from that read. At this point the cache and main memory
	 * match up, so all good. But next the GPU needs to write some data to
	 * that same page. Now if the @cache_level is I915_CACHE_NONE and the
	 * the platform doesn't have the shared-LLC, then the GPU will
	 * effectively skip invalidating the cache(or however that works
	 * internally) when writing the new value.  This is really bad since the
	 * GPU has just written some new data to main memory, but the CPU cache
	 * is still valid and now contains stale data. As a result the next time
	 * we do a cached read with the CPU, we are rewarded with stale data.
	 * Likewise if the cache is later flushed, we might be rewarded with
	 * overwriting main memory with stale data.
	 *
	 * I915_BO_CACHE_COHERENT_FOR_WRITE:
	 *
	 * When writing through the CPU cache, the GPU is still coherent. Note
	 * that this also implies I915_BO_CACHE_COHERENT_FOR_READ.
	 *
	 * This is never set when I915_CACHE_NONE is used for @cache_level,
	 * where instead we have to manually flush the caches after writing
	 * through the CPU cache. For other cache levels this should be set and
	 * the object is therefore considered coherent for both reads and writes
	 * through the CPU cache.
	 */
#define I915_BO_CACHE_COHERENT_FOR_READ BIT(0)
#define I915_BO_CACHE_COHERENT_FOR_WRITE BIT(1)
	unsigned int cache_coherent:2;

	/**
	 * @cache_dirty:
	 *
	 * Track if the cache might be dirty for the @pages i.e it has yet to be
	 * written back to main memory. As a result reading directly from main
	 * memory might yield stale data.
	 *
	 * This also ties into whether the kernel is tracking the object as
	 * coherent with the GPU, as per @cache_coherent, as it determines if
	 * flushing might be needed at various points.
	 *
	 * Another part of @cache_dirty is managing flushing when first
	 * acquiring the pages for system memory, at this point the pages are
	 * considered foreign, so the default assumption is that the cache is
	 * dirty, for example the page zeroing done my the kernel might leave
	 * writes though the CPU cache, or swapping-in, while the actual data in
	 * main memory is potentially stale.  Note that this is a potential
	 * security issue when dealing with userspace objects and zeroing. Now,
	 * whether we actually need apply the big sledgehammer of flushing all
	 * the pages on acquire depends on if @cache_coherent is marked as
	 * I915_BO_CACHE_COHERENT_FOR_WRITE, i.e that the GPU will be coherent
	 * for both reads and writes though the CPU cache. So pretty much this
	 * should only be needed for I915_CACHE_NONE objects.
	 *
	 * Update: Some bonkers hardware decided to add the 'Bypass LLC' MOCS
	 * entry, which defeats our @cache_coherent tracking, since userspace
	 * can freely bypass the CPU cache when touching the pages with the GPU,
	 * where the kernel is completely unaware. On such platform we need
	 * apply the sledgehammer-on-acquire regardless of the @cache_coherent.
	 */
	unsigned int cache_dirty:1;

	/**
	 * @read_domains: Read memory domains.
	 *
	 * These monitor which caches contain read/write data related to the
	 * object. When transitioning from one set of domains to another,
	 * the driver is called to ensure that caches are suitably flushed and
	 * invalidated.
	 */
	u16 read_domains;

	/**
	 * @write_domain: Corresponding unique write memory domain.
	 */
	u16 write_domain;

	/**
	 * @frontbuffer:
	 *
	 * Frontbuffer tracking bits, or NULL if this is just a normal object.
	 */
	struct intel_frontbuffer __rcu *frontbuffer;

	/**
	 * @tiling_and_stride:
	 *
	 * Current tiling stride for the object, if it's tiled.
	 */
	unsigned int tiling_and_stride;
#define FENCE_MINIMUM_STRIDE 128 /* See i915_tiling_ok() */
#define TILING_MASK (FENCE_MINIMUM_STRIDE - 1)
#define STRIDE_MASK (~TILING_MASK)

	/** @mm: Manage all the state related to the backing storage */
	struct {
		/**
		 * @pages_pin_count:
		 *
		 * Protects the @pages and their use. Do not use directly, but
		 * instead go through the i915_gem_object_{pin, unpin}_pages()
		 * interface.
		 *
		 * When the @pages_pin_count reaches zero the pages might be
		 * discared when under memory pressure, if the @madv is also
		 * I915_MADV_DONTNEED.
		 *
		 * When the final ref for the object is dropped, the object
		 * destruction code will also zero the @pages_pin_count, and
		 * free the @pages and related state.
		 */
		atomic_t pages_pin_count;

		/**
		 * @shrink_pin:
		 *
		 * While @shrink_pin is non-zero, the object is not visible to
		 * the shrinker. Usually when the kernel knows the object can't
		 * be swapped out or discarded, we try to hide it from the
		 * shrinker so that it doesn't needlessly waste effort on such
		 * objects.
		 */
		atomic_t shrink_pin;

		/**
		 * @placements:
		 *
		 * Priority list of potential placements for this object.
		 */
		struct intel_memory_region **placements;

		/**
		 * @n_placements: Number of elements in @placements.
		 */
		int n_placements;

		/**
		 * @region: Memory region for this object.
		 */
		struct intel_memory_region *region;

		/**
		 * @res:
		 *
		 * Memory manager resource allocated for this object. Only
		 * needed for the mock region.
		 */
		struct ttm_resource *res;

		/**
		 * @region_link:
		 *
		 * Element within memory_region->objects or region->purgeable
		 * if the object is marked as DONTNEED. Access is protected by
		 * region->obj_lock.
		 */
		struct list_head region_link;

		/**
		 * @pages:
		 *
		 * Only valid while the @pages_pin_count is not zero.
		 *
		 * The cached struct sg_table for the backing pages, or NULL if
		 * the pages have yet to be allocated. We use this when mapping
		 * the object(or rather the struct i915_vma) through the GTT,
		 * effectively each GTT PTE is programmed using this table.
		 *
		 * If we are using an IOMMU then this will contain the
		 * respective DMA addresses for the physical pages, when dealing
		 * with system memory.
		 *
		 * We also like to abuse this as a general container for device
		 * addresses, like for device local memory and stolen memory.
		 */
		struct sg_table *pages;

		/*
		 * @mapping:
		 *
		 * Only valid while the @pages_pin_count is not zero.
		 *
		 * The cached CPU virtual address for the @pages, or NULL if
		 * there is no current mapping.
		 *
		 * The caching type is encoded in the unused lower bits of the
		 * address, so this should not be directly accessed. Rather the
		 * i915_gem_object_pin_map() should be used to obtain the
		 * address, which also ensures the pages are correctly pinned
		 * during CPU access of the virtual address.
		 * i915_gem_object_unpin_map() should be called when done.
		 */
		void *mapping;

		/** @page_sizes: Track the GTT page size related bits */
		struct i915_page_sizes page_sizes;

		/**
		 * @page_mask: SELFTEST ONLY
		 */
		unsigned int page_mask;

		/**
		 * @get_page:
		 *
		 * The cached iterator for looking up struct pages in @pages.
		 */
		struct i915_gem_object_page_iter get_page;

		/**
		 * @get_dma_page:
		 *
		 * The cached iterator for looking up device addresses in
		 * @pages.
		 */
		struct i915_gem_object_page_iter get_dma_page;

		/**
		 * @link:
		 *
		 * Element within i915->mm.unbound_list or i915->mm.bound_list,
		 * locked by i915->mm.obj_lock.
		 */
		struct list_head link;

		/**
		 * @madv: The advice hint for the pages.
		 *
		 * See &drm_i915_gem_madvise.madv.
		 */
		unsigned int madv:2;

		/**
		 * @dirty:
		 *
		 * This is set if the object might have been written to since
		 * the pages were acquired. Tracking if the object is dirty
		 * tells us if we can for example simply discard the pages,
		 * instead of having to persist their contents, assuming the
		 * object is still marked as I915_MADV_WILLNEED.
		 */
		bool dirty:1;
	} mm;

	/**
	 * @ttm:
	 *
	 * The TTM specific state for this object. Currently for discrete
	 * only.
	 */
	struct {
		/**
		 * @cached_io_st:
		 *
		 * Some nasty sleight of hand to manage the sg_table for
		 * discrete, which uses use the TTM backend instead.
		 */
		struct sg_table *cached_io_st;

		/**
		 * @get_io_page: The cached iterator for @cached_io_st
		 */
		struct i915_gem_object_page_iter get_io_page;

		/**
		 * @created:
		 *
		 * Some more nasty sleight of hand to manage the object
		 * destruction differences when the TTM backend is used. Nothing
		 * to see here.
		 */
		bool created:1;
	} ttm;

	/** @bit_17 : Record of address bit 17 of each page at last unbind. */
	unsigned long *bit_17;

	union {
#ifdef CONFIG_MMU_NOTIFIER
		/**
		 * @userptr:
		 *
		 * Track the userptr specific state if this is a userptr object.
		 */
		struct {
			/**
			 * @ptr:
			 *
			 * The user provided virtual address for the memory.
			 */
			uintptr_t ptr;

			/**
			 * @notifier_seq: The notifier sequence number.
			 */
			unsigned long notifier_seq;

			/** @notifier: The struct mmu_interval_notifier */
			struct mmu_interval_notifier notifier;

			/**
			 * @pvec:
			 *
			 * The array of struct pages, as per the provided @ptr.
			 */
			struct page **pvec;

			/**
			 * @page_ref:
			 *
			 * The userptr reference count for the pages.
			 */
			int page_ref;
		} userptr;
#endif
		/**
		 * @stolen:
		 *
		 * Pointer to the contiguous memory block if this is a stolen
		 * memory object.
		 */
		struct drm_mm_node *stolen;

		/** @scratch: SELFTEST ONLY */
		unsigned long scratch;

		/**
		 * @encode:
		 *
		 * Cached PTE encoding for this object, i.e it has the PTE_LM,
		 * caching bits, DMA address etc already built.
		 *
		 * Note that this is *only* used for scratch pages, where it's
		 * an extremely common operation to point the various paging
		 * structures(PDE, PTE etc) at the respective scratch page, and
		 * since the scratch page is static the encoding value here
		 * shouldn't change.
		 */
		u64 encode;

		/**
		 * @gvt_info:
		 *
		 * The GVT specific state, assuming GVT is indeed active.
		 */
		void *gvt_info;
	};
};

static inline struct drm_i915_gem_object *
to_intel_bo(struct drm_gem_object *gem)
{
	/* Assert that to_intel_bo(NULL) == NULL */
	BUILD_BUG_ON(offsetof(struct drm_i915_gem_object, base));

	return container_of(gem, struct drm_i915_gem_object, base);
}

#endif
