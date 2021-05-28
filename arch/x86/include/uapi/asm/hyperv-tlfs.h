/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_X86_HYPERV_TLFS_USER_H
#define _UAPI_ASM_X86_HYPERV_TLFS_USER_H

#include <linux/types.h>

#define HV_PARTITION_PROCESSOR_FEATURE_BANKS 2

union hv_partition_processor_features {
	struct {
		__u64 sse3_support:1;
		__u64 lahf_sahf_support:1;
		__u64 ssse3_support:1;
		__u64 sse4_1_support:1;
		__u64 sse4_2_support:1;
		__u64 sse4a_support:1;
		__u64 xop_support:1;
		__u64 pop_cnt_support:1;
		__u64 cmpxchg16b_support:1;
		__u64 altmovcr8_support:1;
		__u64 lzcnt_support:1;
		__u64 mis_align_sse_support:1;
		__u64 mmx_ext_support:1;
		__u64 amd3dnow_support:1;
		__u64 extended_amd3dnow_support:1;
		__u64 page_1gb_support:1;
		__u64 aes_support:1;
		__u64 pclmulqdq_support:1;
		__u64 pcid_support:1;
		__u64 fma4_support:1;
		__u64 f16c_support:1;
		__u64 rd_rand_support:1;
		__u64 rd_wr_fs_gs_support:1;
		__u64 smep_support:1;
		__u64 enhanced_fast_string_support:1;
		__u64 bmi1_support:1;
		__u64 bmi2_support:1;
		__u64 hle_support_deprecated:1;
		__u64 rtm_support_deprecated:1;
		__u64 movbe_support:1;
		__u64 npiep1_support:1;
		__u64 dep_x87_fpu_save_support:1;
		__u64 rd_seed_support:1;
		__u64 adx_support:1;
		__u64 intel_prefetch_support:1;
		__u64 smap_support:1;
		__u64 hle_support:1;
		__u64 rtm_support:1;
		__u64 rdtscp_support:1;
		__u64 clflushopt_support:1;
		__u64 clwb_support:1;
		__u64 sha_support:1;
		__u64 x87_pointers_saved_support:1;
		__u64 invpcid_support:1;
		__u64 ibrs_support:1;
		__u64 stibp_support:1;
		__u64 ibpb_support: 1;
		__u64 unrestricted_guest_support:1;
		__u64 mdd_support:1;
		__u64 fast_short_rep_mov_support:1;
		__u64 l1dcache_flush_support:1;
		__u64 rdcl_no_support:1;
		__u64 ibrs_all_support:1;
		__u64 skip_l1df_support:1;
		__u64 ssb_no_support:1;
		__u64 rsb_a_no_support:1;
		__u64 virt_spec_ctrl_support:1;
		__u64 rd_pid_support:1;
		__u64 umip_support:1;
		__u64 mbs_no_support:1;
		__u64 mb_clear_support:1;
		__u64 taa_no_support:1;
		__u64 tsx_ctrl_support:1;
		/*
		 * N.B. The final processor feature bit in bank 0 is reserved to
		 * simplify potential downlevel backports.
		 */
		__u64 reserved_bank0:1;

		/* N.B. Begin bank 1 processor features. */
		__u64 acount_mcount_support:1;
		__u64 tsc_invariant_support:1;
		__u64 cl_zero_support:1;
		__u64 rdpru_support:1;
		__u64 la57_support:1;
		__u64 mbec_support:1;
		__u64 nested_virt_support:1;
		__u64 psfd_support:1;
		__u64 cet_ss_support:1;
		__u64 cet_ibt_support:1;
		__u64 vmx_exception_inject_support:1;
		__u64 enqcmd_support:1;
		__u64 umwait_tpause_support:1;
		__u64 movdiri_support:1;
		__u64 movdir64b_support:1;
		__u64 cldemote_support:1;
		__u64 serialize_support:1;
		__u64 tsc_deadline_tmr_support:1;
		__u64 tsc_adjust_support:1;
		__u64 fzlrep_movsb:1;
		__u64 fsrep_stosb:1;
		__u64 fsrep_cmpsb:1;
		__u64 reserved_bank1:42;
	} __packed;
	__u64 as_uint64[HV_PARTITION_PROCESSOR_FEATURE_BANKS];
};

union hv_partition_processor_xsave_features {
	struct {
		__u64 xsave_support : 1;
		__u64 xsaveopt_support : 1;
		__u64 avx_support : 1;
		__u64 reserved1 : 61;
	} __packed;
	__u64 as_uint64;
};

struct hv_partition_creation_properties {
	union hv_partition_processor_features disabled_processor_features;
	union hv_partition_processor_xsave_features
		disabled_processor_xsave_features;
} __packed;

enum hv_register_name {
	/* Suspend Registers */
	HV_REGISTER_EXPLICIT_SUSPEND		= 0x00000000,
	HV_REGISTER_INTERCEPT_SUSPEND		= 0x00000001,
	HV_REGISTER_INSTRUCTION_EMULATION_HINTS	= 0x00000002,
	HV_REGISTER_DISPATCH_SUSPEND		= 0x00000003,
	HV_REGISTER_INTERNAL_ACTIVITY_STATE	= 0x00000004,

	/* Version */
	HV_REGISTER_HYPERVISOR_VERSION	= 0x00000100, /* 128-bit result same as CPUID 0x40000002 */

	/* Feature Access (registers are 128 bits) - same as CPUID 0x40000003 - 0x4000000B */
	HV_REGISTER_PRIVILEGES_AND_FEATURES_INFO	= 0x00000200,
	HV_REGISTER_FEATURES_INFO			= 0x00000201,
	HV_REGISTER_IMPLEMENTATION_LIMITS_INFO		= 0x00000202,
	HV_REGISTER_HARDWARE_FEATURES_INFO		= 0x00000203,
	HV_REGISTER_CPU_MANAGEMENT_FEATURES_INFO	= 0x00000204,
	HV_REGISTER_SVM_FEATURES_INFO			= 0x00000205,
	HV_REGISTER_SKIP_LEVEL_FEATURES_INFO		= 0x00000206,
	HV_REGISTER_NESTED_VIRT_FEATURES_INFO		= 0x00000207,
	HV_REGISTER_IPT_FEATURES_INFO			= 0x00000208,

	/* Guest Crash Registers */
	HV_REGISTER_GUEST_CRASH_P0	= 0x00000210,
	HV_REGISTER_GUEST_CRASH_P1	= 0x00000211,
	HV_REGISTER_GUEST_CRASH_P2	= 0x00000212,
	HV_REGISTER_GUEST_CRASH_P3	= 0x00000213,
	HV_REGISTER_GUEST_CRASH_P4	= 0x00000214,
	HV_REGISTER_GUEST_CRASH_CTL	= 0x00000215,

	/* Power State Configuration */
	HV_REGISTER_POWER_STATE_CONFIG_C1	= 0x00000220,
	HV_REGISTER_POWER_STATE_TRIGGER_C1	= 0x00000221,
	HV_REGISTER_POWER_STATE_CONFIG_C2	= 0x00000222,
	HV_REGISTER_POWER_STATE_TRIGGER_C2	= 0x00000223,
	HV_REGISTER_POWER_STATE_CONFIG_C3	= 0x00000224,
	HV_REGISTER_POWER_STATE_TRIGGER_C3	= 0x00000225,

	/* Frequency Registers */
	HV_REGISTER_PROCESSOR_CLOCK_FREQUENCY	= 0x00000240,
	HV_REGISTER_INTERRUPT_CLOCK_FREQUENCY	= 0x00000241,

	/* Idle Register */
	HV_REGISTER_GUEST_IDLE	= 0x00000250,

	/* Guest Debug */
	HV_REGISTER_DEBUG_DEVICE_OPTIONS	= 0x00000260,

	/* Memory Zeroing Conrol Register */
	HV_REGISTER_MEMORY_ZEROING_CONTROL	= 0x00000270,

	/* Pending Event Register */
	HV_REGISTER_PENDING_EVENT0	= 0x00010004,
	HV_REGISTER_PENDING_EVENT1	= 0x00010005,

	/* Misc */
	HV_REGISTER_VP_RUNTIME			= 0x00090000,
	HV_REGISTER_GUEST_OS_ID			= 0x00090002,
	HV_REGISTER_VP_INDEX			= 0x00090003,
	HV_REGISTER_TIME_REF_COUNT		= 0x00090004,
	HV_REGISTER_CPU_MANAGEMENT_VERSION	= 0x00090007,
	HV_REGISTER_VP_ASSIST_PAGE		= 0x00090013,
	HV_REGISTER_VP_ROOT_SIGNAL_COUNT	= 0x00090014,
	HV_REGISTER_REFERENCE_TSC		= 0x00090017,

	/* Performance statistics Registers */
	HV_REGISTER_STATS_PARTITION_RETAIL	= 0x00090020,
	HV_REGISTER_STATS_PARTITION_INTERNAL	= 0x00090021,
	HV_REGISTER_STATS_VP_RETAIL		= 0x00090022,
	HV_REGISTER_STATS_VP_INTERNAL		= 0x00090023,

	HV_REGISTER_NESTED_VP_INDEX	= 0x00091003,

	/* Hypervisor-defined Registers (Synic) */
	HV_REGISTER_SINT0	= 0x000A0000,
	HV_REGISTER_SINT1	= 0x000A0001,
	HV_REGISTER_SINT2	= 0x000A0002,
	HV_REGISTER_SINT3	= 0x000A0003,
	HV_REGISTER_SINT4	= 0x000A0004,
	HV_REGISTER_SINT5	= 0x000A0005,
	HV_REGISTER_SINT6	= 0x000A0006,
	HV_REGISTER_SINT7	= 0x000A0007,
	HV_REGISTER_SINT8	= 0x000A0008,
	HV_REGISTER_SINT9	= 0x000A0009,
	HV_REGISTER_SINT10	= 0x000A000A,
	HV_REGISTER_SINT11	= 0x000A000B,
	HV_REGISTER_SINT12	= 0x000A000C,
	HV_REGISTER_SINT13	= 0x000A000D,
	HV_REGISTER_SINT14	= 0x000A000E,
	HV_REGISTER_SINT15	= 0x000A000F,
	HV_REGISTER_SCONTROL	= 0x000A0010,
	HV_REGISTER_SVERSION	= 0x000A0011,
	HV_REGISTER_SIFP	= 0x000A0012,
	HV_REGISTER_SIPP	= 0x000A0013,
	HV_REGISTER_EOM		= 0x000A0014,
	HV_REGISTER_SIRBP	= 0x000A0015,

	HV_REGISTER_NESTED_SINT0	= 0x000A1000,
	HV_REGISTER_NESTED_SINT1	= 0x000A1001,
	HV_REGISTER_NESTED_SINT2	= 0x000A1002,
	HV_REGISTER_NESTED_SINT3	= 0x000A1003,
	HV_REGISTER_NESTED_SINT4	= 0x000A1004,
	HV_REGISTER_NESTED_SINT5	= 0x000A1005,
	HV_REGISTER_NESTED_SINT6	= 0x000A1006,
	HV_REGISTER_NESTED_SINT7	= 0x000A1007,
	HV_REGISTER_NESTED_SINT8	= 0x000A1008,
	HV_REGISTER_NESTED_SINT9	= 0x000A1009,
	HV_REGISTER_NESTED_SINT10	= 0x000A100A,
	HV_REGISTER_NESTED_SINT11	= 0x000A100B,
	HV_REGISTER_NESTED_SINT12	= 0x000A100C,
	HV_REGISTER_NESTED_SINT13	= 0x000A100D,
	HV_REGISTER_NESTED_SINT14	= 0x000A100E,
	HV_REGISTER_NESTED_SINT15	= 0x000A100F,
	HV_REGISTER_NESTED_SCONTROL	= 0x000A1010,
	HV_REGISTER_NESTED_SVERSION	= 0x000A1011,
	HV_REGISTER_NESTED_SIFP		= 0x000A1012,
	HV_REGISTER_NESTED_SIPP		= 0x000A1013,
	HV_REGISTER_NESTED_EOM		= 0x000A1014,
	HV_REGISTER_NESTED_SIRBP	= 0x000a1015,


	/* Hypervisor-defined Registers (Synthetic Timers) */
	HV_REGISTER_STIMER0_CONFIG		= 0x000B0000,
	HV_REGISTER_STIMER0_COUNT		= 0x000B0001,
	HV_REGISTER_STIMER1_CONFIG		= 0x000B0002,
	HV_REGISTER_STIMER1_COUNT		= 0x000B0003,
	HV_REGISTER_STIMER2_CONFIG		= 0x000B0004,
	HV_REGISTER_STIMER2_COUNT		= 0x000B0005,
	HV_REGISTER_STIMER3_CONFIG		= 0x000B0006,
	HV_REGISTER_STIMER3_COUNT		= 0x000B0007,
	HV_REGISTER_STIME_UNHALTED_TIMER_CONFIG	= 0x000B0100,
	HV_REGISTER_STIME_UNHALTED_TIMER_COUNT	= 0x000b0101,

	/* Synthetic VSM registers */

	/* 0x000D0000-1 are available for future use. */
	HV_REGISTER_VSM_CODE_PAGE_OFFSETS	= 0x000D0002,
	HV_REGISTER_VSM_VP_STATUS		= 0x000D0003,
	HV_REGISTER_VSM_PARTITION_STATUS	= 0x000D0004,
	HV_REGISTER_VSM_VINA			= 0x000D0005,
	HV_REGISTER_VSM_CAPABILITIES		= 0x000D0006,
	HV_REGISTER_VSM_PARTITION_CONFIG	= 0x000D0007,

	HV_REGISTER_VSM_VP_SECURE_CONFIG_VTL0	= 0x000D0010,
	HV_REGISTER_VSM_VP_SECURE_CONFIG_VTL1	= 0x000D0011,
	HV_REGISTER_VSM_VP_SECURE_CONFIG_VTL2	= 0x000D0012,
	HV_REGISTER_VSM_VP_SECURE_CONFIG_VTL3	= 0x000D0013,
	HV_REGISTER_VSM_VP_SECURE_CONFIG_VTL4	= 0x000D0014,
	HV_REGISTER_VSM_VP_SECURE_CONFIG_VTL5	= 0x000D0015,
	HV_REGISTER_VSM_VP_SECURE_CONFIG_VTL6	= 0x000D0016,
	HV_REGISTER_VSM_VP_SECURE_CONFIG_VTL7	= 0x000D0017,
	HV_REGISTER_VSM_VP_SECURE_CONFIG_VTL8	= 0x000D0018,
	HV_REGISTER_VSM_VP_SECURE_CONFIG_VTL9	= 0x000D0019,
	HV_REGISTER_VSM_VP_SECURE_CONFIG_VTL10	= 0x000D001A,
	HV_REGISTER_VSM_VP_SECURE_CONFIG_VTL11	= 0x000D001B,
	HV_REGISTER_VSM_VP_SECURE_CONFIG_VTL12	= 0x000D001C,
	HV_REGISTER_VSM_VP_SECURE_CONFIG_VTL13	= 0x000D001D,
	HV_REGISTER_VSM_VP_SECURE_CONFIG_VTL14	= 0x000D001E,

	HV_REGISTER_VSM_VP_WAIT_FOR_TLB_LOCK	= 0x000D0020,

	HV_REGISTER_ISOLATION_CAPABILITIES	= 0x000D0100,

	/* Pending Interruption Register */
	HV_REGISTER_PENDING_INTERRUPTION	= 0x00010002,

	/* Interrupt State register */
	HV_REGISTER_INTERRUPT_STATE	= 0x00010003,

	/* Interruptible notification register */
	HV_X64_REGISTER_DELIVERABILITY_NOTIFICATIONS	= 0x00010006,

	/* X64 User-Mode Registers */
	HV_X64_REGISTER_RAX	= 0x00020000,
	HV_X64_REGISTER_RCX	= 0x00020001,
	HV_X64_REGISTER_RDX	= 0x00020002,
	HV_X64_REGISTER_RBX	= 0x00020003,
	HV_X64_REGISTER_RSP	= 0x00020004,
	HV_X64_REGISTER_RBP	= 0x00020005,
	HV_X64_REGISTER_RSI	= 0x00020006,
	HV_X64_REGISTER_RDI	= 0x00020007,
	HV_X64_REGISTER_R8	= 0x00020008,
	HV_X64_REGISTER_R9	= 0x00020009,
	HV_X64_REGISTER_R10	= 0x0002000A,
	HV_X64_REGISTER_R11	= 0x0002000B,
	HV_X64_REGISTER_R12	= 0x0002000C,
	HV_X64_REGISTER_R13	= 0x0002000D,
	HV_X64_REGISTER_R14	= 0x0002000E,
	HV_X64_REGISTER_R15	= 0x0002000F,
	HV_X64_REGISTER_RIP	= 0x00020010,
	HV_X64_REGISTER_RFLAGS	= 0x00020011,

	/* X64 Floating Point and Vector Registers */
	HV_X64_REGISTER_XMM0			= 0x00030000,
	HV_X64_REGISTER_XMM1			= 0x00030001,
	HV_X64_REGISTER_XMM2			= 0x00030002,
	HV_X64_REGISTER_XMM3			= 0x00030003,
	HV_X64_REGISTER_XMM4			= 0x00030004,
	HV_X64_REGISTER_XMM5			= 0x00030005,
	HV_X64_REGISTER_XMM6			= 0x00030006,
	HV_X64_REGISTER_XMM7			= 0x00030007,
	HV_X64_REGISTER_XMM8			= 0x00030008,
	HV_X64_REGISTER_XMM9			= 0x00030009,
	HV_X64_REGISTER_XMM10			= 0x0003000A,
	HV_X64_REGISTER_XMM11			= 0x0003000B,
	HV_X64_REGISTER_XMM12			= 0x0003000C,
	HV_X64_REGISTER_XMM13			= 0x0003000D,
	HV_X64_REGISTER_XMM14			= 0x0003000E,
	HV_X64_REGISTER_XMM15			= 0x0003000F,
	HV_X64_REGISTER_FP_MMX0			= 0x00030010,
	HV_X64_REGISTER_FP_MMX1			= 0x00030011,
	HV_X64_REGISTER_FP_MMX2			= 0x00030012,
	HV_X64_REGISTER_FP_MMX3			= 0x00030013,
	HV_X64_REGISTER_FP_MMX4			= 0x00030014,
	HV_X64_REGISTER_FP_MMX5			= 0x00030015,
	HV_X64_REGISTER_FP_MMX6			= 0x00030016,
	HV_X64_REGISTER_FP_MMX7			= 0x00030017,
	HV_X64_REGISTER_FP_CONTROL_STATUS	= 0x00030018,
	HV_X64_REGISTER_XMM_CONTROL_STATUS	= 0x00030019,

	/* X64 Control Registers */
	HV_X64_REGISTER_CR0	= 0x00040000,
	HV_X64_REGISTER_CR2	= 0x00040001,
	HV_X64_REGISTER_CR3	= 0x00040002,
	HV_X64_REGISTER_CR4	= 0x00040003,
	HV_X64_REGISTER_CR8	= 0x00040004,
	HV_X64_REGISTER_XFEM	= 0x00040005,

	/* X64 Intermediate Control Registers */
	HV_X64_REGISTER_INTERMEDIATE_CR0	= 0x00041000,
	HV_X64_REGISTER_INTERMEDIATE_CR4	= 0x00041003,
	HV_X64_REGISTER_INTERMEDIATE_CR8	= 0x00041004,

	/* X64 Debug Registers */
	HV_X64_REGISTER_DR0	= 0x00050000,
	HV_X64_REGISTER_DR1	= 0x00050001,
	HV_X64_REGISTER_DR2	= 0x00050002,
	HV_X64_REGISTER_DR3	= 0x00050003,
	HV_X64_REGISTER_DR6	= 0x00050004,
	HV_X64_REGISTER_DR7	= 0x00050005,

	/* X64 Segment Registers */
	HV_X64_REGISTER_ES	= 0x00060000,
	HV_X64_REGISTER_CS	= 0x00060001,
	HV_X64_REGISTER_SS	= 0x00060002,
	HV_X64_REGISTER_DS	= 0x00060003,
	HV_X64_REGISTER_FS	= 0x00060004,
	HV_X64_REGISTER_GS	= 0x00060005,
	HV_X64_REGISTER_LDTR	= 0x00060006,
	HV_X64_REGISTER_TR	= 0x00060007,

	/* X64 Table Registers */
	HV_X64_REGISTER_IDTR	= 0x00070000,
	HV_X64_REGISTER_GDTR	= 0x00070001,

	/* X64 Virtualized MSRs */
	HV_X64_REGISTER_TSC		= 0x00080000,
	HV_X64_REGISTER_EFER		= 0x00080001,
	HV_X64_REGISTER_KERNEL_GS_BASE	= 0x00080002,
	HV_X64_REGISTER_APIC_BASE	= 0x00080003,
	HV_X64_REGISTER_PAT		= 0x00080004,
	HV_X64_REGISTER_SYSENTER_CS	= 0x00080005,
	HV_X64_REGISTER_SYSENTER_EIP	= 0x00080006,
	HV_X64_REGISTER_SYSENTER_ESP	= 0x00080007,
	HV_X64_REGISTER_STAR		= 0x00080008,
	HV_X64_REGISTER_LSTAR		= 0x00080009,
	HV_X64_REGISTER_CSTAR		= 0x0008000A,
	HV_X64_REGISTER_SFMASK		= 0x0008000B,
	HV_X64_REGISTER_INITIAL_APIC_ID	= 0x0008000C,

	/* X64 Cache control MSRs */
	HV_X64_REGISTER_MSR_MTRR_CAP		= 0x0008000D,
	HV_X64_REGISTER_MSR_MTRR_DEF_TYPE	= 0x0008000E,
	HV_X64_REGISTER_MSR_MTRR_PHYS_BASE0	= 0x00080010,
	HV_X64_REGISTER_MSR_MTRR_PHYS_BASE1	= 0x00080011,
	HV_X64_REGISTER_MSR_MTRR_PHYS_BASE2	= 0x00080012,
	HV_X64_REGISTER_MSR_MTRR_PHYS_BASE3	= 0x00080013,
	HV_X64_REGISTER_MSR_MTRR_PHYS_BASE4	= 0x00080014,
	HV_X64_REGISTER_MSR_MTRR_PHYS_BASE5	= 0x00080015,
	HV_X64_REGISTER_MSR_MTRR_PHYS_BASE6	= 0x00080016,
	HV_X64_REGISTER_MSR_MTRR_PHYS_BASE7	= 0x00080017,
	HV_X64_REGISTER_MSR_MTRR_PHYS_BASE8	= 0x00080018,
	HV_X64_REGISTER_MSR_MTRR_PHYS_BASE9	= 0x00080019,
	HV_X64_REGISTER_MSR_MTRR_PHYS_BASEA	= 0x0008001A,
	HV_X64_REGISTER_MSR_MTRR_PHYS_BASEB	= 0x0008001B,
	HV_X64_REGISTER_MSR_MTRR_PHYS_BASEC	= 0x0008001C,
	HV_X64_REGISTER_MSR_MTRR_PHYS_BASED	= 0x0008001D,
	HV_X64_REGISTER_MSR_MTRR_PHYS_BASEE	= 0x0008001E,
	HV_X64_REGISTER_MSR_MTRR_PHYS_BASEF	= 0x0008001F,
	HV_X64_REGISTER_MSR_MTRR_PHYS_MASK0	= 0x00080040,
	HV_X64_REGISTER_MSR_MTRR_PHYS_MASK1	= 0x00080041,
	HV_X64_REGISTER_MSR_MTRR_PHYS_MASK2	= 0x00080042,
	HV_X64_REGISTER_MSR_MTRR_PHYS_MASK3	= 0x00080043,
	HV_X64_REGISTER_MSR_MTRR_PHYS_MASK4	= 0x00080044,
	HV_X64_REGISTER_MSR_MTRR_PHYS_MASK5	= 0x00080045,
	HV_X64_REGISTER_MSR_MTRR_PHYS_MASK6	= 0x00080046,
	HV_X64_REGISTER_MSR_MTRR_PHYS_MASK7	= 0x00080047,
	HV_X64_REGISTER_MSR_MTRR_PHYS_MASK8	= 0x00080048,
	HV_X64_REGISTER_MSR_MTRR_PHYS_MASK9	= 0x00080049,
	HV_X64_REGISTER_MSR_MTRR_PHYS_MASKA	= 0x0008004A,
	HV_X64_REGISTER_MSR_MTRR_PHYS_MASKB	= 0x0008004B,
	HV_X64_REGISTER_MSR_MTRR_PHYS_MASKC	= 0x0008004C,
	HV_X64_REGISTER_MSR_MTRR_PHYS_MASKD	= 0x0008004D,
	HV_X64_REGISTER_MSR_MTRR_PHYS_MASKE	= 0x0008004E,
	HV_X64_REGISTER_MSR_MTRR_PHYS_MASKF	= 0x0008004F,
	HV_X64_REGISTER_MSR_MTRR_FIX64K00000	= 0x00080070,
	HV_X64_REGISTER_MSR_MTRR_FIX16K80000	= 0x00080071,
	HV_X64_REGISTER_MSR_MTRR_FIX16KA0000	= 0x00080072,
	HV_X64_REGISTER_MSR_MTRR_FIX4KC0000	= 0x00080073,
	HV_X64_REGISTER_MSR_MTRR_FIX4KC8000	= 0x00080074,
	HV_X64_REGISTER_MSR_MTRR_FIX4KD0000	= 0x00080075,
	HV_X64_REGISTER_MSR_MTRR_FIX4KD8000	= 0x00080076,
	HV_X64_REGISTER_MSR_MTRR_FIX4KE0000	= 0x00080077,
	HV_X64_REGISTER_MSR_MTRR_FIX4KE8000	= 0x00080078,
	HV_X64_REGISTER_MSR_MTRR_FIX4KF0000	= 0x00080079,
	HV_X64_REGISTER_MSR_MTRR_FIX4KF8000	= 0x0008007A,

	HV_X64_REGISTER_TSC_AUX		= 0x0008007B,
	HV_X64_REGISTER_BNDCFGS		= 0x0008007C,
	HV_X64_REGISTER_DEBUG_CTL	= 0x0008007D,

	/* Available */
	HV_X64_REGISTER_AVAILABLE0008007E	= 0x0008007E,
	HV_X64_REGISTER_AVAILABLE0008007F	= 0x0008007F,

	HV_X64_REGISTER_SGX_LAUNCH_CONTROL0	= 0x00080080,
	HV_X64_REGISTER_SGX_LAUNCH_CONTROL1	= 0x00080081,
	HV_X64_REGISTER_SGX_LAUNCH_CONTROL2	= 0x00080082,
	HV_X64_REGISTER_SGX_LAUNCH_CONTROL3	= 0x00080083,
	HV_X64_REGISTER_SPEC_CTRL		= 0x00080084,
	HV_X64_REGISTER_PRED_CMD		= 0x00080085,
	HV_X64_REGISTER_VIRT_SPEC_CTRL		= 0x00080086,

	/* Other MSRs */
	HV_X64_REGISTER_MSR_IA32_MISC_ENABLE		= 0x000800A0,
	HV_X64_REGISTER_IA32_FEATURE_CONTROL		= 0x000800A1,
	HV_X64_REGISTER_IA32_VMX_BASIC			= 0x000800A2,
	HV_X64_REGISTER_IA32_VMX_PINBASED_CTLS		= 0x000800A3,
	HV_X64_REGISTER_IA32_VMX_PROCBASED_CTLS		= 0x000800A4,
	HV_X64_REGISTER_IA32_VMX_EXIT_CTLS		= 0x000800A5,
	HV_X64_REGISTER_IA32_VMX_ENTRY_CTLS		= 0x000800A6,
	HV_X64_REGISTER_IA32_VMX_MISC			= 0x000800A7,
	HV_X64_REGISTER_IA32_VMX_CR0_FIXED0		= 0x000800A8,
	HV_X64_REGISTER_IA32_VMX_CR0_FIXED1		= 0x000800A9,
	HV_X64_REGISTER_IA32_VMX_CR4_FIXED0		= 0x000800AA,
	HV_X64_REGISTER_IA32_VMX_CR4_FIXED1		= 0x000800AB,
	HV_X64_REGISTER_IA32_VMX_VMCS_ENUM		= 0x000800AC,
	HV_X64_REGISTER_IA32_VMX_PROCBASED_CTLS2	= 0x000800AD,
	HV_X64_REGISTER_IA32_VMX_EPT_VPID_CAP		= 0x000800AE,
	HV_X64_REGISTER_IA32_VMX_TRUE_PINBASED_CTLS	= 0x000800AF,
	HV_X64_REGISTER_IA32_VMX_TRUE_PROCBASED_CTLS	= 0x000800B0,
	HV_X64_REGISTER_IA32_VMX_TRUE_EXIT_CTLS		= 0x000800B1,
	HV_X64_REGISTER_IA32_VMX_TRUE_ENTRY_CTLS	= 0x000800B2,

	/* Performance monitoring MSRs */
	HV_X64_REGISTER_PERF_GLOBAL_CTRL	= 0x00081000,
	HV_X64_REGISTER_PERF_GLOBAL_STATUS	= 0x00081001,
	HV_X64_REGISTER_PERF_GLOBAL_IN_USE	= 0x00081002,
	HV_X64_REGISTER_FIXED_CTR_CTRL		= 0x00081003,
	HV_X64_REGISTER_DS_AREA			= 0x00081004,
	HV_X64_REGISTER_PEBS_ENABLE		= 0x00081005,
	HV_X64_REGISTER_PEBS_LD_LAT		= 0x00081006,
	HV_X64_REGISTER_PEBS_FRONTEND		= 0x00081007,
	HV_X64_REGISTER_PERF_EVT_SEL0		= 0x00081100,
	HV_X64_REGISTER_PMC0			= 0x00081200,
	HV_X64_REGISTER_FIXED_CTR0		= 0x00081300,

	HV_X64_REGISTER_LBR_TOS		= 0x00082000,
	HV_X64_REGISTER_LBR_SELECT	= 0x00082001,
	HV_X64_REGISTER_LER_FROM_LIP	= 0x00082002,
	HV_X64_REGISTER_LER_TO_LIP	= 0x00082003,
	HV_X64_REGISTER_LBR_FROM0	= 0x00082100,
	HV_X64_REGISTER_LBR_TO0		= 0x00082200,
	HV_X64_REGISTER_LBR_INFO0	= 0x00083300,

	/* Intel processor trace MSRs */
	HV_X64_REGISTER_RTIT_CTL		= 0x00081008,
	HV_X64_REGISTER_RTIT_STATUS		= 0x00081009,
	HV_X64_REGISTER_RTIT_OUTPUT_BASE	= 0x0008100A,
	HV_X64_REGISTER_RTIT_OUTPUT_MASK_PTRS	= 0x0008100B,
	HV_X64_REGISTER_RTIT_CR3_MATCH		= 0x0008100C,
	HV_X64_REGISTER_RTIT_ADDR0A		= 0x00081400,

	/* RtitAddr0A/B - RtitAddr3A/B occupy 0x00081400-0x00081407. */

	/* X64 Apic registers. These match the equivalent x2APIC MSR offsets. */
	HV_X64_REGISTER_APIC_ID		= 0x00084802,
	HV_X64_REGISTER_APIC_VERSION	= 0x00084803,

	/* Hypervisor-defined registers (Misc) */
	HV_X64_REGISTER_HYPERCALL	= 0x00090001,

	/* X64 Virtual APIC registers synthetic MSRs */
	HV_X64_REGISTER_SYNTHETIC_EOI	= 0x00090010,
	HV_X64_REGISTER_SYNTHETIC_ICR	= 0x00090011,
	HV_X64_REGISTER_SYNTHETIC_TPR	= 0x00090012,

	/* Partition Timer Assist Registers */
	HV_X64_REGISTER_EMULATED_TIMER_PERIOD	= 0x00090030,
	HV_X64_REGISTER_EMULATED_TIMER_CONTROL	= 0x00090031,
	HV_X64_REGISTER_PM_TIMER_ASSIST		= 0x00090032,

	/* Intercept Control Registers */
	HV_X64_REGISTER_CR_INTERCEPT_CONTROL			= 0x000E0000,
	HV_X64_REGISTER_CR_INTERCEPT_CR0_MASK			= 0x000E0001,
	HV_X64_REGISTER_CR_INTERCEPT_CR4_MASK			= 0x000E0002,
	HV_X64_REGISTER_CR_INTERCEPT_IA32_MISC_ENABLE_MASK	= 0x000E0003,
};

struct hv_u128 {
	__u64 high_part;
	__u64 low_part;
} __packed;

union hv_x64_fp_register {
	struct hv_u128 as_uint128;
	struct {
		__u64 mantissa;
		__u64 biased_exponent : 15;
		__u64 sign : 1;
		__u64 reserved : 48;
	} __packed;
} __packed;

union hv_x64_fp_control_status_register {
	struct hv_u128 as_uint128;
	struct {
		__u16 fp_control;
		__u16 fp_status;
		__u8 fp_tag;
		__u8 reserved;
		__u16 last_fp_op;
		union {
			/* long mode */
			__u64 last_fp_rip;
			/* 32 bit mode */
			struct {
				__u32 last_fp_eip;
				__u16 last_fp_cs;
				__u16 padding;
			} __packed;
		};
	} __packed;
} __packed;

union hv_x64_xmm_control_status_register {
	struct hv_u128 as_uint128;
	struct {
		union {
			/* long mode */
			__u64 last_fp_rdp;
			/* 32 bit mode */
			struct {
				__u32 last_fp_dp;
				__u16 last_fp_ds;
				__u16 padding;
			} __packed;
		};
		__u32 xmm_status_control;
		__u32 xmm_status_control_mask;
	} __packed;
} __packed;

struct hv_x64_segment_register {
	__u64 base;
	__u32 limit;
	__u16 selector;
	union {
		struct {
			__u16 segment_type : 4;
			__u16 non_system_segment : 1;
			__u16 descriptor_privilege_level : 2;
			__u16 present : 1;
			__u16 reserved : 4;
			__u16 available : 1;
			__u16 _long : 1;
			__u16 _default : 1;
			__u16 granularity : 1;
		} __packed;
		__u16 attributes;
	};
} __packed;

struct hv_x64_table_register {
	__u16 pad[3];
	__u16 limit;
	__u64 base;
} __packed;

union hv_explicit_suspend_register {
	__u64 as_uint64;
	struct {
		__u64 suspended : 1;
		__u64 reserved : 63;
	} __packed;
};

union hv_intercept_suspend_register {
	__u64 as_uint64;
	struct {
		__u64 suspended : 1;
		__u64 reserved : 63;
	} __packed;
};

union hv_dispatch_suspend_register {
	__u64 as_uint64;
	struct {
		__u64 suspended : 1;
		__u64 reserved : 63;
	} __packed;
};

union hv_x64_interrupt_state_register {
	__u64 as_uint64;
	struct {
		__u64 interrupt_shadow : 1;
		__u64 nmi_masked : 1;
		__u64 reserved : 62;
	} __packed;
};

union hv_x64_pending_interruption_register {
	__u64 as_uint64;
	struct {
		__u32 interruption_pending : 1;
		__u32 interruption_type : 3;
		__u32 deliver_error_code : 1;
		__u32 instruction_length : 4;
		__u32 nested_event : 1;
		__u32 reserved : 6;
		__u32 interruption_vector : 16;
		__u32 error_code;
	} __packed;
};

union hv_x64_msr_npiep_config_contents {
	__u64 as_uint64;
	struct {
		/*
		 * These bits enable instruction execution prevention for
		 * specific instructions.
		 */
		__u64 prevents_gdt : 1;
		__u64 prevents_idt : 1;
		__u64 prevents_ldt : 1;
		__u64 prevents_tr : 1;

		/* The reserved bits must always be 0. */
		__u64 reserved : 60;
	} __packed;
};

union hv_x64_pending_exception_event {
	__u64 as_uint64[2];
	struct {
		__u32 event_pending : 1;
		__u32 event_type : 3;
		__u32 reserved0 : 4;
		__u32 deliver_error_code : 1;
		__u32 reserved1 : 7;
		__u32 vector : 16;
		__u32 error_code;
		__u64 exception_parameter;
	} __packed;
};

union hv_x64_pending_virtualization_fault_event {
	__u64 as_uint64[2];
	struct {
		__u32 event_pending : 1;
		__u32 event_type : 3;
		__u32 reserved0 : 4;
		__u32 reserved1 : 8;
		__u32 parameter0 : 16;
		__u32 code;
		__u64 parameter1;
	} __packed;
};

union hv_register_value {
	struct hv_u128 reg128;
	__u64 reg64;
	__u32 reg32;
	__u16 reg16;
	__u8 reg8;
	union hv_x64_fp_register fp;
	union hv_x64_fp_control_status_register fp_control_status;
	union hv_x64_xmm_control_status_register xmm_control_status;
	struct hv_x64_segment_register segment;
	struct hv_x64_table_register table;
	union hv_explicit_suspend_register explicit_suspend;
	union hv_intercept_suspend_register intercept_suspend;
	union hv_dispatch_suspend_register dispatch_suspend;
	union hv_x64_interrupt_state_register interrupt_state;
	union hv_x64_pending_interruption_register pending_interruption;
	union hv_x64_msr_npiep_config_contents npiep_config;
	union hv_x64_pending_exception_event pending_exception_event;
	union hv_x64_pending_virtualization_fault_event
		pending_virtualization_fault_event;
};

union hv_x64_vp_execution_state {
	__u16 as_uint16;
	struct {
		__u16 cpl:2;
		__u16 cr0_pe:1;
		__u16 cr0_am:1;
		__u16 efer_lma:1;
		__u16 debug_active:1;
		__u16 interruption_pending:1;
		__u16 vtl:4;
		__u16 enclave_mode:1;
		__u16 interrupt_shadow:1;
		__u16 virtualization_fault_active:1;
		__u16 reserved:2;
	} __packed;
};

/* Values for intercept_access_type field */
#define HV_INTERCEPT_ACCESS_READ	0
#define HV_INTERCEPT_ACCESS_WRITE	1
#define HV_INTERCEPT_ACCESS_EXECUTE	2

struct hv_x64_intercept_message_header {
	__u32 vp_index;
	__u8 instruction_length:4;
	__u8 cr8:4; // only set for exo partitions
	__u8 intercept_access_type;
	union hv_x64_vp_execution_state execution_state;
	struct hv_x64_segment_register cs_segment;
	__u64 rip;
	__u64 rflags;
} __packed;

#define HV_HYPERCALL_INTERCEPT_MAX_XMM_REGISTERS 6

struct hv_x64_hypercall_intercept_message {
	struct hv_x64_intercept_message_header header;
	__u64 rax;
	__u64 rbx;
	__u64 rcx;
	__u64 rdx;
	__u64 r8;
	__u64 rsi;
	__u64 rdi;
	struct hv_u128 xmmregisters[HV_HYPERCALL_INTERCEPT_MAX_XMM_REGISTERS];
	struct {
		__u32 isolated:1;
		__u32 reserved:31;
	} __packed;
} __packed;

union hv_x64_register_access_info {
	union hv_register_value source_value;
	__u32 destination_register;
	__u64 source_address;
	__u64 destination_address;
};

struct hv_x64_register_intercept_message {
	struct hv_x64_intercept_message_header header;
	struct {
		__u8 is_memory_op:1;
		__u8 reserved:7;
	} __packed;
	__u8 reserved8;
	__u16 reserved16;
	__u32 register_name;
	union hv_x64_register_access_info access_info;
} __packed;

union hv_x64_memory_access_info {
	__u8 as_uint8;
	struct {
		__u8 gva_valid:1;
		__u8 gva_gpa_valid:1;
		__u8 hypercall_output_pending:1;
		__u8 tlb_locked_no_overlay:1;
		__u8 reserved:4;
	} __packed;
};

union hv_x64_io_port_access_info {
	__u8 as_uint8;
	struct {
		__u8 access_size:3;
		__u8 string_op:1;
		__u8 rep_prefix:1;
		__u8 reserved:3;
	} __packed;
};

union hv_x64_exception_info {
	__u8 as_uint8;
	struct {
		__u8 error_code_valid:1;
		__u8 software_exception:1;
		__u8 reserved:6;
	} __packed;
};

#define HV_CACHE_TYPE_UNCACHED		0
#define HV_CACHE_TYPE_WRITE_COMBINING	1
#define HV_CACHE_TYPE_WRITE_THROUGH	4
#define HV_CACHE_TYPE_WRITE_PROTECTED	5
#define HV_CACHE_TYPE_WRITE_BACK	6

struct hv_x64_memory_intercept_message {
	struct hv_x64_intercept_message_header header;
	__u32 cache_type;
	__u8 instruction_byte_count;
	union hv_x64_memory_access_info memory_access_info;
	__u8 tpr_priority;
	__u8 reserved1;
	__u64 guest_virtual_address;
	__u64 guest_physical_address;
	__u8 instruction_bytes[16];
} __packed;

struct hv_x64_cpuid_intercept_message {
	struct hv_x64_intercept_message_header header;
	__u64 rax;
	__u64 rcx;
	__u64 rdx;
	__u64 rbx;
	__u64 default_result_rax;
	__u64 default_result_rcx;
	__u64 default_result_rdx;
	__u64 default_result_rbx;
} __packed;

struct hv_x64_msr_intercept_message {
	struct hv_x64_intercept_message_header header;
	__u32 msr_number;
	__u32 reserved;
	__u64 rdx;
	__u64 rax;
} __packed;

struct hv_x64_io_port_intercept_message {
	struct hv_x64_intercept_message_header header;
	__u16 port_number;
	union hv_x64_io_port_access_info access_info;
	__u8 instruction_byte_count;
	__u32 reserved;
	__u64 rax;
	__u8 instruction_bytes[16];
	struct hv_x64_segment_register ds_segment;
	struct hv_x64_segment_register es_segment;
	__u64 rcx;
	__u64 rsi;
	__u64 rdi;
} __packed;

struct hv_x64_exception_intercept_message {
	struct hv_x64_intercept_message_header header;
	__u16 exception_vector;
	union hv_x64_exception_info exception_info;
	__u8 instruction_byte_count;
	__u32 error_code;
	__u64 exception_parameter;
	__u64 reserved;
	__u8 instruction_bytes[16];
	struct hv_x64_segment_register ds_segment;
	struct hv_x64_segment_register ss_segment;
	__u64 rax;
	__u64 rcx;
	__u64 rdx;
	__u64 rbx;
	__u64 rsp;
	__u64 rbp;
	__u64 rsi;
	__u64 rdi;
	__u64 r8;
	__u64 r9;
	__u64 r10;
	__u64 r11;
	__u64 r12;
	__u64 r13;
	__u64 r14;
	__u64 r15;
} __packed;

struct hv_x64_invalid_vp_register_message {
	__u32 vp_index;
	__u32 reserved;
} __packed;

struct hv_x64_unrecoverable_exception_message {
	struct hv_x64_intercept_message_header header;
} __packed;

#define HV_UNSUPPORTED_FEATURE_INTERCEPT	1
#define HV_UNSUPPORTED_FEATURE_TASK_SWITCH_TSS	2

struct hv_x64_unsupported_feature_message {
	__u32 vp_index;
	__u32 feature_code;
	__u64 feature_parameter;
} __packed;

struct hv_x64_halt_message {
	struct hv_x64_intercept_message_header header;
} __packed;

#define HV_X64_PENDING_INTERRUPT	0
#define HV_X64_PENDING_NMI		2
#define HV_X64_PENDING_EXCEPTION	3

struct hv_x64_interruption_deliverable_message {
	struct hv_x64_intercept_message_header header;
	__u32 deliverable_type; /* pending interruption type */
	__u32 rsvd;
} __packed;

struct hv_x64_sipi_intercept_message {
	struct hv_x64_intercept_message_header header;
	__u32 target_vp_index;
	__u32 interrupt_vector;
} __packed;

struct hv_x64_apic_eoi_message {
	__u32 vp_index;
	__u32 interrupt_vector;
} __packed;

enum hv_intercept_type {
	HV_INTERCEPT_TYPE_X64_IO_PORT			= 0X00000000,
	HV_INTERCEPT_TYPE_X64_MSR			= 0X00000001,
	HV_INTERCEPT_TYPE_X64_CPUID			= 0X00000002,
	HV_INTERCEPT_TYPE_EXCEPTION			= 0X00000003,
	HV_INTERCEPT_TYPE_REGISTER			= 0X00000004,
	HV_INTERCEPT_TYPE_MMIO				= 0X00000005,
	HV_INTERCEPT_TYPE_X64_GLOBAL_CPUID		= 0X00000006,
	HV_INTERCEPT_TYPE_X64_APIC_SMI			= 0X00000007,
	HV_INTERCEPT_TYPE_HYPERCALL			= 0X00000008,
	HV_INTERCEPT_TYPE_X64_APIC_INIT_SIPI		= 0X00000009,
	HV_INTERCEPT_MC_UPDATE_PATCH_LEVEL_MSR_READ	= 0X0000000A,
	HV_INTERCEPT_TYPE_X64_APIC_WRITE		= 0X0000000B,
	HV_INTERCEPT_TYPE_MAX,
	HV_INTERCEPT_TYPE_INVALID			= 0XFFFFFFFF,
};

union hv_intercept_parameters {
	__u64 as_uint64;

	/* hv_intercept_type_x64_io_port */
	__u16 io_port;

	/* hv_intercept_type_x64_cpuid */
	__u32 cpuid_index;

	/* hv_intercept_type_x64_apic_write */
	__u32 apic_write_mask;

	/* hv_intercept_type_exception */
	__u16 exception_vector;

	/* N.B. Other intercept types do not have any parameters. */
};

/* Access types for the install intercept hypercall parameter */
#define HV_INTERCEPT_ACCESS_MASK_NONE		0x00
#define HV_INTERCEPT_ACCESS_MASK_READ		0X01
#define HV_INTERCEPT_ACCESS_MASK_WRITE		0x02
#define HV_INTERCEPT_ACCESS_MASK_EXECUTE	0x04

enum hv_interrupt_type {
	HV_X64_INTERRUPT_TYPE_FIXED             = 0x0000,
	HV_X64_INTERRUPT_TYPE_LOWESTPRIORITY    = 0x0001,
	HV_X64_INTERRUPT_TYPE_SMI               = 0x0002,
	HV_X64_INTERRUPT_TYPE_REMOTEREAD        = 0x0003,
	HV_X64_INTERRUPT_TYPE_NMI               = 0x0004,
	HV_X64_INTERRUPT_TYPE_INIT              = 0x0005,
	HV_X64_INTERRUPT_TYPE_SIPI              = 0x0006,
	HV_X64_INTERRUPT_TYPE_EXTINT            = 0x0007,
	HV_X64_INTERRUPT_TYPE_LOCALINT0         = 0x0008,
	HV_X64_INTERRUPT_TYPE_LOCALINT1         = 0x0009,
	HV_X64_INTERRUPT_TYPE_MAXIMUM           = 0x000A
};

union hv_interrupt_control {
	struct {
		__u32 interrupt_type; /* enum hv_interrupt type */
		__u32 level_triggered : 1;
		__u32 logical_dest_mode : 1;
		__u32 rsvd : 30;
	} __packed;
	__u64 as_uint64;
};

struct hv_local_interrupt_controller_state {
	__u32 apic_id;
	__u32 apic_version;
	__u32 apic_ldr;
	__u32 apic_dfr;
	__u32 apic_spurious;
	__u32 apic_isr[8];
	__u32 apic_tmr[8];
	__u32 apic_irr[8];
	__u32 apic_esr;
	__u32 apic_icr_high;
	__u32 apic_icr_low;
	__u32 apic_lvt_timer;
	__u32 apic_lvt_thermal;
	__u32 apic_lvt_perfmon;
	__u32 apic_lvt_lint0;
	__u32 apic_lvt_lint1;
	__u32 apic_lvt_error;
	__u32 apic_lvt_cmci;
	__u32 apic_error_status;
	__u32 apic_initial_count;
	__u32 apic_counter_value;
	__u32 apic_divide_configuration;
	__u32 apic_remote_read;
} __packed;

#define HV_XSAVE_DATA_NO_XMM_REGISTERS 1

union hv_x64_xsave_xfem_register {
	__u64 as_uint64;
	struct {
		__u32 low_uint32;
		__u32 high_uint32;
	} __packed;
	struct {
		__u64 legacy_x87: 1;
		__u64 legacy_sse: 1;
		__u64 avx: 1;
		__u64 mpx_bndreg: 1;
		__u64 mpx_bndcsr: 1;
		__u64 avx_512_op_mask: 1;
		__u64 avx_512_zmmhi: 1;
		__u64 avx_512_zmm16_31: 1;
		__u64 rsvd8_9: 2;
		__u64 pasid: 1;
		__u64 cet_u: 1;
		__u64 cet_s: 1;
		__u64 rsvd13_16: 4;
		__u64 xtile_cfg: 1;
		__u64 xtile_data: 1;
		__u64 rsvd19_63: 45;
	} __packed;
};

struct hv_vp_state_data_xsave {
	__u64 flags;
	union hv_x64_xsave_xfem_register states;
} __packed;

/* Bits for dirty mask of hv_vp_register_page */
#define HV_X64_REGISTER_CLASS_GENERAL	0
#define HV_X64_REGISTER_CLASS_IP	1
#define HV_X64_REGISTER_CLASS_XMM	2
#define HV_X64_REGISTER_CLASS_SEGMENT	3
#define HV_X64_REGISTER_CLASS_FLAGS	4

#define HV_VP_REGISTER_PAGE_VERSION_1	1u

struct hv_vp_register_page {
	__u16 version;
	__u8 isvalid;
	__u8 rsvdz;
	__u32 dirty;
	union {
		struct {
			__u64 rax;
			__u64 rcx;
			__u64 rdx;
			__u64 rbx;
			__u64 rsp;
			__u64 rbp;
			__u64 rsi;
			__u64 rdi;
			__u64 r8;
			__u64 r9;
			__u64 r10;
			__u64 r11;
			__u64 r12;
			__u64 r13;
			__u64 r14;
			__u64 r15;
		} __packed;

		__u64 gp_registers[16];
	};
	__u64 rip;
	__u64 rflags;
	union {
		struct {
			struct hv_u128 xmm0;
			struct hv_u128 xmm1;
			struct hv_u128 xmm2;
			struct hv_u128 xmm3;
			struct hv_u128 xmm4;
			struct hv_u128 xmm5;
		} __packed;

		struct hv_u128 xmm_registers[6];
	};
	union {
		struct {
			struct hv_x64_segment_register es;
			struct hv_x64_segment_register cs;
			struct hv_x64_segment_register ss;
			struct hv_x64_segment_register ds;
			struct hv_x64_segment_register fs;
			struct hv_x64_segment_register gs;
		} __packed;

		struct hv_x64_segment_register segment_registers[6];
	};
	/* read only */
	__u64 cr0;
	__u64 cr3;
	__u64 cr4;
	__u64 cr8;
	__u64 efer;
	__u64 dr7;
	union hv_x64_pending_interruption_register pending_interruption;
	union hv_x64_interrupt_state_register interrupt_state;
	__u64 instruction_emulation_hints;
} __packed;

#endif
