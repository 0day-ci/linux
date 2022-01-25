// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * XCTR: XOR Counter mode - Adapted from ctr.c
 *
 * (C) Copyright IBM Corp. 2007 - Joy Latten <latten@us.ibm.com>
 * Copyright 2021 Google LLC
 */

/*
 * XCTR mode is a blockcipher mode of operation used to implement HCTR2. XCTR is
 * closely related to the CTR mode of operation; the main difference is that CTR
 * generates the keystream using E(CTR + IV) whereas XCTR generates the
 * keystream using E(CTR ^ IV).
 *
 * See the HCTR2 paper for more details:
 *	Length-preserving encryption with HCTR2
 *      (https://eprint.iacr.org/2021/1441.pdf)
 */

#include <crypto/algapi.h>
#include <crypto/xctr.h>
#include <crypto/internal/cipher.h>
#include <crypto/internal/skcipher.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

static void crypto_xctr_crypt_final(struct skcipher_walk *walk,
				   struct crypto_cipher *tfm, u32 byte_ctr)
{
	unsigned int bsize = crypto_cipher_blocksize(tfm);
	unsigned long alignmask = crypto_cipher_alignmask(tfm);
	u8 ctr[MAX_CIPHER_BLOCKSIZE];
	u8 ctrblk[MAX_CIPHER_BLOCKSIZE];
	u8 tmp[MAX_CIPHER_BLOCKSIZE + MAX_CIPHER_ALIGNMASK];
	u8 *keystream = PTR_ALIGN(tmp + 0, alignmask + 1);
	u8 *src = walk->src.virt.addr;
	u8 *dst = walk->dst.virt.addr;
	unsigned int nbytes = walk->nbytes;
	u32 ctr32 = byte_ctr / bsize + 1;

	u32_to_le_block(ctr, ctr32, bsize);
	crypto_xor_cpy(ctrblk, ctr, walk->iv, bsize);
	crypto_cipher_encrypt_one(tfm, keystream, ctrblk);
	crypto_xor_cpy(dst, keystream, src, nbytes);
}

static int crypto_xctr_crypt_segment(struct skcipher_walk *walk,
				    struct crypto_cipher *tfm, u32 byte_ctr)
{
	void (*fn)(struct crypto_tfm *, u8 *, const u8 *) =
		   crypto_cipher_alg(tfm)->cia_encrypt;
	unsigned int bsize = crypto_cipher_blocksize(tfm);
	u8 ctr[MAX_CIPHER_BLOCKSIZE];
	u8 ctrblk[MAX_CIPHER_BLOCKSIZE];
	u8 *src = walk->src.virt.addr;
	u8 *dst = walk->dst.virt.addr;
	unsigned int nbytes = walk->nbytes;
	u32 ctr32 = byte_ctr / bsize + 1;

	do {
		/* create keystream */
		u32_to_le_block(ctr, ctr32, bsize);
		crypto_xor_cpy(ctrblk, ctr, walk->iv, bsize);
		fn(crypto_cipher_tfm(tfm), dst, ctrblk);
		crypto_xor(dst, src, bsize);

		ctr32++;

		src += bsize;
		dst += bsize;
	} while ((nbytes -= bsize) >= bsize);

	return nbytes;
}

static int crypto_xctr_crypt_inplace(struct skcipher_walk *walk,
				    struct crypto_cipher *tfm, u32 byte_ctr)
{
	void (*fn)(struct crypto_tfm *, u8 *, const u8 *) =
		   crypto_cipher_alg(tfm)->cia_encrypt;
	unsigned int bsize = crypto_cipher_blocksize(tfm);
	unsigned long alignmask = crypto_cipher_alignmask(tfm);
	unsigned int nbytes = walk->nbytes;
	u8 ctr[MAX_CIPHER_BLOCKSIZE];
	u8 ctrblk[MAX_CIPHER_BLOCKSIZE];
	u8 *src = walk->src.virt.addr;
	u8 tmp[MAX_CIPHER_BLOCKSIZE + MAX_CIPHER_ALIGNMASK];
	u8 *keystream = PTR_ALIGN(tmp + 0, alignmask + 1);
	u32 ctr32 = byte_ctr / bsize + 1;

	u32_to_le_block(ctr, ctr32, bsize);
	do {
		/* create keystream */
		u32_to_le_block(ctr, ctr32, bsize);
		crypto_xor_cpy(ctrblk, ctr, walk->iv, bsize);
		fn(crypto_cipher_tfm(tfm), keystream, ctrblk);
		crypto_xor(src, keystream, bsize);

		ctr32++;

		src += bsize;
	} while ((nbytes -= bsize) >= bsize);

	return nbytes;
}

static int crypto_xctr_crypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_cipher *cipher = skcipher_cipher_simple(tfm);
	const unsigned int bsize = crypto_cipher_blocksize(cipher);
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;
	u32 byte_ctr = 0;

	err = skcipher_walk_virt(&walk, req, false);

	while (walk.nbytes >= bsize) {
		if (walk.src.virt.addr == walk.dst.virt.addr)
			nbytes = crypto_xctr_crypt_inplace(&walk, cipher, byte_ctr);
		else
			nbytes = crypto_xctr_crypt_segment(&walk, cipher, byte_ctr);

		byte_ctr += walk.nbytes - nbytes;
		err = skcipher_walk_done(&walk, nbytes);
	}

	if (walk.nbytes) {
		crypto_xctr_crypt_final(&walk, cipher, byte_ctr);
		err = skcipher_walk_done(&walk, 0);
	}

	return err;
}

static int crypto_xctr_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	struct skcipher_instance *inst;
	struct crypto_alg *alg;
	int err;

	inst = skcipher_alloc_instance_simple(tmpl, tb);
	if (IS_ERR(inst))
		return PTR_ERR(inst);

	alg = skcipher_ialg_simple(inst);

	/* Block size must be >= 4 bytes. */
	err = -EINVAL;
	if (alg->cra_blocksize < 4)
		goto out_free_inst;

	/* XCTR mode is a stream cipher. */
	inst->alg.base.cra_blocksize = 1;

	/*
	 * To simplify the implementation, configure the skcipher walk to only
	 * give a partial block at the very end, never earlier.
	 */
	inst->alg.chunksize = alg->cra_blocksize;

	inst->alg.encrypt = crypto_xctr_crypt;
	inst->alg.decrypt = crypto_xctr_crypt;

	err = skcipher_register_instance(tmpl, inst);
	if (err) {
out_free_inst:
		inst->free(inst);
	}

	return err;
}

static struct crypto_template crypto_xctr_tmpl[] = {
	{
		.name = "xctr",
		.create = crypto_xctr_create,
		.module = THIS_MODULE,
	}
};

static int __init crypto_xctr_module_init(void)
{
	return crypto_register_template(crypto_xctr_tmpl);
}

static void __exit crypto_xctr_module_exit(void)
{
	crypto_unregister_template(crypto_xctr_tmpl);
}

subsys_initcall(crypto_xctr_module_init);
module_exit(crypto_xctr_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("XCTR block cipher mode of operation");
MODULE_ALIAS_CRYPTO("xctr");
MODULE_IMPORT_NS(CRYPTO_INTERNAL);
