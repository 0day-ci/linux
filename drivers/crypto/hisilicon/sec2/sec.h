/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 HiSilicon Limited. */

#ifndef __HISI_SEC_V2_H
#define __HISI_SEC_V2_H

#include "../qm.h"
#include "sec_crypto.h"

/* Algorithm resource per hardware SEC queue */
struct sec_alg_res {
	u8 *pbuf;
	dma_addr_t pbuf_dma;
	u8 *c_ivin;
	dma_addr_t c_ivin_dma;
	u8 *a_ivin;
	dma_addr_t a_ivin_dma;
	u8 *out_mac;
	dma_addr_t out_mac_dma;
};

/* Cipher request of SEC private */
struct sec_cipher_req {
	struct hisi_acc_hw_sgl *c_out;
	dma_addr_t c_out_dma;
	u8 *c_ivin;
	dma_addr_t c_ivin_dma;
	struct skcipher_request *sk_req;
	u32 c_len;
	bool encrypt;
};

struct sec_aead_req {
	u8 *out_mac;
	dma_addr_t out_mac_dma;
	u8 *a_ivin;
	dma_addr_t a_ivin_dma;
	struct aead_request *aead_req;
};

struct sec_ahash_req {
	struct scatterlist *req_sg;
	u8 sg_cut_len[SEC_MAX_SG_OF_REMAIN];
	u32 cut_num;
	u8 op;
	u8 done;
	struct ahash_request *ahash_req;
	/* Means long hash and ping-pong buffer flag */
	bool is_stream_mode;
	u32 req_data_len;
	/* Currently ping-pong data len */
	u32 pp_data_len;
	u64 total_data_len;
	/* Length of data every bd sent to hardware */
	u32 block_data_len;
	int sid;

	struct scatterlist *pp_sg;
	/* Current pingpong hw sg dma address */
	dma_addr_t pp_dma;
};

/* SEC request of Crypto */
struct sec_req {
	union {
		struct sec_sqe sec_sqe;
		struct sec_sqe3 sec_sqe3;
	};
	struct sec_ctx *ctx;
	struct sec_qp_ctx *qp_ctx;

	/**
	 * Common parameter of the SEC request.
	 * ahash, hardware sgl used to hold request's source buffers
	 */
	struct hisi_acc_hw_sgl *in;
	dma_addr_t in_dma;
	struct sec_cipher_req c_req;
	struct sec_aead_req aead_req;
	struct sec_ahash_req hash_req;
	struct list_head backlog_head;

	int err_type;
	int req_id;
	u32 flag;

	/* Status of the SEC request */
	bool fake_busy;
	bool use_pbuf;
};

/**
 * struct sec_req_op - Operations for SEC request
 * @buf_map: DMA map the SGL buffers of the request
 * @buf_unmap: DMA unmap the SGL buffers of the request
 * @bd_fill: Fill the SEC queue BD
 * @bd_send: Send the SEC BD into the hardware queue
 * @callback: Call back for the request
 * @process: Main processing logic of Skcipher
 */
struct sec_req_op {
	int (*buf_map)(struct sec_ctx *ctx, struct sec_req *req);
	void (*buf_unmap)(struct sec_ctx *ctx, struct sec_req *req);
	void (*do_transfer)(struct sec_ctx *ctx, struct sec_req *req);
	int (*bd_fill)(struct sec_ctx *ctx, struct sec_req *req);
	int (*bd_send)(struct sec_ctx *ctx, struct sec_req *req);
	void (*callback)(struct sec_ctx *ctx, struct sec_req *req, int err);
	int (*process)(struct sec_ctx *ctx, struct sec_req *req);
};

/* SEC auth context */
struct sec_auth_ctx {
	dma_addr_t a_key_dma;
	u8 *a_key;
	u8 a_key_len;
	u8 mac_len;
	u8 a_alg;
	u32 blk_size;
	u32 align_sz;
	dma_addr_t metamac_dma;
	void *metamac;
	bool fallback;

	/**
	 * There are two buffers in the above 'metamac' for each stream.
	 * -1: no meta-mac (no input hash digest), the 0th buffer is out-mac
	 * 0: the 0th buffer is meta-mac, the 1th buffer is out-mac
	 * 1: the 1th buffer is meta-mac, the 0th buffer is out-mac
	 */
	char metamac_idx[SEC_MAX_STREAMS];
	struct crypto_shash *hash_tfm;
	/* alloc soft hash tfm for not support parameter */
	struct crypto_shash *fallback_ahash_tfm;
	struct shash_desc *desc;
	struct crypto_aead *fallback_aead_tfm;
};

/* SEC cipher context which cipher's relatives */
struct sec_cipher_ctx {
	u8 *c_key;
	dma_addr_t c_key_dma;
	sector_t iv_offset;
	u32 c_gran_size;
	u32 ivsize;
	u8 c_mode;
	u8 c_alg;
	u8 c_key_len;

	/* add software support */
	bool fallback;
	struct crypto_sync_skcipher *fbtfm;
};

/* SEC queue context which defines queue's relatives */
struct sec_qp_ctx {
	struct hisi_qp *qp;
	struct sec_req *req_list[QM_Q_DEPTH];
	struct idr req_idr;
	struct sec_alg_res res[QM_Q_DEPTH];
	struct sec_ctx *ctx;
	struct mutex req_lock;
	struct list_head backlog;
	struct hisi_acc_sgl_pool *c_in_pool;
	struct hisi_acc_sgl_pool *c_out_pool;
};

enum sec_alg_type {
	SEC_SKCIPHER,
	SEC_AEAD,
	SEC_AHASH
};

/* SEC Crypto TFM context which defines queue and cipher .etc relatives */
struct sec_ctx {
	struct sec_qp_ctx *qp_ctx;
	struct sec_dev *sec;
	const struct sec_req_op *req_op;
	struct hisi_qp **qps;

	/* Half queues for encipher, and half for decipher */
	u32 hlf_q_num;

	/* Threshold for fake busy, trigger to return -EBUSY to user */
	u32 fake_req_limit;

	/* Currrent cyclic index to select a queue for encipher */
	atomic_t enc_qcyclic;

	 /* Currrent cyclic index to select a queue for decipher */
	atomic_t dec_qcyclic;

	enum sec_alg_type alg_type;
	bool pbuf_supported;
	struct sec_cipher_ctx c_ctx;
	struct sec_auth_ctx a_ctx;
	u8 type_supported;

	/*
	 * For stream mode operations, we don't send data into HW everytime.
	 * now ahash uses this. To avoid multiple copy, use pingpong buffers.
	 */
	struct scatterlist pingpong_sg[SEC_MAX_STREAMS][PINGPONG_BUF_NUM];
	u8 pingpong_idx[SEC_MAX_STREAMS];
	u32 pp_data_len[SEC_MAX_STREAMS][PINGPONG_BUF_NUM];
	struct idr stream_idr;
	struct mutex stream_idr_lock;
	struct device *dev;
};

enum sec_endian {
	SEC_LE = 0,
	SEC_32BE,
	SEC_64BE
};

enum sec_debug_file_index {
	SEC_CLEAR_ENABLE,
	SEC_DEBUG_FILE_NUM,
};

struct sec_debug_file {
	enum sec_debug_file_index index;
	spinlock_t lock;
	struct hisi_qm *qm;
};

struct sec_dfx {
	atomic64_t send_cnt;
	atomic64_t recv_cnt;
	atomic64_t send_busy_cnt;
	atomic64_t recv_busy_cnt;
	atomic64_t err_bd_cnt;
	atomic64_t invalid_req_cnt;
	atomic64_t done_flag_cnt;
};

struct sec_debug {
	struct sec_dfx dfx;
	struct sec_debug_file files[SEC_DEBUG_FILE_NUM];
};

struct sec_dev {
	struct hisi_qm qm;
	struct sec_debug debug;
	u32 ctx_q_num;
	bool iommu_used;
};

void sec_destroy_qps(struct hisi_qp **qps, int qp_num);
struct hisi_qp **sec_create_qps(void);
int sec_register_to_crypto(struct hisi_qm *qm);
void sec_unregister_from_crypto(struct hisi_qm *qm);
#endif
