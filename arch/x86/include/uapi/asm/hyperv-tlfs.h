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
	};
	__u64 as_uint64[HV_PARTITION_PROCESSOR_FEATURE_BANKS];
};

union hv_partition_processor_xsave_features {
	struct {
		__u64 xsave_support : 1;
		__u64 xsaveopt_support : 1;
		__u64 avx_support : 1;
		__u64 reserved1 : 61;
	};
	__u64 as_uint64;
};

struct hv_partition_creation_properties {
	union hv_partition_processor_features disabled_processor_features;
	union hv_partition_processor_xsave_features
		disabled_processor_xsave_features;
};

#endif
