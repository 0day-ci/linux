/*
 * Copyright (C) 2021 Emmanuel Gil Peyrot <linkmauve@linkmauve.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/crypto.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <crypto/aes.h>
#include <crypto/internal/skcipher.h>

#include <linux/io.h>
#include <linux/delay.h>

/* Addresses of the registers */
#define AES_CTRL 0
#define AES_SRC  4
#define AES_DEST 8
#define AES_KEY  12
#define AES_IV   16

#define AES_CTRL_EXEC       0x80000000
#define AES_CTRL_EXEC_RESET 0x00000000
#define AES_CTRL_EXEC_INIT  0x80000000
#define AES_CTRL_IRQ        0x40000000
#define AES_CTRL_ERR        0x20000000
#define AES_CTRL_ENA        0x10000000
#define AES_CTRL_DEC        0x08000000
#define AES_CTRL_IV         0x00001000
#define AES_CTRL_BLOCK      0x00000fff

#define OP_TIMEOUT 0x1000

#define AES_DIR_DECRYPT 0
#define AES_DIR_ENCRYPT 1

static void __iomem *base;
static spinlock_t lock;

/* Write a 128 bit field (either a writable key or IV) */
static inline void
writefield(u32 reg, const void *_value)
{
	const u32 *value = _value;
	int i;

	for (i = 0; i < 4; i++)
		iowrite32be(value[i], base + reg);
}

static int
do_crypt(const void *src, void *dst, u32 len, u32 flags)
{
	u32 blocks = ((len >> 4) - 1) & AES_CTRL_BLOCK;
	u32 status;
	u32 counter = OP_TIMEOUT;
	u32 i;

	/* Flush out all of src, we can’t know whether any of it is in cache */
	for (i = 0; i < len; i += 32)
		__asm__("dcbf 0, %0" : : "r" (src + i));
	__asm__("sync" : : : "memory");

	/* Set the addresses for DMA */
	iowrite32be(virt_to_phys((void *)src), base + AES_SRC);
	iowrite32be(virt_to_phys(dst), base + AES_DEST);

	/* Start the operation */
	iowrite32be(flags | blocks, base + AES_CTRL);

	/* TODO: figure out how to use interrupts here, this will probably
	 * lower throughput but let the CPU do other things while the AES
	 * engine is doing its work. */
	do {
		status = ioread32be(base + AES_CTRL);
		cpu_relax();
	} while ((status & AES_CTRL_EXEC) && --counter);

	/* Do we ever get called with dst ≠ src?  If so we have to invalidate
	 * dst in addition to the earlier flush of src. */
	if (unlikely(dst != src)) {
		for (i = 0; i < len; i += 32)
			__asm__("dcbi 0, %0" : : "r" (dst + i));
		__asm__("sync" : : : "memory");
	}

	return counter ? 0 : 1;
}

static void
nintendo_aes_crypt(const void *src, void *dst, u32 len, u8 *iv, int dir,
		   bool firstchunk)
{
	u32 flags = 0;
	unsigned long iflags;
	int ret;

	flags |= AES_CTRL_EXEC_INIT /* | AES_CTRL_IRQ */ | AES_CTRL_ENA;

	if (dir == AES_DIR_DECRYPT)
		flags |= AES_CTRL_DEC;

	if (!firstchunk)
		flags |= AES_CTRL_IV;

	/* Start the critical section */
	spin_lock_irqsave(&lock, iflags);

	if (firstchunk)
		writefield(AES_IV, iv);

	ret = do_crypt(src, dst, len, flags);
	BUG_ON(ret);

	spin_unlock_irqrestore(&lock, iflags);
}

static int nintendo_setkey_skcipher(struct crypto_skcipher *tfm, const u8 *key,
				    unsigned int len)
{
	/* The hardware only supports AES-128 */
	if (len != AES_KEYSIZE_128)
		return -EINVAL;

	writefield(AES_KEY, key);
	return 0;
}

static int nintendo_skcipher_crypt(struct skcipher_request *req, int dir)
{
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;
	char ivbuf[AES_BLOCK_SIZE];
	unsigned int ivsize;

	bool firstchunk = true;

	/* Reset the engine */
	iowrite32be(0, base + AES_CTRL);

	err = skcipher_walk_virt(&walk, req, false);
	ivsize = min(sizeof(ivbuf), walk.ivsize);

	while ((nbytes = walk.nbytes) != 0) {
		unsigned int chunkbytes = round_down(nbytes, AES_BLOCK_SIZE);
		unsigned int ret = nbytes % AES_BLOCK_SIZE;

		if (walk.total == chunkbytes && dir == AES_DIR_DECRYPT) {
			/* If this is the last chunk and we're decrypting, take
			 * note of the IV (which is the last ciphertext block)
			 */
			memcpy(ivbuf, walk.src.virt.addr + walk.total - ivsize,
			       ivsize);
		}

		nintendo_aes_crypt(walk.src.virt.addr, walk.dst.virt.addr,
				   chunkbytes, walk.iv, dir, firstchunk);

		if (walk.total == chunkbytes && dir == AES_DIR_ENCRYPT) {
			/* If this is the last chunk and we're encrypting, take
			 * note of the IV (which is the last ciphertext block)
			 */
			memcpy(walk.iv,
			       walk.dst.virt.addr + walk.total - ivsize,
			       ivsize);
		} else if (walk.total == chunkbytes && dir == AES_DIR_DECRYPT) {
			memcpy(walk.iv, ivbuf, ivsize);
		}

		err = skcipher_walk_done(&walk, ret);
		firstchunk = false;
	}

	return err;
}

static int nintendo_cbc_encrypt(struct skcipher_request *req)
{
	return nintendo_skcipher_crypt(req, AES_DIR_ENCRYPT);
}

static int nintendo_cbc_decrypt(struct skcipher_request *req)
{
	return nintendo_skcipher_crypt(req, AES_DIR_DECRYPT);
}

static struct skcipher_alg nintendo_alg = {
	.base.cra_name		= "cbc(aes)",
	.base.cra_driver_name	= "cbc-aes-nintendo",
	.base.cra_priority	= 400,
	.base.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY,
	.base.cra_blocksize	= AES_BLOCK_SIZE,
	.base.cra_alignmask	= 15,
	.base.cra_module	= THIS_MODULE,
	.setkey			= nintendo_setkey_skcipher,
	.encrypt		= nintendo_cbc_encrypt,
	.decrypt		= nintendo_cbc_decrypt,
	.min_keysize		= AES_KEYSIZE_128,
	.max_keysize		= AES_KEYSIZE_128,
	.ivsize			= AES_BLOCK_SIZE,
};

static int nintendo_aes_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	crypto_unregister_skcipher(&nintendo_alg);
	devm_iounmap(dev, base);
	base = NULL;

	return 0;
}

static int nintendo_aes_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	spin_lock_init(&lock);

	ret = crypto_register_skcipher(&nintendo_alg);
	if (ret)
		goto eiomap;

	dev_notice(dev, "Nintendo Wii and Wii U AES engine enabled\n");
	return 0;

 eiomap:
	devm_iounmap(dev, base);

	dev_err(dev, "Nintendo Wii and Wii U AES initialization failed\n");
	return ret;
}

static const struct of_device_id nintendo_aes_of_match[] = {
	{ .compatible = "nintendo,hollywood-aes", },
	{ .compatible = "nintendo,latte-aes", },
	{/* sentinel */},
};
MODULE_DEVICE_TABLE(of, nintendo_aes_of_match);

static struct platform_driver nintendo_aes_driver = {
	.driver = {
		.name = "nintendo-aes",
		.of_match_table = nintendo_aes_of_match,
	},
	.probe = nintendo_aes_probe,
	.remove = nintendo_aes_remove,
};

module_platform_driver(nintendo_aes_driver);

MODULE_AUTHOR("Emmanuel Gil Peyrot <linkmauve@linkmauve.fr>");
MODULE_DESCRIPTION("Nintendo Wii and Wii U Hardware AES driver");
MODULE_LICENSE("GPL");
