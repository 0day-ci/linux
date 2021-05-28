/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_GENERIC_HYPERV_TLFS_USER_H
#define _UAPI_ASM_GENERIC_HYPERV_TLFS_USER_H

#ifndef BIT
#define BIT(X)	(1ULL << (X))
#endif

/* Define synthetic interrupt controller message constants. */
#define HV_MESSAGE_SIZE			(256)
#define HV_MESSAGE_PAYLOAD_BYTE_COUNT	(240)
#define HV_MESSAGE_PAYLOAD_QWORD_COUNT	(30)

/* Define hypervisor message types. */
enum hv_message_type {
	HVMSG_NONE				= 0x00000000,

	/* Memory access messages. */
	HVMSG_UNMAPPED_GPA			= 0x80000000,
	HVMSG_GPA_INTERCEPT			= 0x80000001,

	/* Timer notification messages. */
	HVMSG_TIMER_EXPIRED			= 0x80000010,

	/* Error messages. */
	HVMSG_INVALID_VP_REGISTER_VALUE		= 0x80000020,
	HVMSG_UNRECOVERABLE_EXCEPTION		= 0x80000021,
	HVMSG_UNSUPPORTED_FEATURE		= 0x80000022,

	/* Trace buffer complete messages. */
	HVMSG_EVENTLOG_BUFFERCOMPLETE		= 0x80000040,

	/* Platform-specific processor intercept messages. */
	HVMSG_X64_IO_PORT_INTERCEPT		= 0x80010000,
	HVMSG_X64_MSR_INTERCEPT			= 0x80010001,
	HVMSG_X64_CPUID_INTERCEPT		= 0x80010002,
	HVMSG_X64_EXCEPTION_INTERCEPT		= 0x80010003,
	HVMSG_X64_APIC_EOI			= 0x80010004,
	HVMSG_X64_LEGACY_FP_ERROR		= 0x80010005,
	HVMSG_X64_IOMMU_PRQ			= 0x80010006,
	HVMSG_X64_HALT				= 0x80010007,
	HVMSG_X64_INTERRUPTION_DELIVERABLE	= 0x80010008,
	HVMSG_X64_SIPI_INTERCEPT		= 0x80010009,
};

/* Define synthetic interrupt controller message flags. */
union hv_message_flags {
	__u8 asu8;
	struct {
		__u8 msg_pending:1;
		__u8 reserved:7;
	} __packed;
};

/* Define port identifier type. */
union hv_port_id {
	__u32 asu32;
	struct {
		__u32 id:24;
		__u32 reserved:8;
	} __packed u;
};

/* Define synthetic interrupt controller message header. */
struct hv_message_header {
	__u32 message_type;
	__u8 payload_size;
	union hv_message_flags message_flags;
	__u8 reserved[2];
	union {
		__u64 sender;
		union hv_port_id port;
	};
} __packed;

/* Define synthetic interrupt controller message format. */
struct hv_message {
	struct hv_message_header header;
	union {
		__u64 payload[HV_MESSAGE_PAYLOAD_QWORD_COUNT];
	} u;
} __packed;

/* Userspace-visible partition creation flags */
#define HV_PARTITION_CREATION_FLAG_SMT_ENABLED_GUEST                BIT(0)
#define HV_PARTITION_CREATION_FLAG_GPA_LARGE_PAGES_DISABLED         BIT(3)
#define HV_PARTITION_CREATION_FLAG_GPA_SUPER_PAGES_ENABLED          BIT(4)
#define HV_PARTITION_CREATION_FLAG_LAPIC_ENABLED                    BIT(13)

/* HV Map GPA (Guest Physical Address) Flags */
#define HV_MAP_GPA_PERMISSIONS_NONE     0x0
#define HV_MAP_GPA_READABLE             0x1
#define HV_MAP_GPA_WRITABLE             0x2
#define HV_MAP_GPA_KERNEL_EXECUTABLE    0x4
#define HV_MAP_GPA_USER_EXECUTABLE      0x8
#define HV_MAP_GPA_EXECUTABLE           0xC
#define HV_MAP_GPA_PERMISSIONS_MASK     0xF

struct hv_register_assoc {
	__u32 name;			/* enum hv_register_name */
	__u32 reserved1;
	__u64 reserved2;
	union hv_register_value value;
} __packed;

/*
 * For getting and setting VP state, there are two options based on the state type:
 *
 *     1.) Data that is accessed by PFNs in the input hypercall page. This is used
 *         for state which may not fit into the hypercall pages.
 *     2.) Data that is accessed directly in the input\output hypercall pages.
 *         This is used for state that will always fit into the hypercall pages.
 *
 * In the future this could be dynamic based on the size if needed.
 *
 * Note these hypercalls have an 8-byte aligned variable header size as per the tlfs
 */

#define HV_GET_SET_VP_STATE_TYPE_PFN	BIT(31)

enum hv_get_set_vp_state_type {
	HV_GET_SET_VP_STATE_LOCAL_INTERRUPT_CONTROLLER_STATE = 0,

	HV_GET_SET_VP_STATE_XSAVE		= 1 | HV_GET_SET_VP_STATE_TYPE_PFN,
	/* Synthetic message page */
	HV_GET_SET_VP_STATE_SIM_PAGE		= 2 | HV_GET_SET_VP_STATE_TYPE_PFN,
	/* Synthetic interrupt event flags page. */
	HV_GET_SET_VP_STATE_SIEF_PAGE		= 3 | HV_GET_SET_VP_STATE_TYPE_PFN,

	/* Synthetic timers. */
	HV_GET_SET_VP_STATE_SYNTHETIC_TIMERS	= 4,
};

enum hv_vp_state_page_type {
	HV_VP_STATE_PAGE_REGISTERS = 0,
	HV_VP_STATE_PAGE_COUNT
};

enum hv_partition_property_code {
	/* Privilege properties */
	HV_PARTITION_PROPERTY_PRIVILEGE_FLAGS				= 0x00010000,

	/* Scheduling properties */
	HV_PARTITION_PROPERTY_SUSPEND					= 0x00020000,
	HV_PARTITION_PROPERTY_CPU_RESERVE				= 0x00020001,
	HV_PARTITION_PROPERTY_CPU_CAP					= 0x00020002,
	HV_PARTITION_PROPERTY_CPU_WEIGHT				= 0x00020003,
	HV_PARTITION_PROPERTY_CPU_GROUP_ID				= 0x00020004,

	/* Time properties */
	HV_PARTITION_PROPERTY_TIME_FREEZE				= 0x00030003,

	/* Debugging properties */
	HV_PARTITION_PROPERTY_DEBUG_CHANNEL_ID				= 0x00040000,

	/* Resource properties */
	HV_PARTITION_PROPERTY_VIRTUAL_TLB_PAGE_COUNT			= 0x00050000,
	HV_PARTITION_PROPERTY_VSM_CONFIG				= 0x00050001,
	HV_PARTITION_PROPERTY_ZERO_MEMORY_ON_RESET			= 0x00050002,
	HV_PARTITION_PROPERTY_PROCESSORS_PER_SOCKET			= 0x00050003,
	HV_PARTITION_PROPERTY_NESTED_TLB_SIZE				= 0x00050004,
	HV_PARTITION_PROPERTY_GPA_PAGE_ACCESS_TRACKING			= 0x00050005,
	HV_PARTITION_PROPERTY_VSM_PERMISSIONS_DIRTY_SINCE_LAST_QUERY	= 0x00050006,
	HV_PARTITION_PROPERTY_SGX_LAUNCH_CONTROL_CONFIG			= 0x00050007,
	HV_PARTITION_PROPERTY_DEFAULT_SGX_LAUNCH_CONTROL0		= 0x00050008,
	HV_PARTITION_PROPERTY_DEFAULT_SGX_LAUNCH_CONTROL1		= 0x00050009,
	HV_PARTITION_PROPERTY_DEFAULT_SGX_LAUNCH_CONTROL2		= 0x0005000a,
	HV_PARTITION_PROPERTY_DEFAULT_SGX_LAUNCH_CONTROL3		= 0x0005000b,
	HV_PARTITION_PROPERTY_ISOLATION_STATE				= 0x0005000c,
	HV_PARTITION_PROPERTY_ISOLATION_CONTROL				= 0x0005000d,
	HV_PARTITION_PROPERTY_RDT_L3_COS_INDEX				= 0x0005000e,
	HV_PARTITION_PROPERTY_RDT_RMID					= 0x0005000f,
	HV_PARTITION_PROPERTY_IMPLEMENTED_PHYSICAL_ADDRESS_BITS		= 0x00050010,
	HV_PARTITION_PROPERTY_NON_ARCHITECTURAL_CORE_SHARING		= 0x00050011,
	HV_PARTITION_PROPERTY_HYPERCALL_DOORBELL_PAGE			= 0x00050012,

	/* Compatibility properties */
	HV_PARTITION_PROPERTY_PROCESSOR_VENDOR				= 0x00060000,
	HV_PARTITION_PROPERTY_PROCESSOR_FEATURES_DEPRECATED		= 0x00060001,
	HV_PARTITION_PROPERTY_PROCESSOR_XSAVE_FEATURES			= 0x00060002,
	HV_PARTITION_PROPERTY_PROCESSOR_CL_FLUSH_SIZE			= 0x00060003,
	HV_PARTITION_PROPERTY_ENLIGHTENMENT_MODIFICATIONS		= 0x00060004,
	HV_PARTITION_PROPERTY_COMPATIBILITY_VERSION			= 0x00060005,
	HV_PARTITION_PROPERTY_PHYSICAL_ADDRESS_WIDTH			= 0x00060006,
	HV_PARTITION_PROPERTY_XSAVE_STATES				= 0x00060007,
	HV_PARTITION_PROPERTY_MAX_XSAVE_DATA_SIZE			= 0x00060008,
	HV_PARTITION_PROPERTY_PROCESSOR_CLOCK_FREQUENCY			= 0x00060009,
	HV_PARTITION_PROPERTY_PROCESSOR_FEATURES0			= 0x0006000a,
	HV_PARTITION_PROPERTY_PROCESSOR_FEATURES1			= 0x0006000b,

	/* Guest software properties */
	HV_PARTITION_PROPERTY_GUEST_OS_ID				= 0x00070000,

	/* Nested virtualization properties */
	HV_PARTITION_PROPERTY_PROCESSOR_VIRTUALIZATION_FEATURES		= 0x00080000,
};

#endif
