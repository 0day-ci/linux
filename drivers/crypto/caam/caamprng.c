// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver to expose SEC4 PRNG via crypto RNG API
 *
 * Copyright 2022 NXP
 *
 */

#include <linux/completion.h>
#include <crypto/internal/rng.h>
#include "compat.h"
#include "regs.h"
#include "intern.h"
#include "desc_constr.h"
#include "jr.h"
#include "error.h"

/*
 * Length of used descriptors, see caam_init_desc()
 */
#define CAAM_PRNG_DESC_LEN (CAAM_CMD_SZ +				\
			    CAAM_CMD_SZ +				\
			    CAAM_CMD_SZ + CAAM_PTR_SZ_MAX)

/* prng per-device context */
struct caam_prng_ctx {
	struct device *jrdev;
	struct completion done;
};

struct caam_prng_alg {
	struct rng_alg rng;
	bool registered;
};

static void caam_prng_done(struct device *jrdev, u32 *desc, u32 err,
			  void *context)
{
	struct caam_prng_ctx *jctx = context;

	if (err)
		caam_jr_strstatus(jrdev, err);

	complete(&jctx->done);
}

static u32 *caam_init_reseed_desc(u32 *desc, dma_addr_t seed_dma, u32 len)
{
	init_job_desc(desc, 0);	/* + 1 cmd_sz */
	/* Generate random bytes: + 1 cmd_sz */
	append_operation(desc, OP_TYPE_CLASS1_ALG | OP_ALG_ALGSEL_RNG |
			OP_ALG_AS_FINALIZE | OP_ALG_AI_ON);
	/* Store bytes: + 1 cmd_sz + caam_ptr_sz  */
	append_load(desc, seed_dma, len, CLASS_1 | LDST_SRCDST_BYTE_CONTEXT);

	print_hex_dump_debug("prng reseed desc@: ", DUMP_PREFIX_ADDRESS,
			     16, 4, desc, desc_bytes(desc), 1);

	return desc;
}

static u32 *caam_init_prng_desc(u32 *desc, dma_addr_t dst_dma, u32 len)
{
	init_job_desc(desc, 0);	/* + 1 cmd_sz */
	/* Generate random bytes: + 1 cmd_sz */
	append_operation(desc, OP_ALG_ALGSEL_RNG | OP_TYPE_CLASS1_ALG);
	/* Store bytes: + 1 cmd_sz + caam_ptr_sz  */
	append_fifo_store(desc, dst_dma,
			  len, FIFOST_TYPE_RNGSTORE);

	print_hex_dump_debug("prng job desc@: ", DUMP_PREFIX_ADDRESS,
			     16, 4, desc, desc_bytes(desc), 1);

	return desc;
}

static int caam_prng_generate(struct crypto_rng *tfm,
			     const u8 *src, unsigned int slen,
			     u8 *dst, unsigned int dlen)
{
	struct caam_prng_ctx ctx;
	dma_addr_t dst_dma;
	u32 *desc;
	int ret;

	ctx.jrdev = caam_jr_alloc();
	ret = PTR_ERR_OR_ZERO(ctx.jrdev);
	if (ret) {
		pr_err("Job Ring Device allocation failed\n");
		return ret;
	}

	desc = kzalloc(CAAM_PRNG_DESC_LEN, GFP_KERNEL | GFP_DMA);
	if (!desc) {
		caam_jr_free(ctx.jrdev);
		return -ENOMEM;
	}

	dst_dma = dma_map_single(ctx.jrdev, dst, dlen, DMA_FROM_DEVICE);
	if (dma_mapping_error(ctx.jrdev, dst_dma)) {
		dev_err(ctx.jrdev, "Failed to map destination buffer memory\n");
		ret = -ENOMEM;
		goto out;
	}

	init_completion(&ctx.done);
	ret = caam_jr_enqueue(ctx.jrdev,
			      caam_init_prng_desc(desc, dst_dma, dlen),
			      caam_prng_done, &ctx);

	if (ret == -EINPROGRESS) {
		wait_for_completion(&ctx.done);
		ret = 0;
	}

	dma_unmap_single(ctx.jrdev, dst_dma, dlen, DMA_FROM_DEVICE);

out:
	kfree(desc);
	caam_jr_free(ctx.jrdev);
	return ret;
}

static void caam_prng_exit(struct crypto_tfm *tfm)
{

	return;
}

static int caam_prng_init(struct crypto_tfm *tfm)
{
	return 0;
}

static int caam_prng_seed(struct crypto_rng *tfm,
			 const u8 *seed, unsigned int slen)
{
	struct caam_prng_ctx ctx;
	dma_addr_t seed_dma;
	u32 *desc;
	int ret;

	ctx.jrdev = caam_jr_alloc();
	ret = PTR_ERR_OR_ZERO(ctx.jrdev);
	if (ret) {
		pr_err("Job Ring Device allocation failed\n");
		return ret;
	}

	desc = kzalloc(CAAM_PRNG_DESC_LEN, GFP_KERNEL | GFP_DMA);
	if (!desc) {
		caam_jr_free(ctx.jrdev);
		return -ENOMEM;
	}

	seed_dma = dma_map_single(ctx.jrdev, seed, slen, DMA_FROM_DEVICE);
	if (dma_mapping_error(ctx.jrdev, seed_dma)) {
		dev_err(ctx.jrdev, "Failed to map destination buffer memory\n");
		ret = -ENOMEM;
		goto out;
	}

	init_completion(&ctx.done);
	ret = caam_jr_enqueue(ctx.jrdev,
			      caam_init_reseed_desc(desc, seed_dma, slen),
			      caam_prng_done, &ctx);

	if (ret == -EINPROGRESS) {
		wait_for_completion(&ctx.done);
		ret = 0;
	}

	dma_unmap_single(ctx.jrdev, seed_dma, slen, DMA_FROM_DEVICE);

out:
	kfree(desc);
	caam_jr_free(ctx.jrdev);
	return ret;
}

static struct caam_prng_alg caam_prng_alg = {
	.rng = {
		.generate = caam_prng_generate,
		.seed = caam_prng_seed,
		.seedsize = 32,
		.base = {
			.cra_name = "stdrng",
			.cra_driver_name = "prng-caam",
			.cra_priority = 500,
			.cra_ctxsize = sizeof(struct caam_prng_ctx),
			.cra_module = THIS_MODULE,
			.cra_init = caam_prng_init,
			.cra_exit = caam_prng_exit,
		},
	}
};

void caam_prng_unregister(void *data)
{
	if (caam_prng_alg.registered)
		crypto_unregister_rng(&caam_prng_alg.rng);
}

int caam_prng_register(struct device *ctrldev)
{
	struct caam_drv_private *priv = dev_get_drvdata(ctrldev);
	u32 rng_inst;
	int ret = 0;

	/* Check for available RNG blocks before registration */
	if (priv->era < 10)
		rng_inst = (rd_reg32(&priv->jr[0]->perfmon.cha_num_ls) &
			    CHA_ID_LS_RNG_MASK) >> CHA_ID_LS_RNG_SHIFT;
	else
		rng_inst = rd_reg32(&priv->jr[0]->vreg.rng) & CHA_VER_NUM_MASK;

	if (!rng_inst) {
		dev_dbg(ctrldev, "RNG block is not available..."
				"skipping registering rng algorithm\n");

		return ret;
	}

	ret = crypto_register_rng(&caam_prng_alg.rng);
	if (ret) {
		dev_err(ctrldev,
			"couldn't register rng crypto alg: %d\n",
			ret);
		return ret;
	}

	caam_prng_alg.registered = true;

	dev_info(ctrldev,
		 "rng crypto API alg registered %s\n", caam_prng_alg.rng.base.cra_name);

	return 0;
}
