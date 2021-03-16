// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 Pengutronix, Steffen Trumtrar <kernel@pengutronix.de>
 * Copyright (C) 2021 Pengutronix, Ahmad Fatoum <kernel@pengutronix.de>
 */

#include <linux/device.h>
#include <soc/fsl/caam-blob.h>

#include "compat.h"
#include "desc_constr.h"
#include "desc.h"
#include "error.h"
#include "intern.h"
#include "jr.h"
#include "regs.h"

struct caam_blob_priv {
	struct device jrdev;
};

struct caam_blob_job_result {
	int err;
	struct completion completion;
};

static void caam_blob_job_done(struct device *dev, u32 *desc, u32 err, void *context)
{
	struct caam_blob_job_result *res = context;
	int ecode = 0;

	dev_dbg(dev, "%s %d: err 0x%x\n", __func__, __LINE__, err);

	if (err)
		ecode = caam_jr_strstatus(dev, err);

	res->err = ecode;

	/*
	 * Upon completion, desc points to a buffer containing a CAAM job
	 * descriptor which encapsulates data into an externally-storable
	 * blob.
	 */
	complete(&res->completion);
}

static u32 *caam_blob_alloc_desc(size_t keymod_len)
{
	size_t len;

	/* header + (key mod immediate) + 2x pointers + op */
	len = 4 + (4 + ALIGN(keymod_len, 4)) + 2*(4 + 4 + CAAM_PTR_SZ_MAX) + 4;

	if (len > CAAM_DESC_BYTES_MAX)
		return NULL;

	return kzalloc(len, GFP_KERNEL | GFP_DMA);
}

int caam_encap_blob(struct caam_blob_priv *priv, const char *keymod,
		    void *input, void *output, size_t length)
{
	u32 *desc;
	struct device *jrdev = &priv->jrdev;
	dma_addr_t dma_in, dma_out;
	struct caam_blob_job_result testres;
	size_t keymod_len = strlen(keymod);
	int ret;

	if (length <= CAAM_BLOB_OVERHEAD)
		return -EINVAL;

	desc = caam_blob_alloc_desc(keymod_len);
	if (!desc) {
		dev_err(jrdev, "unable to allocate desc\n");
		return -ENOMEM;
	}

	dma_in = dma_map_single(jrdev, input, length - CAAM_BLOB_OVERHEAD, DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, dma_in)) {
		dev_err(jrdev, "unable to map input DMA buffer\n");
		ret = -ENOMEM;
		goto out_free;
	}

	dma_out = dma_map_single(jrdev, output, length,	DMA_FROM_DEVICE);
	if (dma_mapping_error(jrdev, dma_out)) {
		dev_err(jrdev, "unable to map output DMA buffer\n");
		ret = -ENOMEM;
		goto out_unmap_in;
	}

	/*
	 * A data blob is encrypted using a blob key (BK); a random number.
	 * The BK is used as an AES-CCM key. The initial block (B0) and the
	 * initial counter (Ctr0) are generated automatically and stored in
	 * Class 1 Context DWords 0+1+2+3. The random BK is stored in the
	 * Class 1 Key Register. Operation Mode is set to AES-CCM.
	 */

	init_job_desc(desc, 0);
	append_key_as_imm(desc, keymod, keymod_len, keymod_len,
			  CLASS_2 | KEY_DEST_CLASS_REG);
	append_seq_in_ptr(desc, dma_in, length - CAAM_BLOB_OVERHEAD, 0);
	append_seq_out_ptr(desc, dma_out, length, 0);
	append_operation(desc, OP_TYPE_ENCAP_PROTOCOL | OP_PCLID_BLOB);

	print_hex_dump_debug("data@"__stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 1, input,
			     length - CAAM_BLOB_OVERHEAD, false);
	print_hex_dump_debug("jobdesc@"__stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 1, desc,
			     desc_bytes(desc), false);

	testres.err = 0;
	init_completion(&testres.completion);

	ret = caam_jr_enqueue(jrdev, desc, caam_blob_job_done, &testres);
	if (ret == -EINPROGRESS) {
		wait_for_completion(&testres.completion);
		ret = testres.err;
		print_hex_dump_debug("output@"__stringify(__LINE__)": ",
				     DUMP_PREFIX_ADDRESS, 16, 1, output,
				     length, false);
	}

	dma_unmap_single(jrdev, dma_out, length, DMA_FROM_DEVICE);
out_unmap_in:
	dma_unmap_single(jrdev, dma_in, length - CAAM_BLOB_OVERHEAD, DMA_TO_DEVICE);
out_free:
	kfree(desc);

	return ret;
}
EXPORT_SYMBOL(caam_encap_blob);

int caam_decap_blob(struct caam_blob_priv *priv, const char *keymod,
		    void *input, void *output, size_t length)
{
	u32 *desc;
	struct device *jrdev = &priv->jrdev;
	dma_addr_t dma_in, dma_out;
	struct caam_blob_job_result testres;
	size_t keymod_len = strlen(keymod);
	int ret;

	if (length <= CAAM_BLOB_OVERHEAD)
		return -EINVAL;

	desc = caam_blob_alloc_desc(keymod_len);
	if (!desc) {
		dev_err(jrdev, "unable to allocate desc\n");
		return -ENOMEM;
	}

	dma_in = dma_map_single(jrdev, input, length, DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, dma_in)) {
		dev_err(jrdev, "unable to map input DMA buffer\n");
		ret = -ENOMEM;
		goto out_free;
	}

	dma_out = dma_map_single(jrdev, output, length - CAAM_BLOB_OVERHEAD, DMA_FROM_DEVICE);
	if (dma_mapping_error(jrdev, dma_out)) {
		dev_err(jrdev, "unable to map output DMA buffer\n");
		ret = -ENOMEM;
		goto out_unmap_in;
	}

	/*
	 * A data blob is encrypted using a blob key (BK); a random number.
	 * The BK is used as an AES-CCM key. The initial block (B0) and the
	 * initial counter (Ctr0) are generated automatically and stored in
	 * Class 1 Context DWords 0+1+2+3. The random BK is stored in the
	 * Class 1 Key Register. Operation Mode is set to AES-CCM.
	 */

	init_job_desc(desc, 0);
	append_key_as_imm(desc, keymod, keymod_len, keymod_len,
			  CLASS_2 | KEY_DEST_CLASS_REG);
	append_seq_in_ptr(desc, dma_in, length, 0);
	append_seq_out_ptr(desc, dma_out, length - CAAM_BLOB_OVERHEAD, 0);
	append_operation(desc, OP_TYPE_DECAP_PROTOCOL | OP_PCLID_BLOB);

	print_hex_dump_debug("data@"__stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 1, input,
			     length, false);
	print_hex_dump_debug("jobdesc@"__stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 1, desc,
			     desc_bytes(desc), false);

	testres.err = 0;
	init_completion(&testres.completion);

	ret = caam_jr_enqueue(jrdev, desc, caam_blob_job_done, &testres);
	if (ret == -EINPROGRESS) {
		wait_for_completion(&testres.completion);
		ret = testres.err;
		print_hex_dump_debug("output@"__stringify(__LINE__)": ",
				     DUMP_PREFIX_ADDRESS, 16, 1, output,
				     length - CAAM_BLOB_OVERHEAD, false);
	}

	dma_unmap_single(jrdev, dma_out, length - CAAM_BLOB_OVERHEAD, DMA_FROM_DEVICE);
out_unmap_in:
	dma_unmap_single(jrdev, dma_in, length, DMA_TO_DEVICE);
out_free:
	kfree(desc);

	return ret;
}
EXPORT_SYMBOL(caam_decap_blob);

struct caam_blob_priv *caam_blob_gen_init(void)
{
	struct device *jrdev;

	jrdev = caam_jr_alloc();
	if (IS_ERR(jrdev))
		return ERR_CAST(jrdev);

	return container_of(jrdev, struct caam_blob_priv, jrdev);
}
EXPORT_SYMBOL(caam_blob_gen_init);

void caam_blob_gen_exit(struct caam_blob_priv *priv)
{
	caam_jr_free(&priv->jrdev);
}
EXPORT_SYMBOL(caam_blob_gen_exit);
