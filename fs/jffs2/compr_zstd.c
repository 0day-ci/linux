
#include <linux/zstd.h>
#include "compr.h"

#define ZSTD_DEF_LEVEL	3

static ZSTD_CCtx *cctx;
static ZSTD_DCtx *dctx;
static void *cwksp;
static void *dwksp;

static ZSTD_parameters zstd_params(void)
{
	return ZSTD_getParams(ZSTD_DEF_LEVEL, 0, 0);
}

static int zstd_comp_init(void)
{
	int ret = 0;
	const ZSTD_parameters params = zstd_params();
	const size_t wksp_size = ZSTD_CCtxWorkspaceBound(params.cParams);

	cwksp = vzalloc(wksp_size);
	if (!cwksp) {
		ret = -ENOMEM;
		goto out;
	}

	cctx = ZSTD_initCCtx(cwksp, wksp_size);
	if (!cctx) {
		ret = -EINVAL;
		goto out_free;
	}
out:
	return ret;
out_free:
	vfree(cwksp);
	goto out;
}

static int zstd_decomp_init(void)
{
	int ret = 0;
	const size_t wksp_size = ZSTD_DCtxWorkspaceBound();

	dwksp = vzalloc(wksp_size);
	if (!dwksp) {
		ret = -ENOMEM;
		goto out;
	}

	dctx = ZSTD_initDCtx(dwksp, wksp_size);
	if (!dctx) {
		ret = -EINVAL;
		goto out_free;
	}
out:
	return ret;
out_free:
	vfree(dwksp);
	goto out;
}

static void zstd_comp_exit(void)
{
	vfree(cwksp);
	cwksp = NULL;
	cctx = NULL;
}

static void zstd_decomp_exit(void)
{
	vfree(dwksp);
	dwksp = NULL;
	dctx = NULL;
}

static int jffs2_zstd_compress(unsigned char *data_in, unsigned char *cpage_out,
			      uint32_t *sourcelen, uint32_t *dstlen)
{
	size_t out_len;
	const ZSTD_parameters params = zstd_params();

	out_len = ZSTD_compressCCtx(cctx, cpage_out, *dstlen, data_in, *sourcelen, params);
	if (ZSTD_isError(out_len) || out_len > *dstlen)
		return -1;
	*dstlen = out_len;
	return 0;
}

static int jffs2_zstd_decompress(unsigned char *data_in, unsigned char *cpage_out,
				 uint32_t srclen, uint32_t destlen)
{
	size_t out_len;

	out_len = ZSTD_decompressDCtx(dctx, cpage_out, destlen, data_in, srclen);
	if (ZSTD_isError(out_len) || out_len != destlen)
		return -1;

	return 0;
}

static struct jffs2_compressor jffs2_zstd_comp = {
	.priority = JFFS2_ZSTD_PRIORITY,
	.name = "zstd",
	.compr = JFFS2_COMPR_ZSTD,
	.compress = &jffs2_zstd_compress,
	.decompress = &jffs2_zstd_decompress,
	.disabled = 0,
};

int __init jffs2_zstd_init(void)
{
	int ret;

	ret = zstd_comp_init();
	if (ret)
		return ret;
	ret = zstd_decomp_init();
	if (ret)
		zstd_comp_exit();
	ret = jffs2_register_compressor(&jffs2_zstd_comp);
	return ret;
}

void jffs2_zstd_exit(void)
{
	jffs2_unregister_compressor(&jffs2_zstd_comp);
	zstd_comp_exit();
	zstd_decomp_exit();
}
