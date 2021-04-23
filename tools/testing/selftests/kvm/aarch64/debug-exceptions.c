// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE /* for program_invocation_short_name */
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <asm/barrier.h>

#include <linux/compiler.h>

#include <test_util.h>
#include <kvm_util.h>
#include <processor.h>

#define VCPU_ID 0

extern unsigned char sw_bp, hw_bp, bp_svc, bp_brk, hw_wp, ss_start;
static volatile uint64_t sw_bp_addr, hw_bp_addr;
static volatile uint64_t wp_addr, wp_data_addr;
static volatile uint64_t svc_addr;
static volatile uint64_t ss_addr[4], ss_idx;
#define  CAST_TO_PC(v)  ((uint64_t)&(v))

static void reset_debug_state(void)
{
	asm volatile("msr daifset, #8");

	write_sysreg(osdlr_el1, 0);
	write_sysreg(oslar_el1, 0);
	asm volatile("isb" : : : "memory");

	write_sysreg(mdscr_el1, 0);
	/* This test only uses the first bp and wp slot. */
	write_sysreg(dbgbvr0_el1, 0);
	write_sysreg(dbgbcr0_el1, 0);
	write_sysreg(dbgwcr0_el1, 0);
	write_sysreg(dbgwvr0_el1, 0);
	asm volatile("isb" : : : "memory");
}

static void install_wp(uint64_t addr)
{
	uint32_t wcr;
	uint32_t mdscr;

	wcr = DBGWCR_LEN8 | DBGWCR_RD | DBGWCR_WR | DBGWCR_EL1 | DBGWCR_E;
	write_sysreg(dbgwcr0_el1, wcr);
	write_sysreg(dbgwvr0_el1, addr);
	asm volatile("isb" : : : "memory");

	asm volatile("msr daifclr, #8");

	mdscr = read_sysreg(mdscr_el1) | MDSCR_KDE | MDSCR_MDE;
	write_sysreg(mdscr_el1, mdscr);
}

static void install_hw_bp(uint64_t addr)
{
	uint32_t bcr;
	uint32_t mdscr;

	bcr = DBGBCR_LEN8 | DBGBCR_EXEC | DBGBCR_EL1 | DBGBCR_E;
	write_sysreg(dbgbcr0_el1, bcr);
	write_sysreg(dbgbvr0_el1, addr);
	asm volatile("isb" : : : "memory");

	asm volatile("msr daifclr, #8");

	mdscr = read_sysreg(mdscr_el1) | MDSCR_KDE | MDSCR_MDE;
	write_sysreg(mdscr_el1, mdscr);
}

static void install_ss(void)
{
	uint32_t mdscr;

	asm volatile("msr daifclr, #8");

	mdscr = read_sysreg(mdscr_el1) | MDSCR_KDE | MDSCR_SS;
	write_sysreg(mdscr_el1, mdscr);
}

static volatile char write_data;

#define GUEST_ASSERT_EQ(arg1, arg2) \
	GUEST_ASSERT_2((arg1) == (arg2), (arg1), (arg2))

static void guest_code(void)
{
	GUEST_SYNC(0);

	/* Software-breakpoint */
	asm volatile("sw_bp: brk #0");
	GUEST_ASSERT_EQ(sw_bp_addr, CAST_TO_PC(sw_bp));

	GUEST_SYNC(1);

	/* Hardware-breakpoint */
	reset_debug_state();
	install_hw_bp(CAST_TO_PC(hw_bp));
	asm volatile("hw_bp: nop");
	GUEST_ASSERT_EQ(hw_bp_addr, CAST_TO_PC(hw_bp));

	GUEST_SYNC(2);

	/* Hardware-breakpoint + svc */
	reset_debug_state();
	install_hw_bp(CAST_TO_PC(bp_svc));
	asm volatile("bp_svc: svc #0");
	GUEST_ASSERT_EQ(hw_bp_addr, CAST_TO_PC(bp_svc));
	GUEST_ASSERT_EQ(svc_addr, CAST_TO_PC(bp_svc) + 4);

	GUEST_SYNC(3);

	/* Hardware-breakpoint + software-breakpoint */
	reset_debug_state();
	install_hw_bp(CAST_TO_PC(bp_brk));
	asm volatile("bp_brk: brk #0");
	GUEST_ASSERT_EQ(sw_bp_addr, CAST_TO_PC(bp_brk));
	GUEST_ASSERT_EQ(hw_bp_addr, CAST_TO_PC(bp_brk));

	GUEST_SYNC(4);

	/* Watchpoint */
	reset_debug_state();
	install_wp(CAST_TO_PC(write_data));
	write_data = 'x';
	GUEST_ASSERT_EQ(write_data, 'x');
	GUEST_ASSERT_EQ(wp_data_addr, CAST_TO_PC(write_data));

	GUEST_SYNC(5);

	/* Single-step */
	reset_debug_state();
	install_ss();
	ss_idx = 0;
	asm volatile("ss_start:\n"
		     "mrs x0, esr_el1\n"
		     "add x0, x0, #1\n"
		     "msr daifset, #8\n"
		     : : : "x0");
	GUEST_ASSERT_EQ(ss_addr[0], CAST_TO_PC(ss_start));
	GUEST_ASSERT_EQ(ss_addr[1], CAST_TO_PC(ss_start) + 4);
	GUEST_ASSERT_EQ(ss_addr[2], CAST_TO_PC(ss_start) + 8);

	GUEST_DONE();
}

static void guest_sw_bp_handler(struct ex_regs *regs)
{
	sw_bp_addr = regs->pc;
	regs->pc += 4;
}

static void guest_hw_bp_handler(struct ex_regs *regs)
{
	hw_bp_addr = regs->pc;
	regs->pstate |= SPSR_D;
}

static void guest_wp_handler(struct ex_regs *regs)
{
	wp_data_addr = read_sysreg(far_el1);
	wp_addr = regs->pc;
	regs->pstate |= SPSR_D;
}

static void guest_ss_handler(struct ex_regs *regs)
{
	GUEST_ASSERT_1(ss_idx < 4, ss_idx);
	ss_addr[ss_idx++] = regs->pc;
	regs->pstate |= SPSR_SS;
}

static void guest_svc_handler(struct ex_regs *regs)
{
	svc_addr = regs->pc;
}

static int debug_version(struct kvm_vm *vm)
{
	uint64_t id_aa64dfr0;

	get_reg(vm, VCPU_ID, ARM64_SYS_REG(ID_AA64DFR0_EL1), &id_aa64dfr0);
	return id_aa64dfr0 & 0xf;
}

int main(int argc, char *argv[])
{
	struct kvm_vm *vm;
	struct ucall uc;
	int stage;
	int ret;

	vm = vm_create_default(VCPU_ID, 0, guest_code);
	ucall_init(vm, NULL);

	vm_init_descriptor_tables(vm);
	vcpu_init_descriptor_tables(vm, VCPU_ID);

	if (debug_version(vm) < 6) {
		print_skip("Armv8 debug architecture not supported.");
		kvm_vm_free(vm);
		exit(KSFT_SKIP);
	}

	vm_handle_exception(vm, VECTOR_SYNC_EL1,
			ESR_EC_BRK_INS, guest_sw_bp_handler);
	vm_handle_exception(vm, VECTOR_SYNC_EL1,
			ESR_EC_HW_BP_EL1, guest_hw_bp_handler);
	vm_handle_exception(vm, VECTOR_SYNC_EL1,
			ESR_EC_WP_EL1, guest_wp_handler);
	vm_handle_exception(vm, VECTOR_SYNC_EL1,
			ESR_EC_SSTEP_EL1, guest_ss_handler);
	vm_handle_exception(vm, VECTOR_SYNC_EL1,
			ESR_EC_SVC64, guest_svc_handler);

	for (stage = 0; stage < 7; stage++) {
		ret = _vcpu_run(vm, VCPU_ID);

		TEST_ASSERT(ret == 0, "vcpu_run failed: %d\n", ret);
		switch (get_ucall(vm, VCPU_ID, &uc)) {
		case UCALL_SYNC:
			TEST_ASSERT(uc.args[1] == stage,
				"Stage %d: Unexpected sync ucall, got %lx",
				stage, (ulong)uc.args[1]);

			break;
		case UCALL_ABORT:
			TEST_FAIL("%s at %s:%ld\n\tvalues: %#lx, %#lx",
				(const char *)uc.args[0],
				__FILE__, uc.args[1], uc.args[2], uc.args[3]);
			break;
		case UCALL_DONE:
			goto done;
		default:
			TEST_FAIL("Unknown ucall %lu", uc.cmd);
		}
	}

done:
	kvm_vm_free(vm);
	return 0;
}
