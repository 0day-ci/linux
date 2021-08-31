// SPDX-License-Identifier: GPL-2.0

#define dev_fmt(fmt) "nvidia_smmu_cmdqv: " fmt

#include <linux/acpi.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/iommu.h>
#include <linux/iopoll.h>
#include <linux/kvm_host.h>
#include <linux/mdev.h>
#include <linux/platform_device.h>
#include <linux/vfio.h>

#include <acpi/acpixf.h>

#include "arm-smmu-v3.h"

#define NVIDIA_SMMU_CMDQV_HID		"NVDA0600"

/* CMDQV register page base and size defines */
#define NVIDIA_CMDQV_CONFIG_BASE	(0)
#define NVIDIA_CMDQV_CONFIG_SIZE	(SZ_64K)
#define NVIDIA_VCMDQ_BASE		(0 + SZ_64K)
#define NVIDIA_VCMDQ_SIZE		(SZ_64K * 2) /* PAGE0 and PAGE1 */
#define NVIDIA_VINTF_VCMDQ_BASE		(NVIDIA_VCMDQ_BASE + NVIDIA_VCMDQ_SIZE)

/* CMDQV global config regs */
#define NVIDIA_CMDQV_CONFIG		0x0000
#define  CMDQV_EN			BIT(0)

#define NVIDIA_CMDQV_PARAM		0x0004
#define  CMDQV_NUM_SID_PER_VM_LOG2	GENMASK(15, 12)
#define  CMDQV_NUM_VINTF_LOG2		GENMASK(11, 8)
#define  CMDQV_NUM_VCMDQ_LOG2		GENMASK(7, 4)
#define  CMDQV_VER			GENMASK(3, 0)

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

#define NVIDIA_VINTFi_CONFIG(i)		(NVIDIA_CMDQV_VINTF(i) + NVIDIA_VINTF_CONFIG)
#define NVIDIA_VINTFi_STATUS(i)		(NVIDIA_CMDQV_VINTF(i) + NVIDIA_VINTF_STATUS)
#define NVIDIA_VINTFi_SID_MATCH(i, s)	(NVIDIA_CMDQV_VINTF(i) + NVIDIA_VINTF_SID_MATCH(s))
#define NVIDIA_VINTFi_SID_REPLACE(i, s)	(NVIDIA_CMDQV_VINTF(i) + NVIDIA_VINTF_SID_REPLACE(s))
#define NVIDIA_VINTFi_CMDQ_ERR_MAP(i)	(NVIDIA_CMDQV_VINTF(i) + NVIDIA_VINTF_CMDQ_ERR_MAP)

#define NVIDIA_VINTF_CONFIG		0x0000
#define  VINTF_HYP_OWN			BIT(17)
#define  VINTF_VMID			GENMASK(16, 1)
#define  VINTF_EN			BIT(0)

#define NVIDIA_VINTF_STATUS		0x0004
#define  VINTF_STATUS			GENMASK(3, 1)
#define  VINTF_ENABLED			BIT(0)

#define NVIDIA_VINTF_SID_MATCH(s)	(0x0040 + 0x4*(s))
#define NVIDIA_VINTF_SID_REPLACE(s)	(0x0080 + 0x4*(s))

#define NVIDIA_VINTF_CMDQ_ERR_MAP	0x00C0

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

#define NVIDIA_VCMDQ0_BASE_L		0x00000	/* offset to NVIDIA_VCMDQ_BASE_L(0) */
#define NVIDIA_VCMDQ0_BASE_H		0x00004	/* offset to NVIDIA_VCMDQ_BASE_L(0) */
#define NVIDIA_VCMDQ0_CONS_INDX_BASE_L	0x00008	/* offset to NVIDIA_VCMDQ_BASE_L(0) */
#define NVIDIA_VCMDQ0_CONS_INDX_BASE_H	0x0000C	/* offset to NVIDIA_VCMDQ_BASE_L(0) */

/* VINTF logical-VCMDQ regs */
#define NVIDIA_VINTFi_VCMDQ_BASE(i)	(NVIDIA_VINTF_VCMDQ_BASE + NVIDIA_VCMDQ_SIZE*(i))
#define NVIDIA_VINTFi_VCMDQ(i, q)	(NVIDIA_VINTFi_VCMDQ_BASE(i) + 0x80*(q))

struct nvidia_smmu_vintf {
	u16			idx;
	u16			vmid;
	u32			cfg;
	u32			status;

	void __iomem		*base;
	void __iomem		*vcmdq_base;
	struct arm_smmu_cmdq	*vcmdqs;

#define NVIDIA_SMMU_VINTF_MAX_SIDS 16
	DECLARE_BITMAP(sid_map, NVIDIA_SMMU_VINTF_MAX_SIDS);
	u32			sid_replace[NVIDIA_SMMU_VINTF_MAX_SIDS];

	spinlock_t		lock;
};

struct nvidia_smmu {
	struct arm_smmu_device	smmu;

	struct device		*cmdqv_dev;
	void __iomem		*cmdqv_base;
	resource_size_t		ioaddr;
	resource_size_t		ioaddr_size;
	int			cmdqv_irq;

	/* CMDQV Hardware Params */
	u16			num_total_vintfs;
	u16			num_total_vcmdqs;
	u16			num_vcmdqs_per_vintf;

#define NVIDIA_SMMU_MAX_VINTFS	(1 << 6)
	DECLARE_BITMAP(vintf_map, NVIDIA_SMMU_MAX_VINTFS);

	/* CMDQV_VINTF(0) reserved for host kernel use */
	struct nvidia_smmu_vintf vintf0;

	struct nvidia_smmu_vintf **vmid_mappings;

#ifdef CONFIG_VFIO_MDEV_DEVICE
	/* CMDQV_VINTFs exposed to userspace via mdev */
	struct nvidia_cmdqv_mdev **vintf_mdev;
	/* Cache for two 64-bit VCMDQ base addresses */
	struct nvidia_cmdqv_vcmdq_regcache {
		u64		base_addr;
		u64		cons_addr;
	} *vcmdq_regcache;
	struct mutex		mdev_lock;
	struct mutex		vmid_lock;
#endif
};

#ifdef CONFIG_VFIO_MDEV_DEVICE
struct nvidia_cmdqv_mdev {
	struct nvidia_smmu	*nsmmu;
	struct mdev_device	*mdev;
	struct nvidia_smmu_vintf *vintf;

	struct notifier_block	group_notifier;
	struct kvm		*kvm;
};
#endif

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

#ifdef CONFIG_VFIO_MDEV_DEVICE
struct mdev_parent_ops nvidia_smmu_cmdqv_mdev_ops;

int nvidia_smmu_cmdqv_mdev_init(struct nvidia_smmu *nsmmu)
{
	struct nvidia_cmdqv_mdev *cmdqv_mdev;
	int ret;

	/* Skip mdev init unless there are available VINTFs */
	if (nsmmu->num_total_vintfs <= 1)
		return 0;

	nsmmu->vintf_mdev = devm_kcalloc(nsmmu->cmdqv_dev, nsmmu->num_total_vintfs,
					 sizeof(*nsmmu->vintf_mdev), GFP_KERNEL);
	if (!nsmmu->vintf_mdev)
		return -ENOMEM;

	nsmmu->vcmdq_regcache = devm_kcalloc(nsmmu->cmdqv_dev, nsmmu->num_total_vcmdqs,
					     sizeof(*nsmmu->vcmdq_regcache), GFP_KERNEL);
	if (!nsmmu->vcmdq_regcache)
		return -ENOMEM;

	nsmmu->vmid_mappings = devm_kcalloc(nsmmu->cmdqv_dev, 1 << nsmmu->smmu.vmid_bits,
					    sizeof(*nsmmu->vmid_mappings), GFP_KERNEL);
	if (!nsmmu->vmid_mappings)
		return -ENOMEM;

	mutex_init(&nsmmu->mdev_lock);
	mutex_init(&nsmmu->vmid_lock);

	/* Add a dummy mdev instance to represent vintf0 */
	cmdqv_mdev = devm_kzalloc(nsmmu->cmdqv_dev, sizeof(*cmdqv_mdev), GFP_KERNEL);
	if (!cmdqv_mdev)
		return -ENOMEM;

	cmdqv_mdev->nsmmu = nsmmu;
	nsmmu->vintf_mdev[0] = cmdqv_mdev;

	ret = mdev_register_device(nsmmu->cmdqv_dev, &nvidia_smmu_cmdqv_mdev_ops);
	if (ret) {
		dev_err(nsmmu->cmdqv_dev, "failed to register mdev device: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(to_platform_device(nsmmu->cmdqv_dev), nsmmu);

	return ret;
}
#else
int nvidia_smmu_cmdqv_mdev_init(struct nvidia_smmu *nsmmu)
{
	return 0;
}
#endif

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

	/* Log this vintf0 in vintf_map */
	set_bit(0, nsmmu->vintf_map);

	spin_lock_init(&vintf0->lock);

#ifdef CONFIG_VFIO_MDEV_DEVICE
	if (nsmmu->vintf_mdev && nsmmu->vintf_mdev[0])
		nsmmu->vintf_mdev[0]->vintf = vintf0;
#endif

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

	nsmmu->ioaddr = res->start;
	nsmmu->ioaddr_size = resource_size(res);

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

static int nvidia_smmu_bitmap_alloc(unsigned long *map, int size)
{
	int idx;

	do {
		idx = find_first_zero_bit(map, size);
		if (idx == size)
			return -ENOSPC;
	} while (test_and_set_bit(idx, map));

	return idx;
}

static void nvidia_smmu_bitmap_free(unsigned long *map, int idx)
{
	clear_bit(idx, map);
}

static int nvidia_smmu_attach_dev(struct arm_smmu_domain *smmu_domain, struct device *dev)
{
	struct nvidia_smmu *nsmmu = (struct nvidia_smmu *)smmu_domain->smmu;
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct nvidia_smmu_vintf *vintf = &nsmmu->vintf0;
	int i, slot;

#ifdef CONFIG_VFIO_MDEV_DEVICE
	/* Repoint vintf to the corresponding one for Nested Translation mode */
	if (smmu_domain->stage == ARM_SMMU_DOMAIN_NESTED) {
		u16 vmid = smmu_domain->s2_cfg.vmid;

		mutex_lock(&nsmmu->vmid_lock);
		vintf = nsmmu->vmid_mappings[vmid];
		mutex_unlock(&nsmmu->vmid_lock);
		if (!vintf) {
			dev_err(nsmmu->cmdqv_dev, "failed to find vintf\n");
			return -EINVAL;
		}
	}
#endif

	for (i = 0; i < fwspec->num_ids; i++) {
		unsigned int sid = fwspec->ids[i];
		unsigned long flags;

		/* Find an empty slot of SID_MATCH and SID_REPLACE */
		slot = nvidia_smmu_bitmap_alloc(vintf->sid_map, NVIDIA_SMMU_VINTF_MAX_SIDS);
		if (slot < 0)
			return -EBUSY;

		/* Write PHY_SID to SID_REPLACE and cache it for quick lookup */
		writel_relaxed(sid, vintf->base + NVIDIA_VINTF_SID_REPLACE(slot));

		spin_lock_irqsave(&vintf->lock, flags);
		vintf->sid_replace[slot] = sid;
		spin_unlock_irqrestore(&vintf->lock, flags);

		if (smmu_domain->stage == ARM_SMMU_DOMAIN_NESTED) {
			struct iommu_group *group = iommu_group_get(dev);

			/*
			 * Mark SID_MATCH with iommu_group_id, without setting ENABLE bit
			 * This allows hypervisor to look up one SID_MATCH register that
			 * matches with the same iommu_group_id, and to eventually update
			 * VIRT_SID in SID_MATCH.
			 */
			writel_relaxed(iommu_group_id(group) << 1,
				       vintf->base + NVIDIA_VINTF_SID_MATCH(slot));
		}
	}

	return 0;
}

static void nvidia_smmu_detach_dev(struct arm_smmu_domain *smmu_domain, struct device *dev)
{
	struct nvidia_smmu *nsmmu = (struct nvidia_smmu *)smmu_domain->smmu;
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct nvidia_smmu_vintf *vintf = &nsmmu->vintf0;
	int i, slot;

#ifdef CONFIG_VFIO_MDEV_DEVICE
	/* Replace vintf0 with the corresponding one for Nested Translation mode */
	if (smmu_domain->stage == ARM_SMMU_DOMAIN_NESTED) {
		u16 vmid =  smmu_domain->s2_cfg.vmid;

		mutex_lock(&nsmmu->vmid_lock);
		vintf = nsmmu->vmid_mappings[vmid];
		mutex_unlock(&nsmmu->vmid_lock);
		if (!vintf) {
			dev_err(nsmmu->cmdqv_dev, "failed to find vintf\n");
			return;
		}
	}
#endif

	for (i = 0; i < fwspec->num_ids; i++) {
		unsigned int sid = fwspec->ids[i];
		unsigned long flags;

		spin_lock_irqsave(&vintf->lock, flags);

		/* Find a SID_REPLACE register matching sid */
		for (slot = 0; slot < ARRAY_SIZE(vintf->sid_replace); slot++)
			if (sid == vintf->sid_replace[slot])
				break;

		spin_unlock_irqrestore(&vintf->lock, flags);

		if (slot == ARRAY_SIZE(vintf->sid_replace)) {
			dev_dbg(nsmmu->cmdqv_dev, "failed to find vintf\n");
			return;
		}

		writel_relaxed(0, vintf->base + NVIDIA_VINTF_SID_REPLACE(slot));
		writel_relaxed(0, vintf->base + NVIDIA_VINTF_SID_MATCH(slot));

		nvidia_smmu_bitmap_free(vintf->sid_map, slot);
	}
}

const struct arm_smmu_impl nvidia_smmu_impl = {
	.device_reset = nvidia_smmu_device_reset,
	.get_cmdq = nvidia_smmu_get_cmdq,
	.attach_dev = nvidia_smmu_attach_dev,
	.detach_dev = nvidia_smmu_detach_dev,
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

	ret = nvidia_smmu_cmdqv_mdev_init(nsmmu);
	if (ret)
		return ERR_PTR(ret);

	nsmmu->smmu.impl = &nvidia_smmu_impl;

	return &nsmmu->smmu;
}

#ifdef CONFIG_VFIO_MDEV_DEVICE
#define mdev_name(m) dev_name(mdev_dev(m))

int nvidia_smmu_cmdqv_mdev_create(struct mdev_device *mdev)
{
	struct device *parent_dev = mdev_parent_dev(mdev);
	struct nvidia_smmu *nsmmu = platform_get_drvdata(to_platform_device(parent_dev));
	struct nvidia_cmdqv_mdev *cmdqv_mdev;
	struct nvidia_smmu_vintf *vintf;
	int vmid, idx, ret;
	u32 regval;

	cmdqv_mdev = kzalloc(sizeof(*cmdqv_mdev), GFP_KERNEL);
	if (!cmdqv_mdev)
		return -ENOMEM;

	cmdqv_mdev->vintf = kzalloc(sizeof(*cmdqv_mdev->vintf), GFP_KERNEL);
	if (!cmdqv_mdev->vintf) {
		ret = -ENOMEM;
		goto free_mdev;
	}

	cmdqv_mdev->mdev = mdev;
	cmdqv_mdev->nsmmu = nsmmu;
	vintf = cmdqv_mdev->vintf;

	mutex_lock(&nsmmu->mdev_lock);
	idx = nvidia_smmu_bitmap_alloc(nsmmu->vintf_map, nsmmu->num_total_vintfs);
	if (idx < 0) {
		dev_err(nsmmu->cmdqv_dev, "failed to allocate vintfs\n");
		mutex_unlock(&nsmmu->mdev_lock);
		ret = -EBUSY;
		goto free_vintf;
	}
	nsmmu->vintf_mdev[idx] = cmdqv_mdev;
	mutex_unlock(&nsmmu->mdev_lock);

	mutex_lock(&nsmmu->vmid_lock);
	vmid = arm_smmu_vmid_alloc(&nsmmu->smmu);
	if (vmid < 0) {
		dev_err(nsmmu->cmdqv_dev, "failed to allocate vmid\n");
		mutex_unlock(&nsmmu->vmid_lock);
		ret = -EBUSY;
		goto free_vintf_map;
	}

	/* Create mapping between vmid and vintf */
	nsmmu->vmid_mappings[vmid] = vintf;
	mutex_unlock(&nsmmu->vmid_lock);

	vintf->idx = idx;
	vintf->vmid = vmid;
	vintf->base = nsmmu->cmdqv_base + NVIDIA_CMDQV_VINTF(idx);

	spin_lock_init(&vintf->lock);
	mdev_set_drvdata(mdev, cmdqv_mdev);

	writel_relaxed(0, vintf->base + NVIDIA_VINTF_CONFIG);

	/* Point to NVIDIA_VINTFi_VCMDQ_BASE */
	vintf->vcmdq_base = nsmmu->cmdqv_base + NVIDIA_VINTFi_VCMDQ_BASE(vintf->idx);

	/* Alloc VCMDQs (2n, 2n+1, 2n+2, ...) to VINTF(idx) as logical-VCMDQ (0, 1, 2, ...) */
	for (idx = 0; idx < nsmmu->num_vcmdqs_per_vintf; idx++) {
		u16 vcmdq_idx = nsmmu->num_vcmdqs_per_vintf * vintf->idx + idx;

		regval = FIELD_PREP(CMDQV_CMDQ_ALLOC_VINTF, vintf->idx);
		regval |= FIELD_PREP(CMDQV_CMDQ_ALLOC_LVCMDQ, idx);
		regval |= CMDQV_CMDQ_ALLOCATED;
		writel_relaxed(regval, nsmmu->cmdqv_base + NVIDIA_CMDQV_CMDQ_ALLOC(vcmdq_idx));

		dev_info(nsmmu->cmdqv_dev, "allocated VCMDQ%u to VINTF%u as logical-VCMDQ%u\n",
			 vcmdq_idx, vintf->idx, idx);
	}

	dev_dbg(nsmmu->cmdqv_dev, "allocated VINTF%u to mdev_device (%s) binding to vmid (%d)\n",
		vintf->idx, dev_name(mdev_dev(mdev)), vintf->vmid);

	return 0;

free_vintf_map:
	nvidia_smmu_bitmap_free(nsmmu->vintf_map, idx);
free_vintf:
	kfree(cmdqv_mdev->vintf);
free_mdev:
	kfree(cmdqv_mdev);

	return ret;
}

int nvidia_smmu_cmdqv_mdev_remove(struct mdev_device *mdev)
{
	struct nvidia_cmdqv_mdev *cmdqv_mdev = mdev_get_drvdata(mdev);
	struct nvidia_smmu_vintf *vintf = cmdqv_mdev->vintf;
	struct nvidia_smmu *nsmmu = cmdqv_mdev->nsmmu;
	u16 idx;

	/* Deallocate VCMDQs of the VINTF(idx) */
	for (idx = 0; idx < nsmmu->num_vcmdqs_per_vintf; idx++) {
		u16 vcmdq_idx = nsmmu->num_vcmdqs_per_vintf * vintf->idx + idx;

		writel_relaxed(0, nsmmu->cmdqv_base + NVIDIA_CMDQV_CMDQ_ALLOC(vcmdq_idx));

		dev_info(nsmmu->cmdqv_dev, "deallocated VCMDQ%u to VINTF%u\n",
			 vcmdq_idx, vintf->idx);
	}

	/* Disable and cleanup VINTF configurations */
	writel_relaxed(0, vintf->base + NVIDIA_VINTF_CONFIG);

	mutex_lock(&nsmmu->mdev_lock);
	nvidia_smmu_bitmap_free(nsmmu->vintf_map, vintf->idx);
	nsmmu->vintf_mdev[vintf->idx] = NULL;
	mutex_unlock(&nsmmu->mdev_lock);

	mutex_lock(&nsmmu->vmid_lock);
	arm_smmu_vmid_free(&nsmmu->smmu, vintf->vmid);
	nsmmu->vmid_mappings[vintf->vmid] = NULL;
	mutex_unlock(&nsmmu->vmid_lock);

	mdev_set_drvdata(mdev, NULL);
	kfree(cmdqv_mdev->vintf);
	kfree(cmdqv_mdev);

	return 0;
}

static int nvidia_smmu_cmdqv_mdev_group_notifier(struct notifier_block *nb,
						 unsigned long action, void *data)
{
	struct nvidia_cmdqv_mdev *cmdqv_mdev =
		container_of(nb, struct nvidia_cmdqv_mdev, group_notifier);

	if (action == VFIO_GROUP_NOTIFY_SET_KVM)
		cmdqv_mdev->kvm = data;

	return NOTIFY_OK;
}

int nvidia_smmu_cmdqv_mdev_open(struct mdev_device *mdev)
{
	struct nvidia_cmdqv_mdev *cmdqv_mdev = mdev_get_drvdata(mdev);
	unsigned long events = VFIO_GROUP_NOTIFY_SET_KVM;
	struct device *dev = mdev_dev(mdev);
	int ret;

	cmdqv_mdev->group_notifier.notifier_call = nvidia_smmu_cmdqv_mdev_group_notifier;

	ret = vfio_register_notifier(dev, VFIO_GROUP_NOTIFY, &events, &cmdqv_mdev->group_notifier);
	if (ret)
		dev_err(mdev_dev(mdev), "failed to register group notifier: %d\n", ret);

	return ret;
}

void nvidia_smmu_cmdqv_mdev_release(struct mdev_device *mdev)
{
	struct nvidia_cmdqv_mdev *cmdqv_mdev = mdev_get_drvdata(mdev);
	struct device *dev = mdev_dev(mdev);

	vfio_unregister_notifier(dev, VFIO_GROUP_NOTIFY, &cmdqv_mdev->group_notifier);
}

ssize_t nvidia_smmu_cmdqv_mdev_read(struct mdev_device *mdev, char __user *buf,
				    size_t count, loff_t *ppos)
{
	struct nvidia_cmdqv_mdev *cmdqv_mdev = mdev_get_drvdata(mdev);
	struct nvidia_smmu_vintf *vintf = cmdqv_mdev->vintf;
	struct nvidia_smmu *nsmmu = cmdqv_mdev->nsmmu;
	struct device *dev = mdev_dev(mdev);
	loff_t reg_offset = *ppos, reg;
	u64 regval = 0;
	u16 idx, slot;

	/* Only support aligned 32/64-bit accesses */
	if (!count || (count % 4) || count > 8 || (reg_offset % count))
		return -EINVAL;

	switch (reg_offset) {
	case NVIDIA_CMDQV_CONFIG:
		regval = readl_relaxed(nsmmu->cmdqv_base + NVIDIA_CMDQV_CONFIG);
		break;
	case NVIDIA_CMDQV_STATUS:
		regval = readl_relaxed(nsmmu->cmdqv_base + NVIDIA_CMDQV_STATUS);
		break;
	case NVIDIA_CMDQV_PARAM:
		/*
		 * Guest shall import only one of the VINTFs using mdev interface,
		 * so limit the numbers of VINTF and VCMDQs in the PARAM register.
		 */
		regval = readl_relaxed(nsmmu->cmdqv_base + NVIDIA_CMDQV_PARAM);
		regval &= ~(CMDQV_NUM_VINTF_LOG2 | CMDQV_NUM_VCMDQ_LOG2);
		regval |= FIELD_PREP(CMDQV_NUM_VINTF_LOG2, 0);
		regval |= FIELD_PREP(CMDQV_NUM_VCMDQ_LOG2, ilog2(nsmmu->num_vcmdqs_per_vintf));
		break;
	case NVIDIA_CMDQV_VINTF_ERR_MAP:
		/* Translate the value to bit 0 as guest can only see vintf0 */
		regval = readl_relaxed(vintf->base + NVIDIA_VINTF_STATUS);
		regval = !!FIELD_GET(VINTF_STATUS, regval);
		break;
	case NVIDIA_CMDQV_VINTF_INT_MASK:
		/* Translate the value to bit 0 as guest can only see vintf0 */
		regval = readq_relaxed(nsmmu->cmdqv_base + NVIDIA_CMDQV_VINTF_INT_MASK);
		regval = !!(regval & BIT(vintf->idx));
		break;
	case NVIDIA_CMDQV_VCMDQ_ERR_MAP:
		regval = readq_relaxed(vintf->base + NVIDIA_VINTF_CMDQ_ERR_MAP);
		break;
	case NVIDIA_CMDQV_CMDQ_ALLOC(0) ... NVIDIA_CMDQV_CMDQ_ALLOC(128):
		if (idx >= nsmmu->num_vcmdqs_per_vintf) {
			/* Guest only has limited number of VMCDQs for one VINTF */
			regval = 0;
		} else {
			/* We have allocated VCMDQs, so just report it constantly */
			idx = (reg_offset - NVIDIA_CMDQV_CMDQ_ALLOC(0)) / 4;
			regval = FIELD_PREP(CMDQV_CMDQ_ALLOC_LVCMDQ, idx) | CMDQV_CMDQ_ALLOCATED;
		}
		break;
	case NVIDIA_VINTFi_CONFIG(0):
		regval = readl_relaxed(vintf->base + NVIDIA_VINTF_CONFIG);
		/* Guest should not see the VMID field */
		regval &= ~(VINTF_VMID);
		break;
	case NVIDIA_VINTFi_STATUS(0):
		regval = readl_relaxed(vintf->base + NVIDIA_VINTF_STATUS);
		break;
	case NVIDIA_VINTFi_SID_MATCH(0, 0) ... NVIDIA_VINTFi_SID_MATCH(0, 15):
		slot = (reg_offset - NVIDIA_VINTFi_SID_MATCH(0, 0)) / 0x4;
		regval = readl_relaxed(vintf->base + NVIDIA_VINTF_SID_MATCH(slot));
		break;
	case NVIDIA_VINTFi_SID_REPLACE(0, 0) ... NVIDIA_VINTFi_SID_REPLACE(0, 15):
		/* Guest should not see the PHY_SID but know whether it is set or not */
		slot = (reg_offset - NVIDIA_VINTFi_SID_REPLACE(0, 0)) / 0x4;
		regval = !!readl_relaxed(vintf->base + NVIDIA_VINTF_SID_REPLACE(slot));
		break;
	case NVIDIA_VINTFi_CMDQ_ERR_MAP(0):
		regval = readl_relaxed(vintf->base + NVIDIA_VINTF_CMDQ_ERR_MAP);
		break;
	case NVIDIA_CMDQV_VCMDQ(0) ... NVIDIA_CMDQV_VCMDQ(128):
		/* We allow fallback reading of VCMDQ PAGE0 upon a warning */
		dev_warn(dev, "read access at 0x%llx should go through mmap instead!", reg_offset);

		/* Adjust reg_offset since we're reading base on VINTF logical-VCMDQ space */
		regval = readl_relaxed(vintf->vcmdq_base + reg_offset - NVIDIA_CMDQV_VCMDQ(0));
		break;
	case NVIDIA_VCMDQ_BASE_L(0) ... NVIDIA_VCMDQ_BASE_L(128):
		/* Decipher idx and reg of VCMDQ */
		idx = (reg_offset - NVIDIA_VCMDQ_BASE_L(0)) / 0x80;
		reg = reg_offset - NVIDIA_VCMDQ_BASE_L(idx);

		switch (reg) {
		case NVIDIA_VCMDQ0_BASE_L:
			regval = nsmmu->vcmdq_regcache[idx].base_addr;
			if (count == 4)
				regval = lower_32_bits(regval);
			break;
		case NVIDIA_VCMDQ0_BASE_H:
			regval = upper_32_bits(nsmmu->vcmdq_regcache[idx].base_addr);
			break;
		case NVIDIA_VCMDQ0_CONS_INDX_BASE_L:
			regval = nsmmu->vcmdq_regcache[idx].cons_addr;
			if (count == 4)
				regval = lower_32_bits(regval);
			break;
		case NVIDIA_VCMDQ0_CONS_INDX_BASE_H:
			regval = upper_32_bits(nsmmu->vcmdq_regcache[idx].cons_addr);
			break;
		default:
			dev_err(dev, "unknown base address read access at 0x%llX\n", reg_offset);
			break;
		}
		break;
	default:
		dev_err(dev, "unhandled read access at 0x%llX\n", reg_offset);
		return -EINVAL;
	}

	if (copy_to_user(buf, &regval, count))
		return -EFAULT;
	*ppos += count;

	return count;
}

static u64 nvidia_smmu_cmdqv_mdev_gpa_to_pa(struct nvidia_cmdqv_mdev *cmdqv_mdev, u64 gpa)
{
	u64 gfn, hfn, hva, hpa, pg_offset;
	struct page *pg;
	long num_pages;

	gfn = gpa_to_gfn(gpa);
	pg_offset = gpa ^ gfn_to_gpa(gfn);

	hva = gfn_to_hva(cmdqv_mdev->kvm, gfn);
	if (kvm_is_error_hva(hva))
		return 0;

	num_pages = get_user_pages(hva, 1, FOLL_GET | FOLL_WRITE, &pg, NULL);
	if (num_pages < 1)
		return 0;

	hfn = page_to_pfn(pg);
	hpa = pfn_to_hpa(hfn);

	return hpa | pg_offset;
}

ssize_t nvidia_smmu_cmdqv_mdev_write(struct mdev_device *mdev, const char __user *buf,
				     size_t count, loff_t *ppos)
{
	struct nvidia_cmdqv_mdev *cmdqv_mdev = mdev_get_drvdata(mdev);
	struct nvidia_smmu_vintf *vintf = cmdqv_mdev->vintf;
	struct nvidia_smmu *nsmmu = cmdqv_mdev->nsmmu;
	struct device *dev = mdev_dev(mdev);
	loff_t reg_offset = *ppos, reg;
	u64 mask = U32_MAX;
	u64 regval = 0x0;
	u16 idx, slot;

	/* Only support aligned 32/64-bit accesses */
	if (!count || (count % 4) || count > 8 || (reg_offset % count))
		return -EINVAL;

	/* Get the value to be written to the register at reg_offset */
	if (copy_from_user(&regval, buf, count))
		return -EFAULT;

	switch (reg_offset) {
	case NVIDIA_VINTFi_CONFIG(0):
		regval &= ~(VINTF_VMID);
		regval |= FIELD_PREP(VINTF_VMID, vintf->vmid);
		writel_relaxed(regval, vintf->base + NVIDIA_VINTF_CONFIG);
		break;
	case NVIDIA_CMDQV_CMDQ_ALLOC(0) ... NVIDIA_CMDQV_CMDQ_ALLOC(128):
		/* Ignore since VCMDQs were already allocated to the VINTF */
		break;
	case NVIDIA_VINTFi_SID_MATCH(0, 0) ... NVIDIA_VINTFi_SID_MATCH(0, 15):
		slot = (reg_offset - NVIDIA_VINTFi_SID_MATCH(0, 0)) / 0x4;
		writel_relaxed(regval, vintf->base + NVIDIA_VINTF_SID_MATCH(slot));
		break;
	case NVIDIA_VINTFi_SID_REPLACE(0, 0) ... NVIDIA_VINTFi_SID_REPLACE(0, 15):
		/* Guest should not alter the value */
		break;
	case NVIDIA_CMDQV_VCMDQ(0) ... NVIDIA_CMDQV_VCMDQ(128):
		/* We allow fallback writing at VCMDQ PAGE0 upon a warning */
		dev_warn(dev, "write access at 0x%llx should go through mmap instead!", reg_offset);

		/* Adjust reg_offset since we're reading base on VINTF logical-VCMDQ space */
		writel_relaxed(regval, vintf->vcmdq_base + reg_offset - NVIDIA_CMDQV_VCMDQ(0));
		break;
	case NVIDIA_VCMDQ_BASE_L(0) ... NVIDIA_VCMDQ_BASE_L(128):
		/* Decipher idx and reg of VCMDQ */
		idx = (reg_offset - NVIDIA_VCMDQ_BASE_L(0)) / 0x80;
		reg = reg_offset - NVIDIA_VCMDQ_BASE_L(idx);

		switch (reg) {
		case NVIDIA_VCMDQ0_BASE_L:
			if (count == 8)
				mask = U64_MAX;
			regval &= mask;
			nsmmu->vcmdq_regcache[idx].base_addr &= ~mask;
			nsmmu->vcmdq_regcache[idx].base_addr |= regval;
			regval = nsmmu->vcmdq_regcache[idx].base_addr;
			break;
		case NVIDIA_VCMDQ0_BASE_H:
			nsmmu->vcmdq_regcache[idx].base_addr &= U32_MAX;
			nsmmu->vcmdq_regcache[idx].base_addr |= regval << 32;
			regval = nsmmu->vcmdq_regcache[idx].base_addr;
			break;
		case NVIDIA_VCMDQ0_CONS_INDX_BASE_L:
			if (count == 8)
				mask = U64_MAX;
			regval &= mask;
			nsmmu->vcmdq_regcache[idx].cons_addr &= ~mask;
			nsmmu->vcmdq_regcache[idx].cons_addr |= regval;
			regval = nsmmu->vcmdq_regcache[idx].cons_addr;
			break;
		case NVIDIA_VCMDQ0_CONS_INDX_BASE_H:
			nsmmu->vcmdq_regcache[idx].cons_addr &= U32_MAX;
			nsmmu->vcmdq_regcache[idx].cons_addr |= regval << 32;
			regval = nsmmu->vcmdq_regcache[idx].cons_addr;
			break;
		default:
			dev_err(dev, "unknown base address write access at 0x%llX\n", reg_offset);
			return -EFAULT;
		}

		/* Translate guest PA to host PA before writing to the address register */
		regval = nvidia_smmu_cmdqv_mdev_gpa_to_pa(cmdqv_mdev, regval);

		/* Do not fail mdev write as higher/lower addresses can be written separately */
		if (!regval)
			dev_dbg(dev, "failed to convert guest address for VCMDQ%d\n", idx);

		/* Adjust reg_offset since we're accessing it via the VINTF CMDQ aperture */
		reg_offset -= NVIDIA_CMDQV_VCMDQ(0);
		if (count == 8)
			writeq_relaxed(regval, vintf->vcmdq_base + reg_offset);
		else
			writel_relaxed(regval, vintf->vcmdq_base + reg_offset);
		break;

	default:
		dev_err(dev, "unhandled write access at 0x%llX\n", reg_offset);
		return -EINVAL;
	}

	*ppos += count;
	return count;
}

long nvidia_smmu_cmdqv_mdev_ioctl(struct mdev_device *mdev, unsigned int cmd, unsigned long arg)
{
	struct nvidia_cmdqv_mdev *cmdqv_mdev = mdev_get_drvdata(mdev);
	struct nvidia_smmu_vintf *vintf = cmdqv_mdev->vintf;
	struct device *dev = mdev_dev(mdev);
	struct vfio_device_info device_info;
	struct vfio_region_info region_info;
	unsigned long minsz;

	switch (cmd) {
	case VFIO_DEVICE_GET_INFO:
		minsz = offsetofend(struct vfio_device_info, num_irqs);

		if (copy_from_user(&device_info, (void __user *)arg, minsz))
			return -EFAULT;

		if (device_info.argsz < minsz)
			return -EINVAL;

		device_info.flags = 0;
		device_info.num_irqs = 0;
		/* MMIO Regions: [0] - CMDQV_CONFIG, [1] - VCMDQ_PAGE0, [2] - VCMDQ_PAGE1 */
		device_info.num_regions = 3;

		return copy_to_user((void __user *)arg, &device_info, minsz) ? -EFAULT : 0;
	case VFIO_DEVICE_GET_REGION_INFO:
		minsz = offsetofend(struct vfio_region_info, offset);

		if (copy_from_user(&region_info, (void __user *)arg, minsz))
			return -EFAULT;

		if (region_info.argsz < minsz)
			return -EINVAL;

		if (region_info.index >= 3)
			return -EINVAL;

		/* MMIO Regions: [0] - CMDQV_CONFIG, [1] - VCMDQ_PAGE0, [2] - VCMDQ_PAGE1 */
		region_info.size = SZ_64K;
		region_info.offset = region_info.index * SZ_64K;
		region_info.flags = VFIO_REGION_INFO_FLAG_READ | VFIO_REGION_INFO_FLAG_WRITE;
		/* In case of VCMDQ_PAGE0, add FLAG_MMAP */
		if (region_info.index == 1)
			region_info.flags |= VFIO_REGION_INFO_FLAG_MMAP;

		return copy_to_user((void __user *)arg, &region_info, minsz) ? -EFAULT : 0;
	case VFIO_IOMMU_GET_VMID:
		return copy_to_user((void __user *)arg, &vintf->vmid, sizeof(u16)) ? -EFAULT : 0;
	default:
		dev_err(dev, "unhandled ioctl cmd 0x%X\n", cmd);
		return -ENOTTY;
	}

	return 0;
}

int nvidia_smmu_cmdqv_mdev_mmap(struct mdev_device *mdev, struct vm_area_struct *vma)
{
	struct nvidia_cmdqv_mdev *cmdqv_mdev = mdev_get_drvdata(mdev);
	struct nvidia_smmu_vintf *vintf = cmdqv_mdev->vintf;
	struct nvidia_smmu *nsmmu = cmdqv_mdev->nsmmu;
	struct device *dev = mdev_dev(mdev);
	unsigned int region_idx;
	unsigned long size;

	/* Make sure that only VCMDQ_PAGE0 MMIO region can be mmapped */
	region_idx = (vma->vm_pgoff << PAGE_SHIFT) / SZ_64K;
	if (region_idx != 0x1) {
		dev_err(dev, "mmap unsupported for region_idx %d", region_idx);
		return -EINVAL;
	}

	size = vma->vm_end - vma->vm_start;
	if (size > SZ_64K)
		return -EINVAL;

	/* Fixup the VMA */
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	/* Map PAGE0 of VINTF[idx] */
	vma->vm_pgoff = nsmmu->ioaddr + NVIDIA_VINTFi_VCMDQ_BASE(vintf->idx);
	vma->vm_pgoff >>= PAGE_SHIFT;

	return remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff, size, vma->vm_page_prot);
}

static ssize_t name_show(struct mdev_type *mtype,
			 struct mdev_type_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", "NVIDIA_SMMU_CMDQV_VINTF - (2 VCMDQs/VINTF)");
}
static MDEV_TYPE_ATTR_RO(name);

static ssize_t available_instances_show(struct mdev_type *mtype,
					struct mdev_type_attribute *attr, char *buf)
{
	struct device *parent_dev = mtype_get_parent_dev(mtype);
	struct nvidia_smmu *nsmmu = platform_get_drvdata(to_platform_device(parent_dev));
	u16 idx, cnt = 0;

	mutex_lock(&nsmmu->mdev_lock);
	for (idx = 0; idx < nsmmu->num_total_vintfs; idx++)
		cnt += !nsmmu->vintf_mdev[idx];
	mutex_unlock(&nsmmu->mdev_lock);

	return sprintf(buf, "%d\n", cnt);
}
static MDEV_TYPE_ATTR_RO(available_instances);

static ssize_t device_api_show(struct mdev_type *mtype,
			       struct mdev_type_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", VFIO_DEVICE_API_PLATFORM_STRING);
}
static MDEV_TYPE_ATTR_RO(device_api);

static struct attribute *mdev_types_attrs[] = {
	&mdev_type_attr_name.attr,
	&mdev_type_attr_device_api.attr,
	&mdev_type_attr_available_instances.attr,
	NULL,
};

static struct attribute_group mdev_type_group1 = {
	.name  = "nvidia_cmdqv_vintf",
	.attrs = mdev_types_attrs,
};

static struct attribute_group *mdev_type_groups[] = {
	&mdev_type_group1,
	NULL,
};

struct mdev_parent_ops nvidia_smmu_cmdqv_mdev_ops = {
	.owner = THIS_MODULE,
	.supported_type_groups = mdev_type_groups,
	.create = nvidia_smmu_cmdqv_mdev_create,
	.remove = nvidia_smmu_cmdqv_mdev_remove,
	.open = nvidia_smmu_cmdqv_mdev_open,
	.release = nvidia_smmu_cmdqv_mdev_release,
	.read = nvidia_smmu_cmdqv_mdev_read,
	.write = nvidia_smmu_cmdqv_mdev_write,
	.ioctl = nvidia_smmu_cmdqv_mdev_ioctl,
	.mmap = nvidia_smmu_cmdqv_mdev_mmap,
};

#endif /* CONFIG_VFIO_MDEV_DEVICE */
