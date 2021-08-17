// SPDX-License-Identifier: GPL-2.0

#include "vmcs12.h"

#define ROL16(val, n) ((u16)(((u16)(val) << (n)) | ((u16)(val) >> (16 - (n)))))
#define VMCS12_OFFSET(x) offsetof(struct vmcs12, x)
#define FIELD(number, name)	[ROL16(number, 6)] = VMCS12_OFFSET(name)
#define FIELD64(number, name)						\
	FIELD(number, name),						\
	[ROL16(number##_HIGH, 6)] = VMCS12_OFFSET(name) + sizeof(u32)

const unsigned short vmcs_field_to_offset_table[] = {
	FIELD(VIRTUAL_PROCESSOR_ID, virtual_processor_id),
	FIELD(POSTED_INTR_NV, posted_intr_nv),
	FIELD(GUEST_ES_SELECTOR, guest_es_selector),
	FIELD(GUEST_CS_SELECTOR, guest_cs_selector),
	FIELD(GUEST_SS_SELECTOR, guest_ss_selector),
	FIELD(GUEST_DS_SELECTOR, guest_ds_selector),
	FIELD(GUEST_FS_SELECTOR, guest_fs_selector),
	FIELD(GUEST_GS_SELECTOR, guest_gs_selector),
	FIELD(GUEST_LDTR_SELECTOR, guest_ldtr_selector),
	FIELD(GUEST_TR_SELECTOR, guest_tr_selector),
	FIELD(GUEST_INTR_STATUS, guest_intr_status),
	FIELD(GUEST_PML_INDEX, guest_pml_index),
	FIELD(HOST_ES_SELECTOR, host_es_selector),
	FIELD(HOST_CS_SELECTOR, host_cs_selector),
	FIELD(HOST_SS_SELECTOR, host_ss_selector),
	FIELD(HOST_DS_SELECTOR, host_ds_selector),
	FIELD(HOST_FS_SELECTOR, host_fs_selector),
	FIELD(HOST_GS_SELECTOR, host_gs_selector),
	FIELD(HOST_TR_SELECTOR, host_tr_selector),
	FIELD64(IO_BITMAP_A, io_bitmap_a),
	FIELD64(IO_BITMAP_B, io_bitmap_b),
	FIELD64(MSR_BITMAP, msr_bitmap),
	FIELD64(VM_EXIT_MSR_STORE_ADDR, vm_exit_msr_store_addr),
	FIELD64(VM_EXIT_MSR_LOAD_ADDR, vm_exit_msr_load_addr),
	FIELD64(VM_ENTRY_MSR_LOAD_ADDR, vm_entry_msr_load_addr),
	FIELD64(PML_ADDRESS, pml_address),
	FIELD64(TSC_OFFSET, tsc_offset),
	FIELD64(TSC_MULTIPLIER, tsc_multiplier),
	FIELD64(VIRTUAL_APIC_PAGE_ADDR, virtual_apic_page_addr),
	FIELD64(APIC_ACCESS_ADDR, apic_access_addr),
	FIELD64(POSTED_INTR_DESC_ADDR, posted_intr_desc_addr),
	FIELD64(VM_FUNCTION_CONTROL, vm_function_control),
	FIELD64(EPT_POINTER, ept_pointer),
	FIELD64(EOI_EXIT_BITMAP0, eoi_exit_bitmap0),
	FIELD64(EOI_EXIT_BITMAP1, eoi_exit_bitmap1),
	FIELD64(EOI_EXIT_BITMAP2, eoi_exit_bitmap2),
	FIELD64(EOI_EXIT_BITMAP3, eoi_exit_bitmap3),
	FIELD64(EPTP_LIST_ADDRESS, eptp_list_address),
	FIELD64(VMREAD_BITMAP, vmread_bitmap),
	FIELD64(VMWRITE_BITMAP, vmwrite_bitmap),
	FIELD64(XSS_EXIT_BITMAP, xss_exit_bitmap),
	FIELD64(ENCLS_EXITING_BITMAP, encls_exiting_bitmap),
	FIELD64(GUEST_PHYSICAL_ADDRESS, guest_physical_address),
	FIELD64(VMCS_LINK_POINTER, vmcs_link_pointer),
	FIELD64(GUEST_IA32_DEBUGCTL, guest_ia32_debugctl),
	FIELD64(GUEST_IA32_PAT, guest_ia32_pat),
	FIELD64(GUEST_IA32_EFER, guest_ia32_efer),
	FIELD64(GUEST_IA32_PERF_GLOBAL_CTRL, guest_ia32_perf_global_ctrl),
	FIELD64(GUEST_PDPTR0, guest_pdptr0),
	FIELD64(GUEST_PDPTR1, guest_pdptr1),
	FIELD64(GUEST_PDPTR2, guest_pdptr2),
	FIELD64(GUEST_PDPTR3, guest_pdptr3),
	FIELD64(GUEST_BNDCFGS, guest_bndcfgs),
	FIELD64(HOST_IA32_PAT, host_ia32_pat),
	FIELD64(HOST_IA32_EFER, host_ia32_efer),
	FIELD64(HOST_IA32_PERF_GLOBAL_CTRL, host_ia32_perf_global_ctrl),
	FIELD(PIN_BASED_VM_EXEC_CONTROL, pin_based_vm_exec_control),
	FIELD(CPU_BASED_VM_EXEC_CONTROL, cpu_based_vm_exec_control),
	FIELD(EXCEPTION_BITMAP, exception_bitmap),
	FIELD(PAGE_FAULT_ERROR_CODE_MASK, page_fault_error_code_mask),
	FIELD(PAGE_FAULT_ERROR_CODE_MATCH, page_fault_error_code_match),
	FIELD(CR3_TARGET_COUNT, cr3_target_count),
	FIELD(VM_EXIT_CONTROLS, vm_exit_controls),
	FIELD(VM_EXIT_MSR_STORE_COUNT, vm_exit_msr_store_count),
	FIELD(VM_EXIT_MSR_LOAD_COUNT, vm_exit_msr_load_count),
	FIELD(VM_ENTRY_CONTROLS, vm_entry_controls),
	FIELD(VM_ENTRY_MSR_LOAD_COUNT, vm_entry_msr_load_count),
	FIELD(VM_ENTRY_INTR_INFO_FIELD, vm_entry_intr_info_field),
	FIELD(VM_ENTRY_EXCEPTION_ERROR_CODE, vm_entry_exception_error_code),
	FIELD(VM_ENTRY_INSTRUCTION_LEN, vm_entry_instruction_len),
	FIELD(TPR_THRESHOLD, tpr_threshold),
	FIELD(SECONDARY_VM_EXEC_CONTROL, secondary_vm_exec_control),
	FIELD(VM_INSTRUCTION_ERROR, vm_instruction_error),
	FIELD(VM_EXIT_REASON, vm_exit_reason),
	FIELD(VM_EXIT_INTR_INFO, vm_exit_intr_info),
	FIELD(VM_EXIT_INTR_ERROR_CODE, vm_exit_intr_error_code),
	FIELD(IDT_VECTORING_INFO_FIELD, idt_vectoring_info_field),
	FIELD(IDT_VECTORING_ERROR_CODE, idt_vectoring_error_code),
	FIELD(VM_EXIT_INSTRUCTION_LEN, vm_exit_instruction_len),
	FIELD(VMX_INSTRUCTION_INFO, vmx_instruction_info),
	FIELD(GUEST_ES_LIMIT, guest_es_limit),
	FIELD(GUEST_CS_LIMIT, guest_cs_limit),
	FIELD(GUEST_SS_LIMIT, guest_ss_limit),
	FIELD(GUEST_DS_LIMIT, guest_ds_limit),
	FIELD(GUEST_FS_LIMIT, guest_fs_limit),
	FIELD(GUEST_GS_LIMIT, guest_gs_limit),
	FIELD(GUEST_LDTR_LIMIT, guest_ldtr_limit),
	FIELD(GUEST_TR_LIMIT, guest_tr_limit),
	FIELD(GUEST_GDTR_LIMIT, guest_gdtr_limit),
	FIELD(GUEST_IDTR_LIMIT, guest_idtr_limit),
	FIELD(GUEST_ES_AR_BYTES, guest_es_ar_bytes),
	FIELD(GUEST_CS_AR_BYTES, guest_cs_ar_bytes),
	FIELD(GUEST_SS_AR_BYTES, guest_ss_ar_bytes),
	FIELD(GUEST_DS_AR_BYTES, guest_ds_ar_bytes),
	FIELD(GUEST_FS_AR_BYTES, guest_fs_ar_bytes),
	FIELD(GUEST_GS_AR_BYTES, guest_gs_ar_bytes),
	FIELD(GUEST_LDTR_AR_BYTES, guest_ldtr_ar_bytes),
	FIELD(GUEST_TR_AR_BYTES, guest_tr_ar_bytes),
	FIELD(GUEST_INTERRUPTIBILITY_INFO, guest_interruptibility_info),
	FIELD(GUEST_ACTIVITY_STATE, guest_activity_state),
	FIELD(GUEST_SYSENTER_CS, guest_sysenter_cs),
	FIELD(HOST_IA32_SYSENTER_CS, host_ia32_sysenter_cs),
	FIELD(VMX_PREEMPTION_TIMER_VALUE, vmx_preemption_timer_value),
	FIELD(CR0_GUEST_HOST_MASK, cr0_guest_host_mask),
	FIELD(CR4_GUEST_HOST_MASK, cr4_guest_host_mask),
	FIELD(CR0_READ_SHADOW, cr0_read_shadow),
	FIELD(CR4_READ_SHADOW, cr4_read_shadow),
	FIELD(EXIT_QUALIFICATION, exit_qualification),
	FIELD(GUEST_LINEAR_ADDRESS, guest_linear_address),
	FIELD(GUEST_CR0, guest_cr0),
	FIELD(GUEST_CR3, guest_cr3),
	FIELD(GUEST_CR4, guest_cr4),
	FIELD(GUEST_ES_BASE, guest_es_base),
	FIELD(GUEST_CS_BASE, guest_cs_base),
	FIELD(GUEST_SS_BASE, guest_ss_base),
	FIELD(GUEST_DS_BASE, guest_ds_base),
	FIELD(GUEST_FS_BASE, guest_fs_base),
	FIELD(GUEST_GS_BASE, guest_gs_base),
	FIELD(GUEST_LDTR_BASE, guest_ldtr_base),
	FIELD(GUEST_TR_BASE, guest_tr_base),
	FIELD(GUEST_GDTR_BASE, guest_gdtr_base),
	FIELD(GUEST_IDTR_BASE, guest_idtr_base),
	FIELD(GUEST_DR7, guest_dr7),
	FIELD(GUEST_RSP, guest_rsp),
	FIELD(GUEST_RIP, guest_rip),
	FIELD(GUEST_RFLAGS, guest_rflags),
	FIELD(GUEST_PENDING_DBG_EXCEPTIONS, guest_pending_dbg_exceptions),
	FIELD(GUEST_SYSENTER_ESP, guest_sysenter_esp),
	FIELD(GUEST_SYSENTER_EIP, guest_sysenter_eip),
	FIELD(HOST_CR0, host_cr0),
	FIELD(HOST_CR3, host_cr3),
	FIELD(HOST_CR4, host_cr4),
	FIELD(HOST_FS_BASE, host_fs_base),
	FIELD(HOST_GS_BASE, host_gs_base),
	FIELD(HOST_TR_BASE, host_tr_base),
	FIELD(HOST_GDTR_BASE, host_gdtr_base),
	FIELD(HOST_IDTR_BASE, host_idtr_base),
	FIELD(HOST_IA32_SYSENTER_ESP, host_ia32_sysenter_esp),
	FIELD(HOST_IA32_SYSENTER_EIP, host_ia32_sysenter_eip),
	FIELD(HOST_RSP, host_rsp),
	FIELD(HOST_RIP, host_rip),
};
const unsigned int nr_vmcs12_fields = ARRAY_SIZE(vmcs_field_to_offset_table);

#define FIELD_BIT_SET(name, bitmap) set_bit(f_pos(name), bitmap)
#define FIELD64_BIT_SET(name, bitmap)	\
	do {set_bit(f_pos(name), bitmap);	\
	    set_bit(f_pos(name) + (sizeof(u32) / sizeof(u16)), bitmap);\
	} while (0)

#define FIELD_BIT_CLEAR(name, bitmap) clear_bit(f_pos(name), bitmap)
#define FIELD64_BIT_CLEAR(name, bitmap)	\
	do {clear_bit(f_pos(name), bitmap);	\
	    clear_bit(f_pos(name) + (sizeof(u32) / sizeof(u16)), bitmap);\
	} while (0)

#define FIELD_BIT_CHANGE(name, bitmap) change_bit(f_pos(name), bitmap)
#define FIELD64_BIT_CHANGE(name, bitmap)	\
	do {change_bit(f_pos(name), bitmap);	\
	    change_bit(f_pos(name) + (sizeof(u32) / sizeof(u16)), bitmap);\
	} while (0)

/*
 * Set non-dependent fields to exist
 */
void vmcs12_field_fixed_init(unsigned long *bitmap)
{
	if (unlikely(bitmap == NULL)) {
		pr_err_once("%s: NULL bitmap", __func__);
		return;
	}
	FIELD_BIT_SET(guest_es_selector, bitmap);
	FIELD_BIT_SET(guest_cs_selector, bitmap);
	FIELD_BIT_SET(guest_ss_selector, bitmap);
	FIELD_BIT_SET(guest_ds_selector, bitmap);
	FIELD_BIT_SET(guest_fs_selector, bitmap);
	FIELD_BIT_SET(guest_gs_selector, bitmap);
	FIELD_BIT_SET(guest_ldtr_selector, bitmap);
	FIELD_BIT_SET(guest_tr_selector, bitmap);
	FIELD_BIT_SET(host_es_selector, bitmap);
	FIELD_BIT_SET(host_cs_selector, bitmap);
	FIELD_BIT_SET(host_ss_selector, bitmap);
	FIELD_BIT_SET(host_ds_selector, bitmap);
	FIELD_BIT_SET(host_fs_selector, bitmap);
	FIELD_BIT_SET(host_gs_selector, bitmap);
	FIELD_BIT_SET(host_tr_selector, bitmap);
	FIELD64_BIT_SET(io_bitmap_a, bitmap);
	FIELD64_BIT_SET(io_bitmap_b, bitmap);
	FIELD64_BIT_SET(vm_exit_msr_store_addr, bitmap);
	FIELD64_BIT_SET(vm_exit_msr_load_addr, bitmap);
	FIELD64_BIT_SET(vm_entry_msr_load_addr, bitmap);
	FIELD64_BIT_SET(tsc_offset, bitmap);
	FIELD64_BIT_SET(vmcs_link_pointer, bitmap);
	FIELD64_BIT_SET(guest_ia32_debugctl, bitmap);
	FIELD_BIT_SET(pin_based_vm_exec_control, bitmap);
	FIELD_BIT_SET(cpu_based_vm_exec_control, bitmap);
	FIELD_BIT_SET(exception_bitmap, bitmap);
	FIELD_BIT_SET(page_fault_error_code_mask, bitmap);
	FIELD_BIT_SET(page_fault_error_code_match, bitmap);
	FIELD_BIT_SET(cr3_target_count, bitmap);
	FIELD_BIT_SET(vm_exit_controls, bitmap);
	FIELD_BIT_SET(vm_exit_msr_store_count, bitmap);
	FIELD_BIT_SET(vm_exit_msr_load_count, bitmap);
	FIELD_BIT_SET(vm_entry_controls, bitmap);
	FIELD_BIT_SET(vm_entry_msr_load_count, bitmap);
	FIELD_BIT_SET(vm_entry_intr_info_field, bitmap);
	FIELD_BIT_SET(vm_entry_exception_error_code, bitmap);
	FIELD_BIT_SET(vm_entry_instruction_len, bitmap);
	FIELD_BIT_SET(vm_instruction_error, bitmap);
	FIELD_BIT_SET(vm_exit_reason, bitmap);
	FIELD_BIT_SET(vm_exit_intr_info, bitmap);
	FIELD_BIT_SET(vm_exit_intr_error_code, bitmap);
	FIELD_BIT_SET(idt_vectoring_info_field, bitmap);
	FIELD_BIT_SET(idt_vectoring_error_code, bitmap);
	FIELD_BIT_SET(vm_exit_instruction_len, bitmap);
	FIELD_BIT_SET(vmx_instruction_info, bitmap);
	FIELD_BIT_SET(guest_es_limit, bitmap);
	FIELD_BIT_SET(guest_cs_limit, bitmap);
	FIELD_BIT_SET(guest_ss_limit, bitmap);
	FIELD_BIT_SET(guest_ds_limit, bitmap);
	FIELD_BIT_SET(guest_fs_limit, bitmap);
	FIELD_BIT_SET(guest_gs_limit, bitmap);
	FIELD_BIT_SET(guest_ldtr_limit, bitmap);
	FIELD_BIT_SET(guest_tr_limit, bitmap);
	FIELD_BIT_SET(guest_gdtr_limit, bitmap);
	FIELD_BIT_SET(guest_idtr_limit, bitmap);
	FIELD_BIT_SET(guest_es_ar_bytes, bitmap);
	FIELD_BIT_SET(guest_cs_ar_bytes, bitmap);
	FIELD_BIT_SET(guest_ss_ar_bytes, bitmap);
	FIELD_BIT_SET(guest_ds_ar_bytes, bitmap);
	FIELD_BIT_SET(guest_fs_ar_bytes, bitmap);
	FIELD_BIT_SET(guest_gs_ar_bytes, bitmap);
	FIELD_BIT_SET(guest_ldtr_ar_bytes, bitmap);
	FIELD_BIT_SET(guest_tr_ar_bytes, bitmap);
	FIELD_BIT_SET(guest_interruptibility_info, bitmap);
	FIELD_BIT_SET(guest_activity_state, bitmap);
	FIELD_BIT_SET(guest_sysenter_cs, bitmap);
	FIELD_BIT_SET(host_ia32_sysenter_cs, bitmap);
	FIELD_BIT_SET(cr0_guest_host_mask, bitmap);
	FIELD_BIT_SET(cr4_guest_host_mask, bitmap);
	FIELD_BIT_SET(cr0_read_shadow, bitmap);
	FIELD_BIT_SET(cr4_read_shadow, bitmap);
	FIELD_BIT_SET(exit_qualification, bitmap);
	FIELD_BIT_SET(guest_linear_address, bitmap);
	FIELD_BIT_SET(guest_cr0, bitmap);
	FIELD_BIT_SET(guest_cr3, bitmap);
	FIELD_BIT_SET(guest_cr4, bitmap);
	FIELD_BIT_SET(guest_es_base, bitmap);
	FIELD_BIT_SET(guest_cs_base, bitmap);
	FIELD_BIT_SET(guest_ss_base, bitmap);
	FIELD_BIT_SET(guest_ds_base, bitmap);
	FIELD_BIT_SET(guest_fs_base, bitmap);
	FIELD_BIT_SET(guest_gs_base, bitmap);
	FIELD_BIT_SET(guest_ldtr_base, bitmap);
	FIELD_BIT_SET(guest_tr_base, bitmap);
	FIELD_BIT_SET(guest_gdtr_base, bitmap);
	FIELD_BIT_SET(guest_idtr_base, bitmap);
	FIELD_BIT_SET(guest_dr7, bitmap);
	FIELD_BIT_SET(guest_rsp, bitmap);
	FIELD_BIT_SET(guest_rip, bitmap);
	FIELD_BIT_SET(guest_rflags, bitmap);
	FIELD_BIT_SET(guest_pending_dbg_exceptions, bitmap);
	FIELD_BIT_SET(guest_sysenter_esp, bitmap);
	FIELD_BIT_SET(guest_sysenter_eip, bitmap);
	FIELD_BIT_SET(host_cr0, bitmap);
	FIELD_BIT_SET(host_cr3, bitmap);
	FIELD_BIT_SET(host_cr4, bitmap);
	FIELD_BIT_SET(host_fs_base, bitmap);
	FIELD_BIT_SET(host_gs_base, bitmap);
	FIELD_BIT_SET(host_tr_base, bitmap);
	FIELD_BIT_SET(host_gdtr_base, bitmap);
	FIELD_BIT_SET(host_idtr_base, bitmap);
	FIELD_BIT_SET(host_ia32_sysenter_esp, bitmap);
	FIELD_BIT_SET(host_ia32_sysenter_eip, bitmap);
	FIELD_BIT_SET(host_rsp, bitmap);
	FIELD_BIT_SET(host_rip, bitmap);
}

void vmcs12_field_dynamic_init(struct nested_vmx_msrs *vmx_msrs,
			       unsigned long *bitmap)
{
	if (unlikely(bitmap == NULL)) {
		pr_err_once("%s: NULL bitmap", __func__);
		return;
	}
	vmcs12_field_update_by_pinbased_ctrl(0, vmx_msrs->pinbased_ctls_high,
					     bitmap);

	vmcs12_field_update_by_procbased_ctrl(0, vmx_msrs->procbased_ctls_high,
					      bitmap);

	vmcs12_field_update_by_procbased_ctrl2(0, vmx_msrs->secondary_ctls_high,
					       bitmap);

	vmcs12_field_update_by_vmentry_ctrl(vmx_msrs->exit_ctls_high, 0,
					    vmx_msrs->entry_ctls_high,
					    bitmap);

	vmcs12_field_update_by_vmexit_ctrl(vmx_msrs->entry_ctls_high, 0,
					   vmx_msrs->exit_ctls_high,
					   bitmap);

	vmcs12_field_update_by_vm_func(0, vmx_msrs->vmfunc_controls, bitmap);
}

void vmcs12_field_update_by_pinbased_ctrl(u32 old_val, u32 new_val,
					  unsigned long *bitmap)
{
	if (unlikely(bitmap == NULL)) {
		pr_err_once("%s: NULL bitmap", __func__);
		return;
	}

	if (!(old_val ^ new_val))
		return;
	if ((old_val ^ new_val) & PIN_BASED_POSTED_INTR) {
		FIELD_BIT_CHANGE(posted_intr_nv, bitmap);
		FIELD64_BIT_CHANGE(posted_intr_desc_addr, bitmap);
	}

	if ((old_val ^ new_val) & PIN_BASED_VMX_PREEMPTION_TIMER)
		FIELD_BIT_CHANGE(vmx_preemption_timer_value, bitmap);
}

void vmcs12_field_update_by_procbased_ctrl(u32 old_val, u32 new_val,
					   unsigned long *bitmap)
{
	if (unlikely(bitmap == NULL)) {
		pr_err_once("%s: NULL bitmap", __func__);
		return;
	}
	if (!(old_val ^ new_val))
		return;

	if ((old_val ^ new_val) & CPU_BASED_USE_MSR_BITMAPS)
		FIELD64_BIT_CHANGE(msr_bitmap, bitmap);

	if ((old_val ^ new_val) & CPU_BASED_TPR_SHADOW) {
		FIELD64_BIT_CHANGE(virtual_apic_page_addr, bitmap);
		FIELD_BIT_CHANGE(tpr_threshold, bitmap);
	}

	if ((old_val ^ new_val) &
	    CPU_BASED_ACTIVATE_SECONDARY_CONTROLS) {
		FIELD_BIT_CHANGE(secondary_vm_exec_control, bitmap);
	}
}

void vmcs12_field_update_by_procbased_ctrl2(u32 old_val, u32 new_val,
					    unsigned long *bitmap)
{
	if (unlikely(bitmap == NULL)) {
		pr_err_once("%s: NULL bitmap", __func__);
		return;
	}
	if (!(old_val ^ new_val))
		return;

	if ((old_val ^ new_val) & SECONDARY_EXEC_ENABLE_VPID)
		FIELD_BIT_CHANGE(virtual_processor_id, bitmap);

	if ((old_val ^ new_val) &
	    SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY) {
		FIELD_BIT_CHANGE(guest_intr_status, bitmap);
		FIELD64_BIT_CHANGE(eoi_exit_bitmap0, bitmap);
		FIELD64_BIT_CHANGE(eoi_exit_bitmap1, bitmap);
		FIELD64_BIT_CHANGE(eoi_exit_bitmap2, bitmap);
		FIELD64_BIT_CHANGE(eoi_exit_bitmap3, bitmap);
	}

	if ((old_val ^ new_val) & SECONDARY_EXEC_ENABLE_PML) {
		FIELD_BIT_CHANGE(guest_pml_index, bitmap);
		FIELD64_BIT_CHANGE(pml_address, bitmap);
	}

	if ((old_val ^ new_val) & SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES)
		FIELD64_BIT_CHANGE(apic_access_addr, bitmap);

	if ((old_val ^ new_val) & SECONDARY_EXEC_ENABLE_VMFUNC)
		FIELD64_BIT_CHANGE(vm_function_control, bitmap);

	if ((old_val ^ new_val) & SECONDARY_EXEC_ENABLE_EPT) {
		FIELD64_BIT_CHANGE(ept_pointer, bitmap);
		FIELD64_BIT_CHANGE(guest_physical_address, bitmap);
		FIELD64_BIT_CHANGE(guest_pdptr0, bitmap);
		FIELD64_BIT_CHANGE(guest_pdptr1, bitmap);
		FIELD64_BIT_CHANGE(guest_pdptr2, bitmap);
		FIELD64_BIT_CHANGE(guest_pdptr3, bitmap);
	}

	if ((old_val ^ new_val) & SECONDARY_EXEC_SHADOW_VMCS) {
		FIELD64_BIT_CHANGE(vmread_bitmap, bitmap);
		FIELD64_BIT_CHANGE(vmwrite_bitmap, bitmap);
	}

	if ((old_val ^ new_val) & SECONDARY_EXEC_XSAVES)
		FIELD64_BIT_CHANGE(xss_exit_bitmap, bitmap);

	if ((old_val ^ new_val) & SECONDARY_EXEC_ENCLS_EXITING)
		FIELD64_BIT_CHANGE(encls_exiting_bitmap, bitmap);

	if ((old_val ^ new_val) & SECONDARY_EXEC_TSC_SCALING)
		FIELD64_BIT_CHANGE(tsc_multiplier, bitmap);

	if ((old_val ^ new_val) & SECONDARY_EXEC_PAUSE_LOOP_EXITING) {
		FIELD64_BIT_CHANGE(vmread_bitmap, bitmap);
		FIELD64_BIT_CHANGE(vmwrite_bitmap, bitmap);
	}
}

void vmcs12_field_update_by_vmentry_ctrl(u32 vm_exit_ctrl, u32 old_val,
					 u32 new_val, unsigned long *bitmap)
{
	if (unlikely(bitmap == NULL)) {
		pr_err_once("%s: NULL bitmap", __func__);
		return;
	}
	if (!(old_val ^ new_val))
		return;

	if ((old_val ^ new_val) & VM_ENTRY_LOAD_IA32_PAT) {
		if ((new_val & VM_ENTRY_LOAD_IA32_PAT) ||
		    (vm_exit_ctrl & VM_EXIT_SAVE_IA32_PAT))
			FIELD64_BIT_SET(guest_ia32_pat, bitmap);
		else
			FIELD64_BIT_CLEAR(guest_ia32_pat, bitmap);
	}

	if ((old_val ^ new_val) & VM_ENTRY_LOAD_IA32_EFER) {
		if ((new_val & VM_ENTRY_LOAD_IA32_EFER) ||
		    (vm_exit_ctrl & VM_EXIT_SAVE_IA32_EFER))
			FIELD64_BIT_SET(guest_ia32_efer, bitmap);
		else
			FIELD64_BIT_CLEAR(guest_ia32_efer, bitmap);
	}

	if ((old_val ^ new_val) & VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL)
		FIELD64_BIT_CHANGE(guest_ia32_perf_global_ctrl, bitmap);

	if ((old_val ^ new_val) & VM_ENTRY_LOAD_BNDCFGS) {
		if ((new_val & VM_ENTRY_LOAD_BNDCFGS) ||
		    (vm_exit_ctrl & VM_EXIT_CLEAR_BNDCFGS))
			FIELD64_BIT_SET(guest_bndcfgs, bitmap);
		else
			FIELD64_BIT_CLEAR(guest_bndcfgs, bitmap);
	}
}

void vmcs12_field_update_by_vmexit_ctrl(u32 vm_entry_ctrl, u32 old_val,
					u32 new_val, unsigned long *bitmap)
{
	if (unlikely(bitmap == NULL)) {
		pr_err_once("%s: NULL bitmap", __func__);
		return;
	}
	if (!(old_val ^ new_val))
		return;

	if ((old_val ^ new_val) & VM_EXIT_LOAD_IA32_PAT)
		FIELD64_BIT_CHANGE(host_ia32_pat, bitmap);

	if ((old_val ^ new_val) & VM_EXIT_LOAD_IA32_EFER)
		FIELD64_BIT_CHANGE(host_ia32_efer, bitmap);

	if ((old_val ^ new_val) & VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL)
		FIELD64_BIT_CHANGE(host_ia32_perf_global_ctrl, bitmap);

	if ((old_val ^ new_val) & VM_EXIT_SAVE_IA32_PAT) {
		if ((new_val & VM_EXIT_SAVE_IA32_PAT) ||
		    (vm_entry_ctrl & VM_ENTRY_LOAD_IA32_PAT))
			FIELD64_BIT_SET(guest_ia32_pat, bitmap);
		else
			FIELD64_BIT_CLEAR(guest_ia32_pat, bitmap);
	}

	if ((old_val ^ new_val) & VM_EXIT_SAVE_IA32_EFER) {
		if ((new_val & VM_EXIT_SAVE_IA32_EFER) ||
		    (vm_entry_ctrl & VM_ENTRY_LOAD_IA32_EFER))
			FIELD64_BIT_SET(guest_ia32_efer, bitmap);
		else
			FIELD64_BIT_CLEAR(guest_ia32_efer, bitmap);
	}

	if ((old_val ^ new_val) & VM_EXIT_CLEAR_BNDCFGS) {
		if ((new_val & VM_EXIT_CLEAR_BNDCFGS) ||
		    (vm_entry_ctrl & VM_ENTRY_LOAD_BNDCFGS))
			FIELD64_BIT_SET(guest_bndcfgs, bitmap);
		else
			FIELD64_BIT_CLEAR(guest_bndcfgs, bitmap);
	}
}

void vmcs12_field_update_by_vm_func(u64 old_val, u64 new_val,
				    unsigned long *bitmap)
{
	if (unlikely(bitmap == NULL)) {
		pr_err_once("%s: NULL bitmap", __func__);
		return;
	}

	if (!(old_val ^ new_val))
		return;

	if ((old_val ^ new_val) & VMFUNC_CONTROL_BIT(EPTP_SWITCHING))
		FIELD64_BIT_CHANGE(eptp_list_address, bitmap);
}
