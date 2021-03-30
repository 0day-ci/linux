// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * RSA padding templates.
 *
 * Copyright (c) 2015  Intel Corporation
 */

#include <crypto/internal/rsa-common.h>
#include <linux/module.h>
#include <linux/random.h>

typedef int (*rsa_akcipher_complete_cb)(struct akcipher_request *, int);
static void rsapad_akcipher_req_complete(struct crypto_async_request *child_async_req,
					 int err, rsa_akcipher_complete_cb cb)
{
	struct akcipher_request *req = child_async_req->data;
	struct crypto_async_request async_req;

	if (err == -EINPROGRESS)
		return;

	async_req.data = req->base.data;
	async_req.tfm = crypto_akcipher_tfm(crypto_akcipher_reqtfm(req));
	async_req.flags = child_async_req->flags;
	req->base.complete(&async_req, cb(req, err));
}

static void rsapad_akcipher_setup_child(struct akcipher_request *req,
					struct scatterlist *src_sg,
					struct scatterlist *dst_sg,
					unsigned int src_len,
					unsigned int dst_len,
					crypto_completion_t cb)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct rsapad_tfm_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct rsapad_akciper_req_ctx *req_ctx = akcipher_request_ctx(req);

	akcipher_request_set_tfm(&req_ctx->child_req, ctx->child);
	akcipher_request_set_callback(&req_ctx->child_req, req->base.flags, cb, req);
	akcipher_request_set_crypt(&req_ctx->child_req, src_sg, dst_sg, src_len, dst_len);
}

static int pkcs1pad_encrypt_sign_complete(struct akcipher_request *req, int err)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct rsapad_tfm_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct rsapad_akciper_req_ctx *req_ctx = akcipher_request_ctx(req);
	unsigned int pad_len;
	unsigned int len;
	u8 *out_buf;

	if (err)
		goto out;

	len = req_ctx->child_req.dst_len;
	pad_len = ctx->key_size - len;

	/* Four billion to one */
	if (likely(!pad_len))
		goto out;

	out_buf = kzalloc(ctx->key_size, GFP_KERNEL);
	err = -ENOMEM;
	if (!out_buf)
		goto out;

	sg_copy_to_buffer(req->dst, sg_nents_for_len(req->dst, len),
			  out_buf + pad_len, len);
	sg_copy_from_buffer(req->dst,
			    sg_nents_for_len(req->dst, ctx->key_size),
			    out_buf, ctx->key_size);
	kfree_sensitive(out_buf);

out:
	req->dst_len = ctx->key_size;

	kfree(req_ctx->in_buf);

	return err;
}

static void pkcs1pad_encrypt_sign_complete_cb(
		struct crypto_async_request *child_async_req, int err)
{
	rsapad_akcipher_req_complete(child_async_req, err,
				     pkcs1pad_encrypt_sign_complete);
}

static int pkcs1pad_encrypt(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct rsapad_tfm_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct rsapad_akciper_req_ctx *req_ctx = akcipher_request_ctx(req);
	int err;
	unsigned int i, ps_end;

	if (!ctx->key_size)
		return -EINVAL;

	if (req->src_len > ctx->key_size - 11)
		return -EOVERFLOW;

	if (req->dst_len < ctx->key_size) {
		req->dst_len = ctx->key_size;
		return -EOVERFLOW;
	}

	req_ctx->in_buf = kmalloc(ctx->key_size - 1 - req->src_len,
				  GFP_KERNEL);
	if (!req_ctx->in_buf)
		return -ENOMEM;

	ps_end = ctx->key_size - req->src_len - 2;
	req_ctx->in_buf[0] = 0x02;
	for (i = 1; i < ps_end; i++)
		req_ctx->in_buf[i] = 1 + prandom_u32_max(255);
	req_ctx->in_buf[ps_end] = 0x00;

	rsapad_akcipher_sg_set_buf(req_ctx->in_sg, req_ctx->in_buf,
			ctx->key_size - 1 - req->src_len, req->src);

	/* Reuse output buffer */
	rsapad_akcipher_setup_child(req, req_ctx->in_sg, req->dst,
				    ctx->key_size - 1, req->dst_len,
				    pkcs1pad_encrypt_sign_complete_cb);

	err = crypto_akcipher_encrypt(&req_ctx->child_req);
	if (err != -EINPROGRESS && err != -EBUSY)
		return pkcs1pad_encrypt_sign_complete(req, err);

	return err;
}

static int pkcs1pad_decrypt_complete(struct akcipher_request *req, int err)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct rsapad_tfm_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct rsapad_akciper_req_ctx *req_ctx = akcipher_request_ctx(req);
	unsigned int dst_len;
	unsigned int pos;
	u8 *out_buf;

	if (err)
		goto done;

	err = -EINVAL;
	dst_len = req_ctx->child_req.dst_len;
	if (dst_len < ctx->key_size - 1)
		goto done;

	out_buf = req_ctx->out_buf;
	if (dst_len == ctx->key_size) {
		if (out_buf[0] != 0x00)
			/* Decrypted value had no leading 0 byte */
			goto done;

		dst_len--;
		out_buf++;
	}

	if (out_buf[0] != 0x02)
		goto done;

	for (pos = 1; pos < dst_len; pos++)
		if (out_buf[pos] == 0x00)
			break;
	if (pos < 9 || pos == dst_len)
		goto done;
	pos++;

	err = 0;

	if (req->dst_len < dst_len - pos)
		err = -EOVERFLOW;
	req->dst_len = dst_len - pos;

	if (!err)
		sg_copy_from_buffer(req->dst,
				sg_nents_for_len(req->dst, req->dst_len),
				out_buf + pos, req->dst_len);

done:
	kfree_sensitive(req_ctx->out_buf);

	return err;
}

static void pkcs1pad_decrypt_complete_cb(
		struct crypto_async_request *child_async_req, int err)
{
	rsapad_akcipher_req_complete(child_async_req, err, pkcs1pad_decrypt_complete);
}

static int pkcs1pad_decrypt(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct rsapad_tfm_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct rsapad_akciper_req_ctx *req_ctx = akcipher_request_ctx(req);
	int err;

	if (!ctx->key_size || req->src_len != ctx->key_size)
		return -EINVAL;

	req_ctx->out_buf = kmalloc(ctx->key_size, GFP_KERNEL);
	if (!req_ctx->out_buf)
		return -ENOMEM;

	rsapad_akcipher_sg_set_buf(req_ctx->out_sg, req_ctx->out_buf,
			    ctx->key_size, NULL);

	/* Reuse input buffer, output to a new buffer */
	rsapad_akcipher_setup_child(req, req->src, req_ctx->out_sg,
				    req->src_len, ctx->key_size,
				    pkcs1pad_decrypt_complete_cb);

	err = crypto_akcipher_decrypt(&req_ctx->child_req);
	if (err != -EINPROGRESS && err != -EBUSY)
		return pkcs1pad_decrypt_complete(req, err);

	return err;
}

static int pkcs1pad_sign(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct rsapad_tfm_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct rsapad_akciper_req_ctx *req_ctx = akcipher_request_ctx(req);
	struct akcipher_instance *inst = akcipher_alg_instance(tfm);
	struct rsapad_inst_ctx *ictx = akcipher_instance_ctx(inst);
	const struct rsa_asn1_template *digest_info = ictx->digest_info;
	int err;
	unsigned int ps_end, digest_size = 0;

	if (!ctx->key_size)
		return -EINVAL;

	if (digest_info)
		digest_size = digest_info->size;

	if (req->src_len + digest_size > ctx->key_size - 11)
		return -EOVERFLOW;

	if (req->dst_len < ctx->key_size) {
		req->dst_len = ctx->key_size;
		return -EOVERFLOW;
	}

	req_ctx->in_buf = kmalloc(ctx->key_size - 1 - req->src_len,
				  GFP_KERNEL);
	if (!req_ctx->in_buf)
		return -ENOMEM;

	ps_end = ctx->key_size - digest_size - req->src_len - 2;
	req_ctx->in_buf[0] = 0x01;
	memset(req_ctx->in_buf + 1, 0xff, ps_end - 1);
	req_ctx->in_buf[ps_end] = 0x00;

	if (digest_info)
		memcpy(req_ctx->in_buf + ps_end + 1, digest_info->data,
		       digest_info->size);

	rsapad_akcipher_sg_set_buf(req_ctx->in_sg, req_ctx->in_buf,
			ctx->key_size - 1 - req->src_len, req->src);

	/* Reuse output buffer */
	rsapad_akcipher_setup_child(req, req_ctx->in_sg, req->dst,
				    ctx->key_size - 1, req->dst_len,
				    pkcs1pad_encrypt_sign_complete_cb);

	err = crypto_akcipher_decrypt(&req_ctx->child_req);
	if (err != -EINPROGRESS && err != -EBUSY)
		return pkcs1pad_encrypt_sign_complete(req, err);

	return err;
}

static int pkcs1pad_verify_complete(struct akcipher_request *req, int err)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct rsapad_tfm_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct rsapad_akciper_req_ctx *req_ctx = akcipher_request_ctx(req);
	struct akcipher_instance *inst = akcipher_alg_instance(tfm);
	struct rsapad_inst_ctx *ictx = akcipher_instance_ctx(inst);
	const struct rsa_asn1_template *digest_info = ictx->digest_info;
	unsigned int dst_len;
	unsigned int pos;
	u8 *out_buf;

	if (err)
		goto done;

	err = -EINVAL;
	dst_len = req_ctx->child_req.dst_len;
	if (dst_len < ctx->key_size - 1)
		goto done;

	out_buf = req_ctx->out_buf;
	if (dst_len == ctx->key_size) {
		if (out_buf[0] != 0x00)
			/* Decrypted value had no leading 0 byte */
			goto done;

		dst_len--;
		out_buf++;
	}

	err = -EBADMSG;
	if (out_buf[0] != 0x01)
		goto done;

	for (pos = 1; pos < dst_len; pos++)
		if (out_buf[pos] != 0xff)
			break;

	if (pos < 9 || pos == dst_len || out_buf[pos] != 0x00)
		goto done;
	pos++;

	if (digest_info) {
		if (crypto_memneq(out_buf + pos, digest_info->data,
				  digest_info->size))
			goto done;

		pos += digest_info->size;
	}

	err = 0;

	if (req->dst_len != dst_len - pos) {
		err = -EKEYREJECTED;
		req->dst_len = dst_len - pos;
		goto done;
	}
	/* Extract appended digest. */
	sg_pcopy_to_buffer(req->src,
			   sg_nents_for_len(req->src,
					    req->src_len + req->dst_len),
			   req_ctx->out_buf + ctx->key_size,
			   req->dst_len, ctx->key_size);
	/* Do the actual verification step. */
	if (memcmp(req_ctx->out_buf + ctx->key_size, out_buf + pos,
		   req->dst_len) != 0)
		err = -EKEYREJECTED;
done:
	kfree_sensitive(req_ctx->out_buf);

	return err;
}

static void pkcs1pad_verify_complete_cb(
		struct crypto_async_request *child_async_req, int err)
{
	rsapad_akcipher_req_complete(child_async_req, err,
				     pkcs1pad_verify_complete);
}

/*
 * The verify operation is here for completeness similar to the verification
 * defined in RFC2313 section 10.2 except that block type 0 is not accepted,
 * as in RFC2437.  RFC2437 section 9.2 doesn't define any operation to
 * retrieve the DigestInfo from a signature, instead the user is expected
 * to call the sign operation to generate the expected signature and compare
 * signatures instead of the message-digests.
 */
static int pkcs1pad_verify(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct rsapad_tfm_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct rsapad_akciper_req_ctx *req_ctx = akcipher_request_ctx(req);
	int err;

	if (WARN_ON(req->dst) ||
	    WARN_ON(!req->dst_len) ||
	    !ctx->key_size || req->src_len < ctx->key_size)
		return -EINVAL;

	req_ctx->out_buf = kmalloc(ctx->key_size + req->dst_len, GFP_KERNEL);
	if (!req_ctx->out_buf)
		return -ENOMEM;

	rsapad_akcipher_sg_set_buf(req_ctx->out_sg, req_ctx->out_buf,
			    ctx->key_size, NULL);

	/* Reuse input buffer, output to a new buffer */
	rsapad_akcipher_setup_child(req, req->src, req_ctx->out_sg,
				    req->src_len, ctx->key_size,
				    pkcs1pad_verify_complete_cb);

	err = crypto_akcipher_encrypt(&req_ctx->child_req);
	if (err != -EINPROGRESS && err != -EBUSY)
		return pkcs1pad_verify_complete(req, err);

	return err;
}

static struct akcipher_alg pkcs1pad_alg = {
	.init = rsapad_akcipher_init_tfm,
	.exit = rsapad_akcipher_exit_tfm,

	.encrypt = pkcs1pad_encrypt,
	.decrypt = pkcs1pad_decrypt,
	.sign = pkcs1pad_sign,
	.verify = pkcs1pad_verify,
	.set_pub_key = rsapad_set_pub_key,
	.set_priv_key = rsapad_set_priv_key,
	.max_size = rsapad_get_max_size
};

static int pkcs1pad_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	return rsapad_akcipher_create(tmpl, tb, &pkcs1pad_alg);
}

struct crypto_template rsa_pkcs1pad_tmpl = {
	.name = "pkcs1pad",
	.create = pkcs1pad_create,
	.module = THIS_MODULE,
};
