// SPDX-License-Identifier: GPL-2.0

#define dev_fmt(fmt) "nvidia_smmu_cmdqv: " fmt

#include <linux/acpi.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/iommu.h>
#include <linux/iopoll.h>
#include <linux/platform_device.h>

#include <acpi/acpixf.h>

#include "arm-smmu-v3.h"

#define NVIDIA_SMMU_CMDQV_HID		"NVDA0600"

/* CMDQV register page base and size defines */
#define NVIDIA_CMDQV_CONFIG_BASE	(0)
#define NVIDIA_CMDQV_CONFIG_SIZE	(SZ_64K)
#define NVIDIA_VCMDQ_BASE		(0 + SZ_64K)
#define NVIDIA_VCMDQ_SIZE		(SZ_64K * 2) /* PAGE0 and PAGE1 */

/* CMDQV global config regs */
#define NVIDIA_CMDQV_CONFIG		0x0000
#define  CMDQV_EN			BIT(0)

#define NVIDIA_CMDQV_PARAM		0x0004
#define  CMDQV_NUM_VINTF_LOG2		GENMASK(11, 8)
#define  CMDQV_NUM_VCMDQ_LOG2		GENMASK(7, 4)

#define NVIDIA_CMDQV_STATUS		0x0008
#define  CMDQV_STATUS			GENMASK(2, 1)
#define  CMDQV_ENABLED			BIT(0)

#define NVIDIA_CMDQV_VINTF_ERR_MAP	0x000C
#define NVIDIA_CMDQV_VINTF_INT_MASK	0x0014
#define NVIDIA_CMDQV_VCMDQ_ERR_MAP	0x001C

#define NVIDIA_CMDQV_CMDQ_ALLOC(q)	(0x0200 + 0x4*(q))
#define  CMDQV_CMDQ_ALLOC_VINTF		GENMASK(20, 15)
#define  CMDQV_CMDQ_ALLOC_LVCMDQ	GENMASK(7, 1)
#define  CMDQV_CMDQ_ALLOCATED		BIT(0)

/* VINTF config regs */
#define NVIDIA_CMDQV_VINTF(v)		(0x1000 + 0x100*(v))

#define NVIDIA_VINTF_CONFIG		0x0000
#define  VINTF_HYP_OWN			BIT(17)
#define  VINTF_VMID			GENMASK(16, 1)
#define  VINTF_EN			BIT(0)

#define NVIDIA_VINTF_STATUS		0x0004
#define  VINTF_STATUS			GENMASK(3, 1)
#define  VINTF_ENABLED			BIT(0)

/* VCMDQ config regs */
/* -- PAGE0 -- */
#define NVIDIA_CMDQV_VCMDQ(q)		(NVIDIA_VCMDQ_BASE + 0x80*(q))

#define NVIDIA_VCMDQ_CONS		0x00000
#define  VCMDQ_CONS_ERR			GENMASK(30, 24)

#define NVIDIA_VCMDQ_PROD		0x00004

#define NVIDIA_VCMDQ_CONFIG		0x00008
#define  VCMDQ_EN			BIT(0)

#define NVIDIA_VCMDQ_STATUS		0x0000C
#define  VCMDQ_ENABLED			BIT(0)

#define NVIDIA_VCMDQ_GERROR		0x00010
#define NVIDIA_VCMDQ_GERRORN		0x00014

/* -- PAGE1 -- */
#define NVIDIA_VCMDQ_BASE_L(q)		(NVIDIA_CMDQV_VCMDQ(q) + SZ_64K)
#define  VCMDQ_ADDR			GENMASK(63, 5)
#define  VCMDQ_LOG2SIZE			GENMASK(4, 0)

struct nvidia_smmu_vintf {
	u16			idx;
	u32			cfg;
	u32			status;

	void __iomem		*base;
	struct arm_smmu_cmdq	*vcmdqs;
};

struct nvidia_smmu {
	struct arm_smmu_device	smmu;

	struct device		*cmdqv_dev;
	void __iomem		*cmdqv_base;
	int			cmdqv_irq;

	/* CMDQV Hardware Params */
	u16			num_total_vintfs;
	u16			num_total_vcmdqs;
	u16			num_vcmdqs_per_vintf;

	/* CMDQV_VINTF(0) reserved for host kernel use */
	struct nvidia_smmu_vintf vintf0;
};

static irqreturn_t nvidia_smmu_cmdqv_isr(int irq, void *devid)
{
	struct nvidia_smmu *nsmmu = (struct nvidia_smmu *)devid;
	struct nvidia_smmu_vintf *vintf0 = &nsmmu->vintf0;
	u32 vintf_err_map[2];
	u32 vcmdq_err_map[4];

	vintf_err_map[0] = readl_relaxed(nsmmu->cmdqv_base + NVIDIA_CMDQV_VINTF_ERR_MAP);
	vintf_err_map[1] = readl_relaxed(nsmmu->cmdqv_base + NVIDIA_CMDQV_VINTF_ERR_MAP + 0x4);

	vcmdq_err_map[0] = readl_relaxed(nsmmu->cmdqv_base + NVIDIA_CMDQV_VCMDQ_ERR_MAP);
	vcmdq_err_map[1] = readl_relaxed(nsmmu->cmdqv_base + NVIDIA_CMDQV_VCMDQ_ERR_MAP + 0x4);
	vcmdq_err_map[2] = readl_relaxed(nsmmu->cmdqv_base + NVIDIA_CMDQV_VCMDQ_ERR_MAP + 0x8);
	vcmdq_err_map[3] = readl_relaxed(nsmmu->cmdqv_base + NVIDIA_CMDQV_VCMDQ_ERR_MAP + 0xC);

	dev_warn(nsmmu->cmdqv_dev,
		 "unexpected cmdqv error reported: vintf_map %08X %08X, vcmdq_map %08X %08X %08X %08X\n",
		 vintf_err_map[0], vintf_err_map[1], vcmdq_err_map[0], vcmdq_err_map[1],
		 vcmdq_err_map[2], vcmdq_err_map[3]);

	/* If the error was reported by vintf0, avoid using any of its VCMDQs */
	if (vintf_err_map[vintf0->idx / 32] & (1 << (vintf0->idx % 32))) {
		vintf0->status = readl_relaxed(vintf0->base + NVIDIA_VINTF_STATUS);

		dev_warn(nsmmu->cmdqv_dev, "error (0x%lX) reported by host vintf0 - disabling its vcmdqs\n",
			 FIELD_GET(VINTF_STATUS, vintf0->status));
	} else if (vintf_err_map[0] || vintf_err_map[1]) {
		dev_err(nsmmu->cmdqv_dev, "cmdqv error interrupt triggered by unassigned vintf!\n");
	}

	return IRQ_HANDLED;
}

/* Adapt struct arm_smmu_cmdq init sequences from arm-smmu-v3.c for VCMDQs */
static int nvidia_smmu_init_one_arm_smmu_cmdq(struct nvidia_smmu *nsmmu,
					      struct arm_smmu_cmdq *cmdq,
					      void __iomem *vcmdq_base,
					      u16 qidx)
{
	struct arm_smmu_queue *q = &cmdq->q;
	size_t qsz;

	/* struct arm_smmu_cmdq config normally done in arm_smmu_device_hw_probe() */
	q->llq.max_n_shift = ilog2(SZ_64K >> CMDQ_ENT_SZ_SHIFT);

	/* struct arm_smmu_cmdq config normally done in arm_smmu_init_one_queue() */
	qsz = (1 << q->llq.max_n_shift) << CMDQ_ENT_SZ_SHIFT;
	q->base = dmam_alloc_coherent(nsmmu->cmdqv_dev, qsz, &q->base_dma, GFP_KERNEL);
	if (!q->base) {
		dev_err(nsmmu->cmdqv_dev, "failed to allocate 0x%zX bytes for VCMDQ%u\n",
			qsz, qidx);
		return -ENOMEM;
	}
	dev_dbg(nsmmu->cmdqv_dev, "allocated %u entries for VCMDQ%u @ 0x%llX [%pad] ++ %zX",
		1 << q->llq.max_n_shift, qidx, (u64)q->base, &q->base_dma, qsz);

	q->prod_reg = vcmdq_base + NVIDIA_VCMDQ_PROD;
	q->cons_reg = vcmdq_base + NVIDIA_VCMDQ_CONS;
	q->ent_dwords = CMDQ_ENT_DWORDS;

	q->q_base  = q->base_dma & VCMDQ_ADDR;
	q->q_base |= FIELD_PREP(VCMDQ_LOG2SIZE, q->llq.max_n_shift);

	q->llq.prod = q->llq.cons = 0;

	/* struct arm_smmu_cmdq config normally done in arm_smmu_cmdq_init() */
	atomic_set(&cmdq->owner_prod, 0);
	atomic_set(&cmdq->lock, 0);

	cmdq->valid_map = (atomic_long_t *)bitmap_zalloc(1 << q->llq.max_n_shift, GFP_KERNEL);
	if (!cmdq->valid_map) {
		dev_err(nsmmu->cmdqv_dev, "failed to allocate valid_map for VCMDQ%u\n", qidx);
		return -ENOMEM;
	}

	return 0;
}

static int nvidia_smmu_cmdqv_init(struct nvidia_smmu *nsmmu)
{
	struct nvidia_smmu_vintf *vintf0 = &nsmmu->vintf0;
	u32 regval;
	u16 qidx;
	int ret;

	/* Setup vintf0 for host kernel */
	vintf0->idx = 0;
	vintf0->base = nsmmu->cmdqv_base + NVIDIA_CMDQV_VINTF(0);

	regval = FIELD_PREP(VINTF_HYP_OWN, nsmmu->num_total_vintfs > 1);
	writel_relaxed(regval, vintf0->base + NVIDIA_VINTF_CONFIG);

	regval |= FIELD_PREP(VINTF_EN, 1);
	writel_relaxed(regval, vintf0->base + NVIDIA_VINTF_CONFIG);

	vintf0->cfg = regval;

	ret = readl_relaxed_poll_timeout(vintf0->base + NVIDIA_VINTF_STATUS,
					 regval, regval == VINTF_ENABLED,
					 1, ARM_SMMU_POLL_TIMEOUT_US);
	vintf0->status = regval;
	if (ret) {
		dev_err(nsmmu->cmdqv_dev, "failed to enable VINTF%u: STATUS = 0x%08X\n",
			vintf0->idx, regval);
		return ret;
	}

	/* Allocate vcmdqs to vintf0 */
	for (qidx = 0; qidx < nsmmu->num_vcmdqs_per_vintf; qidx++) {
		regval  = FIELD_PREP(CMDQV_CMDQ_ALLOC_VINTF, vintf0->idx);
		regval |= FIELD_PREP(CMDQV_CMDQ_ALLOC_LVCMDQ, qidx);
		regval |= CMDQV_CMDQ_ALLOCATED;
		writel_relaxed(regval, nsmmu->cmdqv_base + NVIDIA_CMDQV_CMDQ_ALLOC(qidx));
	}

	/* Build an arm_smmu_cmdq for each vcmdq allocated to vintf0 */
	vintf0->vcmdqs = devm_kcalloc(nsmmu->cmdqv_dev, nsmmu->num_vcmdqs_per_vintf,
				      sizeof(*vintf0->vcmdqs), GFP_KERNEL);
	if (!vintf0->vcmdqs)
		return -ENOMEM;

	for (qidx = 0; qidx < nsmmu->num_vcmdqs_per_vintf; qidx++) {
		void __iomem *vcmdq_base = nsmmu->cmdqv_base + NVIDIA_CMDQV_VCMDQ(qidx);
		struct arm_smmu_cmdq *cmdq = &vintf0->vcmdqs[qidx];

		/* Setup struct arm_smmu_cmdq data members */
		nvidia_smmu_init_one_arm_smmu_cmdq(nsmmu, cmdq, vcmdq_base, qidx);

		/* Configure and enable the vcmdq */
		writel_relaxed(0, vcmdq_base + NVIDIA_VCMDQ_PROD);
		writel_relaxed(0, vcmdq_base + NVIDIA_VCMDQ_CONS);

		writeq_relaxed(cmdq->q.q_base, nsmmu->cmdqv_base + NVIDIA_VCMDQ_BASE_L(qidx));

		writel_relaxed(VCMDQ_EN, vcmdq_base + NVIDIA_VCMDQ_CONFIG);
		ret = readl_poll_timeout(vcmdq_base + NVIDIA_VCMDQ_STATUS,
					 regval, regval == VCMDQ_ENABLED,
					 1, ARM_SMMU_POLL_TIMEOUT_US);
		if (ret) {
			u32 gerror = readl_relaxed(vcmdq_base + NVIDIA_VCMDQ_GERROR);
			u32 gerrorn = readl_relaxed(vcmdq_base + NVIDIA_VCMDQ_GERRORN);
			u32 cons = readl_relaxed(vcmdq_base + NVIDIA_VCMDQ_CONS);

			dev_err(nsmmu->cmdqv_dev,
				"failed to enable VCMDQ%u: GERROR=0x%X, GERRORN=0x%X, CONS=0x%X\n",
				qidx, gerror, gerrorn, cons);
			return ret;
		}

		dev_info(nsmmu->cmdqv_dev, "VCMDQ%u allocated to VINTF%u as logical-VCMDQ%u\n",
			 qidx, vintf0->idx, qidx);
	}

	return 0;
}

static int nvidia_smmu_probe(struct nvidia_smmu *nsmmu)
{
	struct platform_device *cmdqv_pdev = to_platform_device(nsmmu->cmdqv_dev);
	struct resource *res;
	u32 regval;

	/* Base address */
	res = platform_get_resource(cmdqv_pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENXIO;

	nsmmu->cmdqv_base = devm_ioremap_resource(nsmmu->cmdqv_dev, res);
	if (IS_ERR(nsmmu->cmdqv_base))
		return PTR_ERR(nsmmu->cmdqv_base);

	/* Interrupt */
	nsmmu->cmdqv_irq = platform_get_irq(cmdqv_pdev, 0);
	if (nsmmu->cmdqv_irq < 0) {
		dev_warn(nsmmu->cmdqv_dev, "no cmdqv interrupt - errors will not be reported\n");
		nsmmu->cmdqv_irq = 0;
	}

	/* Probe the h/w */
	regval = readl_relaxed(nsmmu->cmdqv_base + NVIDIA_CMDQV_CONFIG);
	if (!FIELD_GET(CMDQV_EN, regval)) {
		dev_err(nsmmu->cmdqv_dev, "CMDQV h/w is disabled: CMDQV_CONFIG=0x%08X\n", regval);
		return -ENODEV;
	}

	regval = readl_relaxed(nsmmu->cmdqv_base + NVIDIA_CMDQV_STATUS);
	if (!FIELD_GET(CMDQV_ENABLED, regval) || FIELD_GET(CMDQV_STATUS, regval)) {
		dev_err(nsmmu->cmdqv_dev, "CMDQV h/w not ready: CMDQV_STATUS=0x%08X\n", regval);
		return -ENODEV;
	}

	regval = readl_relaxed(nsmmu->cmdqv_base + NVIDIA_CMDQV_PARAM);
	nsmmu->num_total_vintfs = 1 << FIELD_GET(CMDQV_NUM_VINTF_LOG2, regval);
	nsmmu->num_total_vcmdqs = 1 << FIELD_GET(CMDQV_NUM_VCMDQ_LOG2, regval);
	nsmmu->num_vcmdqs_per_vintf = nsmmu->num_total_vcmdqs / nsmmu->num_total_vintfs;

	return 0;
}

static struct arm_smmu_cmdq *nvidia_smmu_get_cmdq(struct arm_smmu_device *smmu, u64 *cmds, int n)
{
	struct nvidia_smmu *nsmmu = (struct nvidia_smmu *)smmu;
	struct nvidia_smmu_vintf *vintf0 = &nsmmu->vintf0;
	u16 qidx;

	/* Make sure vintf0 is enabled and healthy */
	if (vintf0->status != VINTF_ENABLED)
		return &smmu->cmdq;

	/* Check for illegal CMDs */
	if (!FIELD_GET(VINTF_HYP_OWN, vintf0->cfg)) {
		u64 opcode = (n) ? FIELD_GET(CMDQ_0_OP, cmds[0]) : CMDQ_OP_CMD_SYNC;

		/* List all non-illegal CMDs for cmdq overriding */
		switch (opcode) {
		case CMDQ_OP_TLBI_NH_ASID:
		case CMDQ_OP_TLBI_NH_VA:
		case CMDQ_OP_TLBI_S12_VMALL:
		case CMDQ_OP_TLBI_S2_IPA:
		case CMDQ_OP_ATC_INV:
			break;
		default:
			/* Skip overriding for illegal CMDs */
			return &smmu->cmdq;
		}
	}

	/*
	 * Select a vcmdq to use. Here we use a temporal solution to
	 * balance out traffic on cmdq issuing: each cmdq has its own
	 * lock, if all cpus issue cmdlist using the same cmdq, only
	 * one CPU at a time can enter the process, while the others
	 * will be spinning at the same lock.
	 */
	qidx = smp_processor_id() % nsmmu->num_vcmdqs_per_vintf;
	return &vintf0->vcmdqs[qidx];
}

static int nvidia_smmu_device_reset(struct arm_smmu_device *smmu)
{
	struct nvidia_smmu *nsmmu = (struct nvidia_smmu *)smmu;
	int ret;

	ret = nvidia_smmu_cmdqv_init(nsmmu);
	if (ret)
		return ret;

	if (nsmmu->cmdqv_irq) {
		ret = devm_request_irq(nsmmu->cmdqv_dev, nsmmu->cmdqv_irq, nvidia_smmu_cmdqv_isr,
				       IRQF_SHARED, "nvidia-smmu-cmdqv", nsmmu);
		if (ret) {
			dev_err(nsmmu->cmdqv_dev, "failed to claim irq (%d): %d\n",
				nsmmu->cmdqv_irq, ret);
			return ret;
		}
	}

	/* Disable FEAT_MSI and OPT_MSIPOLL since VCMDQs only support CMD_SYNC w/CS_NONE */
	smmu->features &= ~ARM_SMMU_FEAT_MSI;
	smmu->options &= ~ARM_SMMU_OPT_MSIPOLL;

	return 0;
}

const struct arm_smmu_impl nvidia_smmu_impl = {
	.device_reset = nvidia_smmu_device_reset,
	.get_cmdq = nvidia_smmu_get_cmdq,
};

#ifdef CONFIG_ACPI
struct nvidia_smmu *nvidia_smmu_create(struct arm_smmu_device *smmu)
{
	struct nvidia_smmu *nsmmu = NULL;
	struct acpi_iort_node *node;
	struct acpi_device *adev;
	struct device *cmdqv_dev;
	const char *match_uid;

	if (acpi_disabled)
		return NULL;

	/* Look for a device in the DSDT whose _UID matches the SMMU's iort_node identifier */
	node = *(struct acpi_iort_node **)dev_get_platdata(smmu->dev);
	match_uid = kasprintf(GFP_KERNEL, "%u", node->identifier);
	adev = acpi_dev_get_first_match_dev(NVIDIA_SMMU_CMDQV_HID, match_uid, -1);
	kfree(match_uid);

	if (!adev)
		return NULL;

	cmdqv_dev = bus_find_device_by_acpi_dev(&platform_bus_type, adev);
	if (!cmdqv_dev)
		return NULL;

	dev_info(smmu->dev, "found companion CMDQV device, %s", dev_name(cmdqv_dev));

	nsmmu = devm_krealloc(smmu->dev, smmu, sizeof(*nsmmu), GFP_KERNEL);
	if (!nsmmu)
		return ERR_PTR(-ENOMEM);

	nsmmu->cmdqv_dev = cmdqv_dev;

	return nsmmu;
}
#else
struct nvidia_smmu *nvidia_smmu_create(struct arm_smmu_device *smmu)
{
	return NULL;
}
#endif

struct arm_smmu_device *nvidia_smmu_v3_impl_init(struct arm_smmu_device *smmu)
{
	struct nvidia_smmu *nsmmu;
	int ret;

	nsmmu = nvidia_smmu_create(smmu);
	if (!nsmmu)
		return smmu;

	ret = nvidia_smmu_probe(nsmmu);
	if (ret)
		return ERR_PTR(ret);

	nsmmu->smmu.impl = &nvidia_smmu_impl;

	return &nsmmu->smmu;
}
