/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Qualcomm MSM8996 Network-on-Chip (NoC) QoS driver
 *
 * Copyright (c) 2021 Yassine Oudjana <y.oudjana@protonmail.com>
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/interconnect-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/interconnect/qcom,msm8996.h>

#include "icc-rpm-qos.h"
#include "smd-rpm.h"
#include "msm8996.h"

static const struct clk_bulk_data bus_clocks[] = {
	{ .id = "bus" },
	{ .id = "bus_a" },
};

static const struct clk_bulk_data bus_mm_clocks[] = {
	{ .id = "bus" },
	{ .id = "bus_a" },
	{ .id = "iface" },
};

DEFINE_QNODE(mas_cnoc_a1noc, MSM8996_MASTER_CNOC_A1NOC, 8, 116, -1, true, -1, 0, -1, MSM8996_SLAVE_A1NOC_SNOC);
DEFINE_QNODE(mas_crypto_c0, MSM8996_MASTER_CRYPTO_CORE0, 8, 23, -1, true, NOC_QOS_MODE_FIXED, 1, 0, MSM8996_SLAVE_A1NOC_SNOC);
DEFINE_QNODE(mas_pnoc_a1noc, MSM8996_MASTER_PNOC_A1NOC, 8, 117, -1, false, NOC_QOS_MODE_FIXED, 0, 1, MSM8996_SLAVE_A1NOC_SNOC);
DEFINE_QNODE(mas_usb3, MSM8996_MASTER_USB3, 8, 32, -1, true, NOC_QOS_MODE_FIXED, 1, 3, MSM8996_SLAVE_A2NOC_SNOC);
DEFINE_QNODE(mas_ipa, MSM8996_MASTER_IPA, 8, 59, -1, true, NOC_QOS_MODE_FIXED, 0, -1, MSM8996_SLAVE_A2NOC_SNOC);
DEFINE_QNODE(mas_ufs, MSM8996_MASTER_UFS, 8, 68, -1, true, NOC_QOS_MODE_FIXED, 1, 2, MSM8996_SLAVE_A2NOC_SNOC);
DEFINE_QNODE(mas_apps_proc, MSM8996_MASTER_AMPSS_M0, 8, 0, -1, true, NOC_QOS_MODE_FIXED, 0, 0, MSM8996_SLAVE_BIMC_SNOC_1, MSM8996_SLAVE_EBI_CH0, MSM8996_SLAVE_BIMC_SNOC_0);
DEFINE_QNODE(mas_oxili, MSM8996_MASTER_GRAPHICS_3D, 8, 6, -1, true, NOC_QOS_MODE_BYPASS, 0, 1, MSM8996_SLAVE_BIMC_SNOC_1, MSM8996_SLAVE_HMSS_L3, MSM8996_SLAVE_EBI_CH0, MSM8996_SLAVE_BIMC_SNOC_0);
DEFINE_QNODE(mas_mnoc_bimc, MSM8996_MASTER_MNOC_BIMC, 8, 2, -1, true, NOC_QOS_MODE_BYPASS, 0, 2, MSM8996_SLAVE_BIMC_SNOC_1, MSM8996_SLAVE_HMSS_L3, MSM8996_SLAVE_EBI_CH0, MSM8996_SLAVE_BIMC_SNOC_0);
DEFINE_QNODE(mas_snoc_bimc, MSM8996_MASTER_SNOC_BIMC, 8, 3, -1, false, NOC_QOS_MODE_BYPASS, 0, -1, MSM8996_SLAVE_HMSS_L3, MSM8996_SLAVE_EBI_CH0);
DEFINE_QNODE(mas_snoc_cnoc, MSM8996_MASTER_SNOC_CNOC, 8, 52, -1, false, -1, 0, -1, MSM8996_SLAVE_CLK_CTL, MSM8996_SLAVE_RBCPR_CX, MSM8996_SLAVE_A2NOC_SMMU_CFG, MSM8996_SLAVE_A0NOC_MPU_CFG, MSM8996_SLAVE_MESSAGE_RAM, MSM8996_SLAVE_CNOC_MNOC_MMSS_CFG, MSM8996_SLAVE_PCIE_0_CFG, MSM8996_SLAVE_TLMM, MSM8996_SLAVE_MPM, MSM8996_SLAVE_A0NOC_SMMU_CFG, MSM8996_SLAVE_EBI1_PHY_CFG, MSM8996_SLAVE_BIMC_CFG, MSM8996_SLAVE_PIMEM_CFG, MSM8996_SLAVE_RBCPR_MX, MSM8996_SLAVE_PRNG, MSM8996_SLAVE_PCIE20_AHB2PHY, MSM8996_SLAVE_A2NOC_MPU_CFG, MSM8996_SLAVE_QDSS_CFG, MSM8996_SLAVE_A2NOC_CFG, MSM8996_SLAVE_A0NOC_CFG, MSM8996_SLAVE_UFS_CFG, MSM8996_SLAVE_CRYPTO_0_CFG, MSM8996_SLAVE_PCIE_1_CFG, MSM8996_SLAVE_SNOC_CFG, MSM8996_SLAVE_SNOC_MPU_CFG, MSM8996_SLAVE_A1NOC_MPU_CFG, MSM8996_SLAVE_A1NOC_SMMU_CFG, MSM8996_SLAVE_PCIE_2_CFG, MSM8996_SLAVE_CNOC_MNOC_CFG, MSM8996_SLAVE_QDSS_RBCPR_APU_CFG, MSM8996_SLAVE_PMIC_ARB, MSM8996_SLAVE_IMEM_CFG, MSM8996_SLAVE_A1NOC_CFG, MSM8996_SLAVE_SSC_CFG, MSM8996_SLAVE_TCSR, MSM8996_SLAVE_LPASS_SMMU_CFG, MSM8996_SLAVE_DCC_CFG);
DEFINE_QNODE(mas_qdss_dap, MSM8996_MASTER_QDSS_DAP, 8, 49, -1, true, -1, 0, -1, MSM8996_SLAVE_QDSS_RBCPR_APU_CFG, MSM8996_SLAVE_RBCPR_CX, MSM8996_SLAVE_A2NOC_SMMU_CFG, MSM8996_SLAVE_A0NOC_MPU_CFG, MSM8996_SLAVE_MESSAGE_RAM, MSM8996_SLAVE_PCIE_0_CFG, MSM8996_SLAVE_TLMM, MSM8996_SLAVE_MPM, MSM8996_SLAVE_A0NOC_SMMU_CFG, MSM8996_SLAVE_EBI1_PHY_CFG, MSM8996_SLAVE_BIMC_CFG, MSM8996_SLAVE_PIMEM_CFG, MSM8996_SLAVE_RBCPR_MX, MSM8996_SLAVE_CLK_CTL, MSM8996_SLAVE_PRNG, MSM8996_SLAVE_PCIE20_AHB2PHY, MSM8996_SLAVE_A2NOC_MPU_CFG, MSM8996_SLAVE_QDSS_CFG, MSM8996_SLAVE_A2NOC_CFG, MSM8996_SLAVE_A0NOC_CFG, MSM8996_SLAVE_UFS_CFG, MSM8996_SLAVE_CRYPTO_0_CFG, MSM8996_SLAVE_CNOC_A1NOC, MSM8996_SLAVE_PCIE_1_CFG, MSM8996_SLAVE_SNOC_CFG, MSM8996_SLAVE_SNOC_MPU_CFG, MSM8996_SLAVE_A1NOC_MPU_CFG, MSM8996_SLAVE_A1NOC_SMMU_CFG, MSM8996_SLAVE_PCIE_2_CFG, MSM8996_SLAVE_CNOC_MNOC_CFG, MSM8996_SLAVE_CNOC_MNOC_MMSS_CFG, MSM8996_SLAVE_PMIC_ARB, MSM8996_SLAVE_IMEM_CFG, MSM8996_SLAVE_A1NOC_CFG, MSM8996_SLAVE_SSC_CFG, MSM8996_SLAVE_TCSR, MSM8996_SLAVE_LPASS_SMMU_CFG, MSM8996_SLAVE_DCC_CFG);
DEFINE_QNODE(mas_cnoc_mnoc_mmss_cfg, MSM8996_MASTER_CNOC_MNOC_MMSS_CFG, 8, 4, -1, true, -1, 0, -1, MSM8996_SLAVE_MMAGIC_CFG, MSM8996_SLAVE_DSA_MPU_CFG, MSM8996_SLAVE_MMSS_CLK_CFG, MSM8996_SLAVE_CAMERA_THROTTLE_CFG, MSM8996_SLAVE_VENUS_CFG, MSM8996_SLAVE_SMMU_VFE_CFG, MSM8996_SLAVE_MISC_CFG, MSM8996_SLAVE_SMMU_CPP_CFG, MSM8996_SLAVE_GRAPHICS_3D_CFG, MSM8996_SLAVE_DISPLAY_THROTTLE_CFG, MSM8996_SLAVE_VENUS_THROTTLE_CFG, MSM8996_SLAVE_CAMERA_CFG, MSM8996_SLAVE_DISPLAY_CFG, MSM8996_SLAVE_CPR_CFG, MSM8996_SLAVE_SMMU_ROTATOR_CFG, MSM8996_SLAVE_DSA_CFG, MSM8996_SLAVE_SMMU_VENUS_CFG, MSM8996_SLAVE_VMEM_CFG, MSM8996_SLAVE_SMMU_JPEG_CFG, MSM8996_SLAVE_SMMU_MDP_CFG, MSM8996_SLAVE_MNOC_MPU_CFG);
DEFINE_QNODE(mas_cnoc_mnoc_cfg, MSM8996_MASTER_CNOC_MNOC_CFG, 8, 5, -1, true, -1, 0, -1, MSM8996_SLAVE_SERVICE_MNOC);
DEFINE_QNODE(mas_cpp, MSM8996_MASTER_CPP, 32, 115, -1, true, NOC_QOS_MODE_BYPASS, 0, 5, MSM8996_SLAVE_MNOC_BIMC);
DEFINE_QNODE(mas_jpeg, MSM8996_MASTER_JPEG, 32, 7, -1, true, NOC_QOS_MODE_BYPASS, 0, 7, MSM8996_SLAVE_MNOC_BIMC);
DEFINE_QNODE(mas_mdp_p0, MSM8996_MASTER_MDP_PORT0, 32, 8, -1, true, NOC_QOS_MODE_BYPASS, 0, 1, MSM8996_SLAVE_MNOC_BIMC);
DEFINE_QNODE(mas_mdp_p1, MSM8996_MASTER_MDP_PORT1, 32, 61, -1, true, NOC_QOS_MODE_BYPASS, 0, 2, MSM8996_SLAVE_MNOC_BIMC);
DEFINE_QNODE(mas_rotator, MSM8996_MASTER_ROTATOR, 32, 120, -1, true, NOC_QOS_MODE_BYPASS, 0, 0, MSM8996_SLAVE_MNOC_BIMC);
DEFINE_QNODE(mas_venus, MSM8996_MASTER_VIDEO_P0, 32, 9, -1, true, NOC_QOS_MODE_BYPASS, 0, 3 /* TODO: 3 4 ?? */, MSM8996_SLAVE_MNOC_BIMC);
DEFINE_QNODE(mas_vfe, MSM8996_MASTER_VFE, 32, 11, -1, true, NOC_QOS_MODE_BYPASS, 0, 6, MSM8996_SLAVE_MNOC_BIMC);
DEFINE_QNODE(mas_snoc_vmem, MSM8996_MASTER_SNOC_VMEM, 32, 114, -1, true, -1, 0, -1, MSM8996_SLAVE_VMEM);
DEFINE_QNODE(mas_venus_vmem, MSM8996_MASTER_VIDEO_P0_OCMEM, 32, 121, -1, true, -1, 0, -1, MSM8996_SLAVE_VMEM);
DEFINE_QNODE(mas_snoc_pnoc, MSM8996_MASTER_SNOC_PNOC, 8, 44, -1, false, -1, 0, -1, MSM8996_SLAVE_BLSP_1, MSM8996_SLAVE_BLSP_2, MSM8996_SLAVE_SDCC_1, MSM8996_SLAVE_SDCC_2, MSM8996_SLAVE_SDCC_4, MSM8996_SLAVE_TSIF, MSM8996_SLAVE_PDM, MSM8996_SLAVE_AHB2PHY);
DEFINE_QNODE(mas_sdcc_1, MSM8996_MASTER_SDCC_1, 8, 33, -1, false, -1, 0, -1, MSM8996_SLAVE_PNOC_A1NOC);
DEFINE_QNODE(mas_sdcc_2, MSM8996_MASTER_SDCC_2, 8, 35, -1, false, -1, 0, -1, MSM8996_SLAVE_PNOC_A1NOC);
DEFINE_QNODE(mas_sdcc_4, MSM8996_MASTER_SDCC_4, 8, 36, -1, false, -1, 0, -1, MSM8996_SLAVE_PNOC_A1NOC);
DEFINE_QNODE(mas_usb_hs, MSM8996_MASTER_USB_HS, 8, 42, -1, false, -1, 0, -1, MSM8996_SLAVE_PNOC_A1NOC);
DEFINE_QNODE(mas_blsp_1, MSM8996_MASTER_BLSP_1, 4, 41, -1, false, -1, 0, -1, MSM8996_SLAVE_PNOC_A1NOC);
DEFINE_QNODE(mas_blsp_2, MSM8996_MASTER_BLSP_2, 4, 39, -1, false, -1, 0, -1, MSM8996_SLAVE_PNOC_A1NOC);
DEFINE_QNODE(mas_tsif, MSM8996_MASTER_TSIF, 4, 37, -1, false, -1, 0, -1, MSM8996_SLAVE_PNOC_A1NOC);
DEFINE_QNODE(mas_hmss, MSM8996_MASTER_HMSS, 8, 118, -1, true, NOC_QOS_MODE_FIXED, 1, 4, MSM8996_SLAVE_PIMEM, MSM8996_SLAVE_OCIMEM, MSM8996_SLAVE_SNOC_BIMC);
DEFINE_QNODE(mas_qdss_bam, MSM8996_MASTER_QDSS_BAM, 16, 19, -1, true, NOC_QOS_MODE_FIXED, 1, 2, MSM8996_SLAVE_PIMEM, MSM8996_SLAVE_USB3, MSM8996_SLAVE_OCIMEM, MSM8996_SLAVE_SNOC_BIMC, MSM8996_SLAVE_SNOC_PNOC);
DEFINE_QNODE(mas_snoc_cfg, MSM8996_MASTER_SNOC_CFG, 16, 20, -1, true, -1, 0, -1, MSM8996_SLAVE_SERVICE_SNOC);
DEFINE_QNODE(mas_bimc_snoc_0, MSM8996_MASTER_BIMC_SNOC_0, 16, 21, -1, true, -1, 0, -1, MSM8996_SLAVE_SNOC_VMEM, MSM8996_SLAVE_USB3, MSM8996_SLAVE_PIMEM, MSM8996_SLAVE_LPASS, MSM8996_SLAVE_APPSS, MSM8996_SLAVE_SNOC_CNOC, MSM8996_SLAVE_SNOC_PNOC, MSM8996_SLAVE_OCIMEM, MSM8996_SLAVE_QDSS_STM);
DEFINE_QNODE(mas_bimc_snoc_1, MSM8996_MASTER_BIMC_SNOC_1, 16, 109, -1, true, -1, 0, -1, MSM8996_SLAVE_PCIE_2, MSM8996_SLAVE_PCIE_1, MSM8996_SLAVE_PCIE_0);
DEFINE_QNODE(mas_a0noc_snoc, MSM8996_MASTER_A0NOC_SNOC, 16, 110, -1, true, -1, 0, -1, MSM8996_SLAVE_SNOC_PNOC, MSM8996_SLAVE_OCIMEM, MSM8996_SLAVE_APPSS, MSM8996_SLAVE_SNOC_BIMC, MSM8996_SLAVE_PIMEM);
DEFINE_QNODE(mas_a1noc_snoc, MSM8996_MASTER_A1NOC_SNOC, 16, 111, -1, false, -1, 0, -1, MSM8996_SLAVE_SNOC_VMEM, MSM8996_SLAVE_USB3, MSM8996_SLAVE_PCIE_0, MSM8996_SLAVE_PIMEM, MSM8996_SLAVE_PCIE_2, MSM8996_SLAVE_LPASS, MSM8996_SLAVE_PCIE_1, MSM8996_SLAVE_APPSS, MSM8996_SLAVE_SNOC_BIMC, MSM8996_SLAVE_SNOC_CNOC, MSM8996_SLAVE_SNOC_PNOC, MSM8996_SLAVE_OCIMEM, MSM8996_SLAVE_QDSS_STM);
DEFINE_QNODE(mas_a2noc_snoc, MSM8996_MASTER_A2NOC_SNOC, 16, 112, -1, false, -1, 0, -1, MSM8996_SLAVE_SNOC_VMEM, MSM8996_SLAVE_USB3, MSM8996_SLAVE_PCIE_1, MSM8996_SLAVE_PIMEM, MSM8996_SLAVE_PCIE_2, MSM8996_SLAVE_QDSS_STM, MSM8996_SLAVE_LPASS, MSM8996_SLAVE_SNOC_BIMC, MSM8996_SLAVE_SNOC_CNOC, MSM8996_SLAVE_SNOC_PNOC, MSM8996_SLAVE_OCIMEM, MSM8996_SLAVE_PCIE_0);
DEFINE_QNODE(mas_qdss_etr, MSM8996_MASTER_QDSS_ETR, 16, 31, -1, true, NOC_QOS_MODE_FIXED, 1, 3, MSM8996_SLAVE_PIMEM, MSM8996_SLAVE_USB3, MSM8996_SLAVE_OCIMEM, MSM8996_SLAVE_SNOC_BIMC, MSM8996_SLAVE_SNOC_PNOC);
DEFINE_QNODE(slv_a0noc_snoc, MSM8996_SLAVE_A0NOC_SNOC, 8, -1, 141, true, -1, 0, -1, MSM8996_MASTER_A0NOC_SNOC);
DEFINE_QNODE(slv_a1noc_snoc, MSM8996_SLAVE_A1NOC_SNOC, 8, -1, 142, false, -1, 0, -1, MSM8996_MASTER_A1NOC_SNOC);
DEFINE_QNODE(slv_a2noc_snoc, MSM8996_SLAVE_A2NOC_SNOC, 8, -1, 143, false, -1, 0, -1, MSM8996_MASTER_A2NOC_SNOC);
DEFINE_QNODE(slv_ebi, MSM8996_SLAVE_EBI_CH0, 8, -1, 0, false, -1, 0, -1, 0);
DEFINE_QNODE(slv_hmss_l3, MSM8996_SLAVE_HMSS_L3, 8, -1, 160, false, -1, 0, -1, 0);
DEFINE_QNODE(slv_bimc_snoc_0, MSM8996_SLAVE_BIMC_SNOC_0, 8, -1, 2, true, -1, 0, -1, MSM8996_MASTER_BIMC_SNOC_0);
DEFINE_QNODE(slv_bimc_snoc_1, MSM8996_SLAVE_BIMC_SNOC_1, 8, -1, 138, true, -1, 0, -1, MSM8996_MASTER_BIMC_SNOC_1);
DEFINE_QNODE(slv_cnoc_a1noc, MSM8996_SLAVE_CNOC_A1NOC, 4, -1, 75, true, -1, 0, -1, MSM8996_MASTER_CNOC_A1NOC);
DEFINE_QNODE(slv_clk_ctl, MSM8996_SLAVE_CLK_CTL, 4, -1, 47, false, -1, 0, -1, 0);
DEFINE_QNODE(slv_tcsr, MSM8996_SLAVE_TCSR, 4, -1, 50, false, -1, 0, -1, 0);
DEFINE_QNODE(slv_tlmm, MSM8996_SLAVE_TLMM, 4, -1, 51, false, -1, 0, -1, 0);
DEFINE_QNODE(slv_crypto0_cfg, MSM8996_SLAVE_CRYPTO_0_CFG, 4, -1, 52, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_mpm, MSM8996_SLAVE_MPM, 4, -1, 62, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_pimem_cfg, MSM8996_SLAVE_PIMEM_CFG, 4, -1, 167, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_imem_cfg, MSM8996_SLAVE_IMEM_CFG, 4, -1, 54, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_message_ram, MSM8996_SLAVE_MESSAGE_RAM, 4, -1, 55, false, -1, 0, -1, 0);
DEFINE_QNODE(slv_bimc_cfg, MSM8996_SLAVE_BIMC_CFG, 4, -1, 56, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_pmic_arb, MSM8996_SLAVE_PMIC_ARB, 4, -1, 59, false, -1, 0, -1, 0);
DEFINE_QNODE(slv_prng, MSM8996_SLAVE_PRNG, 4, -1, 127, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_dcc_cfg, MSM8996_SLAVE_DCC_CFG, 4, -1, 155, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_rbcpr_mx, MSM8996_SLAVE_RBCPR_MX, 4, -1, 170, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_qdss_cfg, MSM8996_SLAVE_QDSS_CFG, 4, -1, 63, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_rbcpr_cx, MSM8996_SLAVE_RBCPR_CX, 4, -1, 169, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_cpu_apu_cfg, MSM8996_SLAVE_QDSS_RBCPR_APU_CFG, 4, -1, 168, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_cnoc_mnoc_cfg, MSM8996_SLAVE_CNOC_MNOC_CFG, 4, -1, 66, true, -1, 0, -1, MSM8996_MASTER_CNOC_MNOC_CFG);
DEFINE_QNODE(slv_snoc_cfg, MSM8996_SLAVE_SNOC_CFG, 4, -1, 70, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_snoc_mpu_cfg, MSM8996_SLAVE_SNOC_MPU_CFG, 4, -1, 67, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_ebi1_phy_cfg, MSM8996_SLAVE_EBI1_PHY_CFG, 4, -1, 73, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_a0noc_cfg, MSM8996_SLAVE_A0NOC_CFG, 4, -1, 144, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_pcie_1_cfg, MSM8996_SLAVE_PCIE_1_CFG, 4, -1, 89, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_pcie_2_cfg, MSM8996_SLAVE_PCIE_2_CFG, 4, -1, 165, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_pcie_0_cfg, MSM8996_SLAVE_PCIE_0_CFG, 4, -1, 88, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_pcie20_ahb2phy, MSM8996_SLAVE_PCIE20_AHB2PHY, 4, -1, 163, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_a0noc_mpu_cfg, MSM8996_SLAVE_A0NOC_MPU_CFG, 4, -1, 145, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_ufs_cfg, MSM8996_SLAVE_UFS_CFG, 4, -1, 92, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_a1noc_cfg, MSM8996_SLAVE_A1NOC_CFG, 4, -1, 147, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_a1noc_mpu_cfg, MSM8996_SLAVE_A1NOC_MPU_CFG, 4, -1, 148, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_a2noc_cfg, MSM8996_SLAVE_A2NOC_CFG, 4, -1, 150, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_a2noc_mpu_cfg, MSM8996_SLAVE_A2NOC_MPU_CFG, 4, -1, 151, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_ssc_cfg, MSM8996_SLAVE_SSC_CFG, 4, -1, 177, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_a0noc_smmu_cfg, MSM8996_SLAVE_A0NOC_SMMU_CFG, 8, -1, 146, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_a1noc_smmu_cfg, MSM8996_SLAVE_A1NOC_SMMU_CFG, 8, -1, 149, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_a2noc_smmu_cfg, MSM8996_SLAVE_A2NOC_SMMU_CFG, 8, -1, 152, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_lpass_smmu_cfg, MSM8996_SLAVE_LPASS_SMMU_CFG, 8, -1, 161, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_cnoc_mnoc_mmss_cfg, MSM8996_SLAVE_CNOC_MNOC_MMSS_CFG, 8, -1, 58, true, -1, 0, -1, MSM8996_MASTER_CNOC_MNOC_MMSS_CFG);
DEFINE_QNODE(slv_mmagic_cfg, MSM8996_SLAVE_MMAGIC_CFG, 8, -1, 162, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_cpr_cfg, MSM8996_SLAVE_CPR_CFG, 8, -1, 6, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_misc_cfg, MSM8996_SLAVE_MISC_CFG, 8, -1, 8, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_venus_throttle_cfg, MSM8996_SLAVE_VENUS_THROTTLE_CFG, 8, -1, 178, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_venus_cfg, MSM8996_SLAVE_VENUS_CFG, 8, -1, 10, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_vmem_cfg, MSM8996_SLAVE_VMEM_CFG, 8, -1, 180, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_dsa_cfg, MSM8996_SLAVE_DSA_CFG, 8, -1, 157, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_mnoc_clocks_cfg, MSM8996_SLAVE_MMSS_CLK_CFG, 8, -1, 12, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_dsa_mpu_cfg, MSM8996_SLAVE_DSA_MPU_CFG, 8, -1, 158, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_mnoc_mpu_cfg, MSM8996_SLAVE_MNOC_MPU_CFG, 8, -1, 14, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_display_cfg, MSM8996_SLAVE_DISPLAY_CFG, 8, -1, 4, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_display_throttle_cfg, MSM8996_SLAVE_DISPLAY_THROTTLE_CFG, 8, -1, 156, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_camera_cfg, MSM8996_SLAVE_CAMERA_CFG, 8, -1, 3, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_camera_throttle_cfg, MSM8996_SLAVE_CAMERA_THROTTLE_CFG, 8, -1, 154, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_oxili_cfg, MSM8996_SLAVE_GRAPHICS_3D_CFG, 8, -1, 11, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_smmu_mdp_cfg, MSM8996_SLAVE_SMMU_MDP_CFG, 8, -1, 173, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_smmu_rot_cfg, MSM8996_SLAVE_SMMU_ROTATOR_CFG, 8, -1, 174, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_smmu_venus_cfg, MSM8996_SLAVE_SMMU_VENUS_CFG, 8, -1, 175, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_smmu_cpp_cfg, MSM8996_SLAVE_SMMU_CPP_CFG, 8, -1, 171, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_smmu_jpeg_cfg, MSM8996_SLAVE_SMMU_JPEG_CFG, 8, -1, 172, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_smmu_vfe_cfg, MSM8996_SLAVE_SMMU_VFE_CFG, 8, -1, 176, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_mnoc_bimc, MSM8996_SLAVE_MNOC_BIMC, 32, -1, 16, true, -1, 0, -1, MSM8996_MASTER_MNOC_BIMC);
DEFINE_QNODE(slv_vmem, MSM8996_SLAVE_VMEM, 32, -1, 179, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_srvc_mnoc, MSM8996_SLAVE_SERVICE_MNOC, 8, -1, 17, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_pnoc_a1noc, MSM8996_SLAVE_PNOC_A1NOC, 8, -1, 139, false, -1, 0, -1, MSM8996_MASTER_PNOC_A1NOC);
DEFINE_QNODE(slv_usb_hs, MSM8996_SLAVE_USB_HS, 4, -1, 40, false, -1, 0, -1, 0);
DEFINE_QNODE(slv_sdcc_2, MSM8996_SLAVE_SDCC_2, 4, -1, 33, false, -1, 0, -1, 0);
DEFINE_QNODE(slv_sdcc_4, MSM8996_SLAVE_SDCC_4, 4, -1, 34, false, -1, 0, -1, 0);
DEFINE_QNODE(slv_tsif, MSM8996_SLAVE_TSIF, 4, -1, 35, false, -1, 0, -1, 0);
DEFINE_QNODE(slv_blsp_2, MSM8996_SLAVE_BLSP_2, 4, -1, 37, false, -1, 0, -1, 0);
DEFINE_QNODE(slv_sdcc_1, MSM8996_SLAVE_SDCC_1, 4, -1, 31, false, -1, 0, -1, 0);
DEFINE_QNODE(slv_blsp_1, MSM8996_SLAVE_BLSP_1, 4, -1, 39, false, -1, 0, -1, 0);
DEFINE_QNODE(slv_pdm, MSM8996_SLAVE_PDM, 4, -1, 41, false, -1, 0, -1, 0);
DEFINE_QNODE(slv_ahb2phy, MSM8996_SLAVE_AHB2PHY, 4, -1, 153, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_hmss, MSM8996_SLAVE_APPSS, 16, -1, 20, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_lpass, MSM8996_SLAVE_LPASS, 16, -1, 21, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_usb3, MSM8996_SLAVE_USB3, 16, -1, 22, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_snoc_bimc, MSM8996_SLAVE_SNOC_BIMC, 32, -1, 24, false, -1, 0, -1, MSM8996_MASTER_SNOC_BIMC);
DEFINE_QNODE(slv_snoc_cnoc, MSM8996_SLAVE_SNOC_CNOC, 16, -1, 25, false, -1, 0, -1, MSM8996_MASTER_SNOC_CNOC);
DEFINE_QNODE(slv_imem, MSM8996_SLAVE_OCIMEM, 16, -1, 26, false, -1, 0, -1, 0);
DEFINE_QNODE(slv_pimem, MSM8996_SLAVE_PIMEM, 16, -1, 166, false, -1, 0, -1, 0);
DEFINE_QNODE(slv_snoc_vmem, MSM8996_SLAVE_SNOC_VMEM, 16, -1, 140, true, -1, 0, -1, MSM8996_MASTER_SNOC_VMEM);
DEFINE_QNODE(slv_snoc_pnoc, MSM8996_SLAVE_SNOC_PNOC, 16, -1, 28, false, -1, 0, -1, MSM8996_MASTER_SNOC_PNOC);
DEFINE_QNODE(slv_qdss_stm, MSM8996_SLAVE_QDSS_STM, 16, -1, 30, false, -1, 0, -1, 0);
DEFINE_QNODE(slv_pcie_0, MSM8996_SLAVE_PCIE_0, 16, -1, 84, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_pcie_1, MSM8996_SLAVE_PCIE_1, 16, -1, 85, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_pcie_2, MSM8996_SLAVE_PCIE_2, 16, -1, 164, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_srvc_snoc, MSM8996_SLAVE_SERVICE_SNOC, 16, -1, 29, true, -1, 0, -1, 0);

static struct qcom_icc_node *a1noc_nodes[] = {
	[MASTER_CNOC_A1NOC] = &mas_cnoc_a1noc,
	[MASTER_CRYPTO_CORE0] = &mas_crypto_c0,
	[MASTER_PNOC_A1NOC] = &mas_pnoc_a1noc,
};

static const struct regmap_config msm8996_a1noc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x7000,
	.fast_io	= true,
};

static const struct qcom_icc_desc msm8996_a1noc = {
	.nodes = a1noc_nodes,
	.num_nodes = ARRAY_SIZE(a1noc_nodes),
	.regmap_cfg = &msm8996_a1noc_regmap_config,
};

static struct qcom_icc_node *a2noc_nodes[] = {
	[MASTER_USB3] = &mas_usb3,
	[MASTER_IPA] = &mas_ipa,
	[MASTER_UFS] = &mas_ufs,
};

static const struct regmap_config msm8996_a2noc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0xa000,
	.fast_io	= true,
};

static const struct qcom_icc_desc msm8996_a2noc = {
	.nodes = a2noc_nodes,
	.num_nodes = ARRAY_SIZE(a2noc_nodes),
	.regmap_cfg = &msm8996_a2noc_regmap_config,
};

static struct qcom_icc_node *bimc_nodes[] = {
	[MASTER_AMPSS_M0] = &mas_apps_proc,
	[MASTER_GRAPHICS_3D] = &mas_oxili,
	[MASTER_MNOC_BIMC] = &mas_mnoc_bimc,
	[MASTER_SNOC_BIMC] = &mas_snoc_bimc,
	[SLAVE_EBI_CH0] = &slv_ebi,
	[SLAVE_HMSS_L3] = &slv_hmss_l3,
	[SLAVE_BIMC_SNOC_0] = &slv_bimc_snoc_0,
	[SLAVE_BIMC_SNOC_1] = &slv_bimc_snoc_1,
};

static const struct regmap_config msm8996_bimc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x62000,
	.fast_io	= true,
};

static const struct qcom_icc_desc msm8996_bimc = {
	.nodes = bimc_nodes,
	.num_nodes = ARRAY_SIZE(bimc_nodes),
	.regmap_cfg = &msm8996_bimc_regmap_config,
};

static struct qcom_icc_node *cnoc_nodes[] = {
	[MASTER_SNOC_CNOC] = &mas_snoc_cnoc,
	[MASTER_QDSS_DAP] = &mas_qdss_dap,
	[SLAVE_CNOC_A1NOC] = &slv_cnoc_a1noc,
	[SLAVE_CLK_CTL] = &slv_clk_ctl,
	[SLAVE_TCSR] = &slv_tcsr,
	[SLAVE_TLMM] = &slv_tlmm,
	[SLAVE_CRYPTO_0_CFG] = &slv_crypto0_cfg,
	[SLAVE_MPM] = &slv_mpm,
	[SLAVE_PIMEM_CFG] = &slv_pimem_cfg,
	[SLAVE_IMEM_CFG] = &slv_imem_cfg,
	[SLAVE_MESSAGE_RAM] = &slv_message_ram,
	[SLAVE_BIMC_CFG] = &slv_bimc_cfg,
	[SLAVE_PMIC_ARB] = &slv_pmic_arb,
	[SLAVE_PRNG] = &slv_prng,
	[SLAVE_DCC_CFG] = &slv_dcc_cfg,
	[SLAVE_RBCPR_MX] = &slv_rbcpr_mx,
	[SLAVE_QDSS_CFG] = &slv_qdss_cfg,
	[SLAVE_RBCPR_CX] = &slv_rbcpr_cx,
	[SLAVE_QDSS_RBCPR_APU] = &slv_cpu_apu_cfg,
	[SLAVE_CNOC_MNOC_CFG] = &slv_cnoc_mnoc_cfg,
	[SLAVE_SNOC_CFG] = &slv_snoc_cfg,
	[SLAVE_SNOC_MPU_CFG] = &slv_snoc_mpu_cfg,
	[SLAVE_EBI1_PHY_CFG] = &slv_ebi1_phy_cfg,
	[SLAVE_A0NOC_CFG] = &slv_a0noc_cfg,
	[SLAVE_PCIE_1_CFG] = &slv_pcie_1_cfg,
	[SLAVE_PCIE_2_CFG] = &slv_pcie_2_cfg,
	[SLAVE_PCIE_0_CFG] = &slv_pcie_0_cfg,
	[SLAVE_PCIE20_AHB2PHY] = &slv_pcie20_ahb2phy,
	[SLAVE_A0NOC_MPU_CFG] = &slv_a0noc_mpu_cfg,
	[SLAVE_UFS_CFG] = &slv_ufs_cfg,
	[SLAVE_A1NOC_CFG] = &slv_a1noc_cfg,
	[SLAVE_A1NOC_MPU_CFG] = &slv_a1noc_mpu_cfg,
	[SLAVE_A2NOC_CFG] = &slv_a2noc_cfg,
	[SLAVE_A2NOC_MPU_CFG] = &slv_a2noc_mpu_cfg,
	[SLAVE_SSC_CFG] = &slv_ssc_cfg,
	[SLAVE_A0NOC_SMMU_CFG] = &slv_a0noc_smmu_cfg,
	[SLAVE_A1NOC_SMMU_CFG] = &slv_a1noc_smmu_cfg,
	[SLAVE_A2NOC_SMMU_CFG] = &slv_a2noc_smmu_cfg,
	[SLAVE_LPASS_SMMU_CFG] = &slv_lpass_smmu_cfg,
	[SLAVE_CNOC_MNOC_MMSS_CFG] = &slv_cnoc_mnoc_mmss_cfg,
};

static const struct regmap_config msm8996_cnoc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x1000,
	.fast_io	= true,
};

static const struct qcom_icc_desc msm8996_cnoc = {
	.nodes = cnoc_nodes,
	.num_nodes = ARRAY_SIZE(cnoc_nodes),
	.regmap_cfg = &msm8996_cnoc_regmap_config,
};

static struct qcom_icc_node *mnoc_nodes[] = {
	[MASTER_CNOC_MNOC_CFG] = &mas_cnoc_mnoc_cfg,
	[MASTER_CPP] = &mas_cpp,
	[MASTER_JPEG] = &mas_jpeg,
	[MASTER_MDP_PORT0] = &mas_mdp_p0,
	[MASTER_MDP_PORT1] = &mas_mdp_p1,
	[MASTER_ROTATOR] = &mas_rotator,
	[MASTER_VIDEO_P0] = &mas_venus,
	[MASTER_VFE] = &mas_vfe,
	[MASTER_SNOC_VMEM] = &mas_snoc_vmem,
	[MASTER_VIDEO_P0_OCMEM] = &mas_venus_vmem,
	[MASTER_CNOC_MNOC_MMSS_CFG] = &mas_cnoc_mnoc_mmss_cfg,
	[SLAVE_MNOC_BIMC] = &slv_mnoc_bimc,
	[SLAVE_VMEM] = &slv_vmem,
	[SLAVE_SERVICE_MNOC] = &slv_srvc_mnoc,
	[SLAVE_MMAGIC_CFG] = &slv_mmagic_cfg,
	[SLAVE_CPR_CFG] = &slv_cpr_cfg,
	[SLAVE_MISC_CFG] = &slv_misc_cfg,
	[SLAVE_VENUS_THROTTLE_CFG] = &slv_venus_throttle_cfg,
	[SLAVE_VENUS_CFG] = &slv_venus_cfg,
	[SLAVE_VMEM_CFG] = &slv_vmem_cfg,
	[SLAVE_DSA_CFG] = &slv_dsa_cfg,
	[SLAVE_MMSS_CLK_CFG] = &slv_mnoc_clocks_cfg,
	[SLAVE_DSA_MPU_CFG] = &slv_dsa_mpu_cfg,
	[SLAVE_MNOC_MPU_CFG] = &slv_mnoc_mpu_cfg,
	[SLAVE_DISPLAY_CFG] = &slv_display_cfg,
	[SLAVE_DISPLAY_THROTTLE_CFG] = &slv_display_throttle_cfg,
	[SLAVE_CAMERA_CFG] = &slv_camera_cfg,
	[SLAVE_CAMERA_THROTTLE_CFG] = &slv_camera_throttle_cfg,
	[SLAVE_GRAPHICS_3D_CFG] = &slv_oxili_cfg,
	[SLAVE_SMMU_MDP_CFG] = &slv_smmu_mdp_cfg,
	[SLAVE_SMMU_ROT_CFG] = &slv_smmu_rot_cfg,
	[SLAVE_SMMU_VENUS_CFG] = &slv_smmu_venus_cfg,
	[SLAVE_SMMU_CPP_CFG] = &slv_smmu_cpp_cfg,
	[SLAVE_SMMU_JPEG_CFG] = &slv_smmu_jpeg_cfg,
	[SLAVE_SMMU_VFE_CFG] = &slv_smmu_vfe_cfg,
};

static const struct regmap_config msm8996_mnoc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x20000,
	.fast_io	= true,
};

static const struct qcom_icc_desc msm8996_mnoc = {
	.nodes = mnoc_nodes,
	.num_nodes = ARRAY_SIZE(mnoc_nodes),
	.regmap_cfg = &msm8996_mnoc_regmap_config,
};


static struct qcom_icc_node *pnoc_nodes[] = {
	[MASTER_SNOC_PNOC] = &mas_snoc_pnoc,
	[MASTER_SDCC_1] = &mas_sdcc_1,
	[MASTER_SDCC_2] = &mas_sdcc_2,
	[MASTER_SDCC_4] = &mas_sdcc_4,
	[MASTER_USB_HS] = &mas_usb_hs,
	[MASTER_BLSP_1] = &mas_blsp_1,
	[MASTER_BLSP_2] = &mas_blsp_2,
	[MASTER_TSIF] = &mas_tsif,
	[SLAVE_PNOC_A1NOC] = &slv_pnoc_a1noc,
	[SLAVE_USB_HS] = &slv_usb_hs,
	[SLAVE_SDCC_2] = &slv_sdcc_2,
	[SLAVE_SDCC_4] = &slv_sdcc_4,
	[SLAVE_TSIF] = &slv_tsif,
	[SLAVE_BLSP_2] = &slv_blsp_2,
	[SLAVE_SDCC_1] = &slv_sdcc_1,
	[SLAVE_BLSP_1] = &slv_blsp_1,
	[SLAVE_PDM] = &slv_pdm,
	[SLAVE_AHB2PHY] = &slv_ahb2phy,
};

static const struct regmap_config msm8996_pnoc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x3000,
	.fast_io	= true,
};

static const struct qcom_icc_desc msm8996_pnoc = {
	.nodes = pnoc_nodes,
	.num_nodes = ARRAY_SIZE(pnoc_nodes),
	.regmap_cfg = &msm8996_pnoc_regmap_config,
};

static struct qcom_icc_node *snoc_nodes[] = {
	[MASTER_HMSS] = &mas_hmss,
	[MASTER_QDSS_BAM] = &mas_qdss_bam,
	[MASTER_SNOC_CFG] = &mas_snoc_cfg,
	[MASTER_BIMC_SNOC_0] = &mas_bimc_snoc_0,
	[MASTER_BIMC_SNOC_1] = &mas_bimc_snoc_1,
	[MASTER_A0NOC_SNOC] = &mas_a0noc_snoc,
	[MASTER_A1NOC_SNOC] = &mas_a1noc_snoc,
	[MASTER_A2NOC_SNOC] = &mas_a2noc_snoc,
	[MASTER_QDSS_ETR] = &mas_qdss_etr,
	[SLAVE_A0NOC_SNOC] = &slv_a0noc_snoc,
	[SLAVE_A1NOC_SNOC] = &slv_a1noc_snoc,
	[SLAVE_A2NOC_SNOC] = &slv_a2noc_snoc,
	[SLAVE_HMSS] = &slv_hmss,
	[SLAVE_LPASS] = &slv_lpass,
	[SLAVE_USB3] = &slv_usb3,
	[SLAVE_SNOC_BIMC] = &slv_snoc_bimc,
	[SLAVE_SNOC_CNOC] = &slv_snoc_cnoc,
	[SLAVE_IMEM] = &slv_imem,
	[SLAVE_PIMEM] = &slv_pimem,
	[SLAVE_SNOC_VMEM] = &slv_snoc_vmem,
	[SLAVE_SNOC_PNOC] = &slv_snoc_pnoc,
	[SLAVE_QDSS_STM] = &slv_qdss_stm,
	[SLAVE_PCIE_0] = &slv_pcie_0,
	[SLAVE_PCIE_1] = &slv_pcie_1,
	[SLAVE_PCIE_2] = &slv_pcie_2,
	[SLAVE_SERVICE_SNOC] = &slv_srvc_snoc,
};

static const struct regmap_config msm8996_snoc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x20000,
	.fast_io	= true,
};

static const struct qcom_icc_desc msm8996_snoc = {
	.nodes = snoc_nodes,
	.num_nodes = ARRAY_SIZE(snoc_nodes),
	.regmap_cfg = &msm8996_snoc_regmap_config,
};

static int qnoc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct qcom_icc_desc *desc;
	struct icc_onecell_data *data;
	struct icc_provider *provider;
	struct qcom_icc_node **qnodes;
	struct qcom_icc_provider *qp;
	struct icc_node *node;
	struct resource *res;
	size_t num_nodes, i;
	int ret;

	/* wait for the RPM proxy */
	if (!qcom_icc_rpm_smd_available())
		return -EPROBE_DEFER;

	desc = of_device_get_match_data(dev);
	if (!desc)
		return -EINVAL;

	qnodes = desc->nodes;
	num_nodes = desc->num_nodes;

	qp = devm_kzalloc(dev, sizeof(*qp), GFP_KERNEL);
	if (!qp)
		return -ENOMEM;

	data = devm_kzalloc(dev, struct_size(data, nodes, num_nodes),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	if (of_device_is_compatible(dev->of_node, "qcom,msm8996-mnoc")) {
		qp->bus_clks = devm_kmemdup(dev, bus_mm_clocks,
					    sizeof(bus_mm_clocks), GFP_KERNEL);
		qp->num_clks = ARRAY_SIZE(bus_mm_clocks);
	} else {
		if (of_device_is_compatible(dev->of_node, "qcom,msm8996-bimc"))
			qp->is_bimc_node = true;

		qp->bus_clks = devm_kmemdup(dev, bus_clocks, sizeof(bus_clocks),
					    GFP_KERNEL);
		qp->num_clks = ARRAY_SIZE(bus_clocks);
	}
	if (!qp->bus_clks)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	qp->mmio = devm_ioremap_resource(dev, res);
	if (IS_ERR(qp->mmio)) {
		dev_err(dev, "Cannot ioremap interconnect bus resource\n");
		return PTR_ERR(qp->mmio);
	}

	qp->regmap = devm_regmap_init_mmio(dev, qp->mmio, desc->regmap_cfg);
	if (IS_ERR(qp->regmap)) {
		dev_err(dev, "Cannot regmap interconnect bus resource\n");
		return PTR_ERR(qp->regmap);
	}

	ret = devm_clk_bulk_get(dev, qp->num_clks, qp->bus_clks);
	if (ret)
		return ret;

	ret = clk_bulk_prepare_enable(qp->num_clks, qp->bus_clks);
	if (ret)
		return ret;

	provider = &qp->provider;
	INIT_LIST_HEAD(&provider->nodes);
	provider->dev = dev;
	provider->set = qcom_icc_rpm_qos_set;
	provider->aggregate = icc_std_aggregate;
	provider->xlate = of_icc_xlate_onecell;
	provider->data = data;

	ret = icc_provider_add(provider);
	if (ret) {
		dev_err(dev, "error adding interconnect provider: %d\n", ret);
		clk_bulk_disable_unprepare(qp->num_clks, qp->bus_clks);
		return ret;
	}

	for (i = 0; i < num_nodes; i++) {
		size_t j;

		node = icc_node_create(qnodes[i]->id);
		if (IS_ERR(node)) {
			ret = PTR_ERR(node);
			goto err;
		}

		node->name = qnodes[i]->name;
		node->data = qnodes[i];
		icc_node_add(node, provider);

		for (j = 0; j < qnodes[i]->num_links; j++) {
			icc_link_create(node, qnodes[i]->links[j]);
		}

		data->nodes[i] = node;
	}
	data->num_nodes = num_nodes;
	platform_set_drvdata(pdev, qp);

	return 0;
err:
	icc_nodes_remove(provider);
	clk_bulk_disable_unprepare(qp->num_clks, qp->bus_clks);
	icc_provider_del(provider);

	return ret;
}

static int qnoc_remove(struct platform_device *pdev)
{
	struct qcom_icc_provider *qp = platform_get_drvdata(pdev);

	icc_nodes_remove(&qp->provider);
	clk_bulk_disable_unprepare(qp->num_clks, qp->bus_clks);
	return icc_provider_del(&qp->provider);
}

static const struct of_device_id qnoc_of_match[] = {
	{ .compatible = "qcom,msm8996-a1noc", .data = &msm8996_a1noc},
	{ .compatible = "qcom,msm8996-a2noc", .data = &msm8996_a2noc},
	{ .compatible = "qcom,msm8996-bimc", .data = &msm8996_bimc},
	{ .compatible = "qcom,msm8996-cnoc", .data = &msm8996_cnoc},
	{ .compatible = "qcom,msm8996-mnoc", .data = &msm8996_mnoc},
	{ .compatible = "qcom,msm8996-pnoc", .data = &msm8996_pnoc},
	{ .compatible = "qcom,msm8996-snoc", .data = &msm8996_snoc},
	{ }
};
MODULE_DEVICE_TABLE(of, qnoc_of_match);

static struct platform_driver qnoc_driver = {
	.probe = qnoc_probe,
	.remove = qnoc_remove,
	.driver = {
		.name = "qnoc-msm8996",
		.of_match_table = qnoc_of_match,
		.sync_state = icc_sync_state,
	},
};
module_platform_driver(qnoc_driver);

MODULE_AUTHOR("Yassine Oudjana <y.oudjana@protonmail.com>");
MODULE_DESCRIPTION("Qualcomm MSM8996 NoC driver");
MODULE_LICENSE("GPL v2");
