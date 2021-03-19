/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Intel Keem Bay camera ISP metadata video node.
 *
 * Copyright (C) 2021 Intel Corporation
 */
#ifndef KEEMBAY_METADATA_H
#define KEEMBAY_METADATA_H

#include <media/v4l2-dev.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-v4l2.h>

#include "keembay-vpu-isp.h"
#include "keembay-params-defaults.h"

/**
 * enum kmb_metadata_table_type - Keembay metadata table type
 * @KMB_METADATA_TABLE_LSC: Lens shading table
 * @KMB_METADATA_TABLE_SDEFECT: Static defect pixel table
 * @KMB_METADATA_TABLE_LCA:  Lateral цhroma аberration table
 * @KMB_METADATA_TABLE_HDR: HDR table
 * @KMB_METADATA_TABLE_SHARP: Shartpnes table
 * @KMB_METADATA_TABLE_COLOR_CUMB: Color combination table
 * @KMB_METADATA_TABLE_TNF0: Temporal denoise first table
 * @KMB_METADATA_TABLE_TNF1: Temporal denoise second table
 * @KMB_METADATA_TABLE_WARP: Warp mesh table
 */
enum kmb_metadata_table_type {
	KMB_METADATA_TABLE_LSC		= 0,
	KMB_METADATA_TABLE_SDEFECT	= 1,
	KMB_METADATA_TABLE_LCA		= 2,
	KMB_METADATA_TABLE_HDR		= 3,
	KMB_METADATA_TABLE_SHARP	= 4,
	KMB_METADATA_TABLE_COLOR_CUMB	= 5,
	KMB_METADATA_TABLE_LUT		= 6,
	KMB_METADATA_TABLE_TNF0		= 7,
	KMB_METADATA_TABLE_TNF1		= 8,
	KMB_METADATA_TABLE_WARP		= 10,
	KMB_METADATA_TABLE_MAX		= 11,
};

/**
 * enum kmb_metadata_type - Keembay metadata type
 * @KMB_METADATA_PARAMS: Keembay metadata parameters
 * @KMB_METADATA_STATS: Keembay metadata statistics
 */
enum kmb_metadata_type {
	KMB_METADATA_PARAMS,
	KMB_METADATA_STATS,
};

/**
 * struct kmb_metadata_table - Keembay metadata table
 * @refcount: Metadata table reference count
 * @dma_addr: Physical address of the table
 * @cpu_addr: Virtual address of the table
 * @pool: Dma pool from where table was allocated
 */
struct kmb_metadata_table {
	struct kref refcount;
	dma_addr_t dma_addr;
	void *cpu_addr;
	struct dma_pool *pool;
};

/**
 * struct kmb_metadata_buf - Keembay metadata buffer handle
 * @vb: Video buffer for v4l2
 * @type: Metadata type
 * @stats: Statistics physical addresses
 * @stats.raw: VPU raw statistics physical addresses
 * @stats.dehaze_stats_addr: VPU dehaze statistics physical address
 * @params: VPU ISP parameters
 * @params.isp: VPU ISP parameters virtual address
 * @params.dma_addr_isp: VPU ISP parameters physical address
 * @params.tab: Metadata tables
 * @list: List for buffer queue
 */
struct kmb_metadata_buf {
	struct vb2_v4l2_buffer vb;
	enum kmb_metadata_type type;
	union {
		struct {
			struct kmb_vpu_raw_stats raw[KMB_VPU_MAX_EXPOSURES];
			u64 dehaze_stats_addr;
		} stats;
		struct {
			struct kmb_vpu_isp_params *isp;
			dma_addr_t dma_addr_isp;
			struct kmb_metadata_table *tab[KMB_METADATA_TABLE_MAX];
		} params;
	};
	struct list_head list;
};

/**
 * struct kmb_metabuf_queue_ops - Keembay metadata queue operations
 * @queue: queue an metadata buffer
 * @flish: discard all metadata buffers
 */
struct kmb_metabuf_queue_ops {
	int (*queue)(void *priv, struct kmb_metadata_buf *buf);
	void (*flush)(void *priv);
};

/**
 * struct kmb_metadata - Keembay metadata device
 * @lock: mutex to protect keembay metadata device
 * @video: pointer to V4L2 video device node
 * @dma_dev: pointer to dma device
 * @pad: media pad graph objects
 * @vb2_q: V4L2 Video buffer queue
 * @type: Metadata type
 * @pipe: pointer to KMB pipeline object
 * @priv: pointer to private data
 * @queue_ops: Metadata buffer queue operations
 * @table_pools_refcnt: Table pool reference count
 * @table_pool: ISP tables dma pool
 * @last_buf: Pointer to last enqueued buffer
 * @format: Active format
 * @def: Default ISP params
 */
struct kmb_metadata {
	struct mutex lock;
	struct video_device video;
	struct device *dma_dev;
	struct media_pad pad;
	struct vb2_queue vb2_q;
	enum kmb_metadata_type type;

	struct kmb_pipeline *pipe;

	void *priv;
	const struct kmb_metabuf_queue_ops *queue_ops;

	unsigned int table_pools_refcnt;
	struct dma_pool *table_pool[KMB_METADATA_TABLE_MAX];

	struct kmb_metadata_buf *last_buf;

	struct v4l2_meta_format format;

	struct kmb_vpu_isp_params_defaults def;
};

int kmb_metadata_init(struct kmb_metadata *kmb_meta);
void kmb_metadata_cleanup(struct kmb_metadata *kmb_meta);

int kmb_metadata_register(struct kmb_metadata *kmb_meta,
			  struct v4l2_device *v4l2_dev);
void kmb_metadata_unregister(struct kmb_metadata *kmb_meta);

#endif /* KEEMBAY_METADATA_H */
