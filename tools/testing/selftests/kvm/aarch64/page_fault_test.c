// SPDX-License-Identifier: GPL-2.0
/*
 * page_fault_test.c - Test stage 2 faults.
 *
 * This test tries different combinations of guest accesses (e.g., write,
 * S1PTW), backing source type (e.g., anon) and types of faults (e.g., read on
 * hugetlbfs with a hole). It checks that the expected handling method is
 * called (e.g., uffd faults with the right address and write/read flag).
 */

#define _GNU_SOURCE
#include <linux/bitmap.h>
#include <fcntl.h>
#include <test_util.h>
#include <kvm_util.h>
#include <processor.h>
#include "guest_modes.h"
#include "userfaultfd_util.h"

#define VCPU_ID					0

#define TEST_MEM_SLOT_INDEX			1
#define TEST_PT_SLOT_INDEX			2

/* Max number of backing pages per guest page */
#define BACKING_PG_PER_GUEST_PG			(64 / 4)

/* Test memslot in backing source pages */
#define TEST_MEMSLOT_BACKING_SRC_NPAGES		(1 * BACKING_PG_PER_GUEST_PG)

/* PT memslot size in backing source pages */
#define PT_MEMSLOT_BACKING_SRC_NPAGES		(4 * BACKING_PG_PER_GUEST_PG)

/* Guest virtual addresses that point to the test page and its PTE. */
#define GUEST_TEST_GVA				0xc0000000
#define GUEST_TEST_EXEC_GVA			0xc0000008
#define GUEST_TEST_PTE_GVA			0xd0000000

/* Access flag */
#define PTE_AF					(1ULL << 10)

/* Acces flag update enable/disable */
#define TCR_EL1_HA				(1ULL << 39)

#define CMD_SKIP_TEST				(-1LL)
#define CMD_HOLE_PT				(1ULL << 2)
#define CMD_HOLE_TEST				(1ULL << 3)

#define PREPARE_FN_NR				10
#define CHECK_FN_NR				10

static const uint64_t test_gva = GUEST_TEST_GVA;
static const uint64_t test_exec_gva = GUEST_TEST_EXEC_GVA;
static const uint64_t pte_gva = GUEST_TEST_PTE_GVA;
uint64_t pte_gpa;

enum { PT, TEST, NR_MEMSLOTS};

struct memslot_desc {
	void *hva;
	uint64_t gpa;
	uint64_t size;
	uint64_t guest_pages;
	uint64_t backing_pages;
	enum vm_mem_backing_src_type src_type;
	uint32_t idx;
} memslot[NR_MEMSLOTS] = {
	{
		.idx = TEST_PT_SLOT_INDEX,
		.backing_pages = PT_MEMSLOT_BACKING_SRC_NPAGES,
	},
	{
		.idx = TEST_MEM_SLOT_INDEX,
		.backing_pages = TEST_MEMSLOT_BACKING_SRC_NPAGES,
	},
};

static struct event_cnt {
	int aborts;
	int fail_vcpu_runs;
} events;

struct test_desc {
	const char *name;
	uint64_t mem_mark_cmd;
	/* Skip the test if any prepare function returns false */
	bool (*guest_prepare[PREPARE_FN_NR])(void);
	void (*guest_test)(void);
	void (*guest_test_check[CHECK_FN_NR])(void);
	void (*dabt_handler)(struct ex_regs *regs);
	void (*iabt_handler)(struct ex_regs *regs);
	uint32_t pt_memslot_flags;
	uint32_t test_memslot_flags;
	void (*guest_pre_run)(struct kvm_vm *vm);
	bool skip;
	struct event_cnt expected_events;
};

struct test_params {
	enum vm_mem_backing_src_type src_type;
	struct test_desc *test_desc;
};


static inline void flush_tlb_page(uint64_t vaddr)
{
	uint64_t page = vaddr >> 12;

	dsb(ishst);
	asm("tlbi vaae1is, %0" :: "r" (page));
	dsb(ish);
	isb();
}

#define RET			0xd65f03c0
#define MOV_X0(x)		(0xd2800000 | (((x) & 0xffff) << 5))

static void guest_test_nop(void)
{}

static void guest_test_write64(void)
{
	uint64_t val;

	WRITE_ONCE(*((uint64_t *)test_gva), 0x0123456789ABCDEF);
	val = READ_ONCE(*(uint64_t *)test_gva);
	GUEST_ASSERT_EQ(val, 0x0123456789ABCDEF);
}

/* Check the system for atomic instructions. */
static bool guest_check_lse(void)
{
	uint64_t isar0 = read_sysreg(id_aa64isar0_el1);
	uint64_t atomic = (isar0 >> 20) & 7;

	return atomic >= 2;
}

/* Compare and swap instruction. */
static void guest_test_cas(void)
{
	uint64_t val;
	uint64_t addr = test_gva;

	GUEST_ASSERT_EQ(guest_check_lse(), 1);
	asm volatile(".arch_extension lse\n"
		     "casal %0, %1, [%2]\n"
			:: "r" (0), "r" (0x0123456789ABCDEF), "r" (addr));
	val = READ_ONCE(*(uint64_t *)(addr));
	GUEST_ASSERT_EQ(val, 0x0123456789ABCDEF);
}

static void guest_test_read64(void)
{
	uint64_t val;

	val = READ_ONCE(*(uint64_t *)test_gva);
	GUEST_ASSERT_EQ(val, 0);
}

/* Address translation instruction */
static void guest_test_at(void)
{
	uint64_t par;
	uint64_t addr = 0;

	asm volatile("at s1e1r, %0" :: "r" (test_gva));
	par = read_sysreg(par_el1);

	/* Bit 1 indicates whether the AT was successful */
	GUEST_ASSERT_EQ(par & 1, 0);
	/* The PA in bits [51:12] */
	addr = par & (((1ULL << 40) - 1) << 12);
	GUEST_ASSERT_EQ(addr, memslot[TEST].gpa);
}

static void guest_test_dc_zva(void)
{
	/* The smallest guaranteed block size (bs) is a word. */
	uint16_t val;

	asm volatile("dc zva, %0\n"
			"dsb ish\n"
			:: "r" (test_gva));
	val = READ_ONCE(*(uint16_t *)test_gva);
	GUEST_ASSERT_EQ(val, 0);
}

static void guest_test_ld_preidx(void)
{
	uint64_t val;
	uint64_t addr = test_gva - 8;

	/*
	 * This ends up accessing "test_gva + 8 - 8", where "test_gva - 8"
	 * is not backed by a memslot.
	 */
	asm volatile("ldr %0, [%1, #8]!"
			: "=r" (val), "+r" (addr));
	GUEST_ASSERT_EQ(val, 0);
	GUEST_ASSERT_EQ(addr, test_gva);
}

static void guest_test_st_preidx(void)
{
	uint64_t val = 0x0123456789ABCDEF;
	uint64_t addr = test_gva - 8;

	asm volatile("str %0, [%1, #8]!"
			: "+r" (val), "+r" (addr));

	GUEST_ASSERT_EQ(addr, test_gva);
	val = READ_ONCE(*(uint64_t *)test_gva);
}

static bool guest_set_ha(void)
{
	uint64_t mmfr1 = read_sysreg(id_aa64mmfr1_el1);
	uint64_t hadbs = mmfr1 & 6;
	uint64_t tcr;

	/* Skip if HA is not supported. */
	if (hadbs == 0)
		return false;

	tcr = read_sysreg(tcr_el1) | TCR_EL1_HA;
	write_sysreg(tcr, tcr_el1);
	isb();

	return true;
}

static bool guest_clear_pte_af(void)
{
	*((uint64_t *)pte_gva) &= ~PTE_AF;
	flush_tlb_page(pte_gva);

	return true;
}

static void guest_check_pte_af(void)
{
	flush_tlb_page(pte_gva);
	GUEST_ASSERT_EQ(*((uint64_t *)pte_gva) & PTE_AF, PTE_AF);
}

static void guest_test_exec(void)
{
	int (*code)(void) = (int (*)(void))test_exec_gva;
	int ret;

	ret = code();
	GUEST_ASSERT_EQ(ret, 0x77);
}

static bool guest_prepare(struct test_desc *test)
{
	bool (*prepare_fn)(void);
	int i;

	for (i = 0; i < PREPARE_FN_NR; i++) {
		prepare_fn = test->guest_prepare[i];
		if (prepare_fn && !prepare_fn())
			return false;
	}

	return true;
}

static void guest_test_check(struct test_desc *test)
{
	void (*check_fn)(void);
	int i;

	for (i = 0; i < CHECK_FN_NR; i++) {
		check_fn = test->guest_test_check[i];
		if (!check_fn)
			continue;
		check_fn();
	}
}

static void guest_code(struct test_desc *test)
{
	if (!test->guest_test)
		test->guest_test = guest_test_nop;

	if (!guest_prepare(test))
		GUEST_SYNC(CMD_SKIP_TEST);

	GUEST_SYNC(test->mem_mark_cmd);
	test->guest_test();

	guest_test_check(test);
	GUEST_DONE();
}

static void no_dabt_handler(struct ex_regs *regs)
{
	GUEST_ASSERT_1(false, read_sysreg(far_el1));
}

static void no_iabt_handler(struct ex_regs *regs)
{
	GUEST_ASSERT_1(false, regs->pc);
}

static void punch_hole_in_memslot(struct kvm_vm *vm,
		struct memslot_desc *memslot)
{
	int ret, fd;
	void *hva;

	fd = vm_mem_region_get_src_fd(vm, memslot->idx);
	if (fd != -1) {
		ret = fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
				0, memslot->size);
		TEST_ASSERT(ret == 0, "fallocate failed, errno: %d\n", errno);
	} else {
		hva = addr_gpa2hva(vm, memslot->gpa);
		ret = madvise(hva, memslot->size, MADV_DONTNEED);
		TEST_ASSERT(ret == 0, "madvise failed, errno: %d\n", errno);
	}
}

static void handle_cmd(struct kvm_vm *vm, int cmd)
{
	if (cmd & CMD_HOLE_PT)
		punch_hole_in_memslot(vm, &memslot[PT]);
	if (cmd & CMD_HOLE_TEST)
		punch_hole_in_memslot(vm, &memslot[TEST]);
}

static void sync_stats_from_guest(struct kvm_vm *vm)
{
	struct event_cnt *ec = addr_gva2hva(vm, (uint64_t)&events);

	events.aborts += ec->aborts;
}

void fail_vcpu_run_no_handler(int ret)
{
	TEST_FAIL("Unexpected vcpu run failure\n");
}

static uint64_t get_total_guest_pages(enum vm_guest_mode mode,
		struct test_params *p)
{
	uint64_t large_page_size = get_backing_src_pagesz(p->src_type);
	uint64_t guest_page_size = vm_guest_mode_params[mode].page_size;
	uint64_t size;

	size = PT_MEMSLOT_BACKING_SRC_NPAGES * large_page_size;
	size += TEST_MEMSLOT_BACKING_SRC_NPAGES * large_page_size;

	return size / guest_page_size;
}

static void load_exec_code_for_test(void)
{
	uint32_t *code;

	/* Write this "code" into test_exec_gva */
	assert(test_exec_gva - test_gva);
	code = memslot[TEST].hva + 8;

	code[0] = MOV_X0(0x77);
	code[1] = RET;
}

static void setup_guest_args(struct kvm_vm *vm, struct test_desc *test)
{
	vm_vaddr_t test_desc_gva;

	test_desc_gva = vm_vaddr_alloc_page(vm);
	memcpy(addr_gva2hva(vm, test_desc_gva), test,
			sizeof(struct test_desc));
	vcpu_args_set(vm, 0, 1, test_desc_gva);
}

static void setup_abort_handlers(struct kvm_vm *vm, struct test_desc *test)
{
	vm_init_descriptor_tables(vm);
	vcpu_init_descriptor_tables(vm, VCPU_ID);
	if (!test->dabt_handler)
		test->dabt_handler = no_dabt_handler;
	if (!test->iabt_handler)
		test->iabt_handler = no_iabt_handler;
	vm_install_sync_handler(vm, VECTOR_SYNC_CURRENT,
			0x25, test->dabt_handler);
	vm_install_sync_handler(vm, VECTOR_SYNC_CURRENT,
			0x21, test->iabt_handler);
}

static void setup_memslots(struct kvm_vm *vm, enum vm_guest_mode mode,
		struct test_params *p)
{
	uint64_t large_page_size = get_backing_src_pagesz(p->src_type);
	uint64_t guest_page_size = vm_guest_mode_params[mode].page_size;
	struct test_desc *test = p->test_desc;
	uint64_t hole_gpa;
	uint64_t alignment;
	int i;

	/* Calculate the test and PT memslot sizes */
	for (i = 0; i < NR_MEMSLOTS; i++) {
		memslot[i].size = large_page_size * memslot[i].backing_pages;
		memslot[i].guest_pages = memslot[i].size / guest_page_size;
		memslot[i].src_type = p->src_type;
	}

	TEST_ASSERT(memslot[TEST].size >= guest_page_size,
			"The test memslot should have space one guest page.\n");
	TEST_ASSERT(memslot[PT].size >= (4 * guest_page_size),
			"The PT memslot sould have space for 4 guest pages.\n");

	/* Place the memslots GPAs at the end of physical memory */
	alignment = max(large_page_size, guest_page_size);
	memslot[TEST].gpa = (vm_get_max_gfn(vm) - memslot[TEST].guest_pages) *
		guest_page_size;
	memslot[TEST].gpa = align_down(memslot[TEST].gpa, alignment);
	/* Add a 1-guest_page-hole between the two memslots */
	hole_gpa = memslot[TEST].gpa - guest_page_size;
	virt_pg_map(vm, test_gva - guest_page_size, hole_gpa);
	memslot[PT].gpa = hole_gpa - (memslot[PT].guest_pages *
			guest_page_size);
	memslot[PT].gpa = align_down(memslot[PT].gpa, alignment);

	/* Create memslots for and test data and a PTE. */
	vm_userspace_mem_region_add(vm, p->src_type, memslot[PT].gpa,
			memslot[PT].idx, memslot[PT].guest_pages,
			test->pt_memslot_flags);
	vm_userspace_mem_region_add(vm, p->src_type, memslot[TEST].gpa,
			memslot[TEST].idx, memslot[TEST].guest_pages,
			test->test_memslot_flags);

	for (i = 0; i < NR_MEMSLOTS; i++)
		memslot[i].hva = addr_gpa2hva(vm, memslot[i].gpa);

	/* Map the test test_gva using the PT memslot. */
	_virt_pg_map(vm, test_gva, memslot[TEST].gpa,
			4 /* NORMAL (See DEFAULT_MAIR_EL1) */,
			TEST_PT_SLOT_INDEX);

	/*
	 * Find the PTE of the test page and map it in the guest so it can
	 * clear the AF.
	 */
	pte_gpa = vm_get_pte_gpa(vm, test_gva);
	TEST_ASSERT(memslot[PT].gpa <= pte_gpa &&
			pte_gpa < (memslot[PT].gpa + memslot[PT].size),
			"The EPT should be in the PT memslot.");
	/* This is an artibrary requirement just to make things simpler. */
	TEST_ASSERT(pte_gpa % guest_page_size == 0,
			"The pte_gpa (%p) should be aligned to the guest page (%lx).",
			(void *)pte_gpa, guest_page_size);
	virt_pg_map(vm, pte_gva, pte_gpa);
}

static void check_event_counts(struct test_desc *test)
{
	ASSERT_EQ(test->expected_events.aborts,	events.aborts);
}

static void print_test_banner(enum vm_guest_mode mode, struct test_params *p)
{
	struct test_desc *test = p->test_desc;

	pr_debug("Test: %s\n", test->name);
	pr_debug("Testing guest mode: %s\n", vm_guest_mode_string(mode));
	pr_debug("Testing memory backing src type: %s\n",
			vm_mem_backing_src_alias(p->src_type)->name);
}

static void reset_event_counts(void)
{
	memset(&events, 0, sizeof(events));
}

static bool vcpu_run_loop(struct kvm_vm *vm, struct test_desc *test)
{
	bool skip_test = false;
	struct ucall uc;
	int stage;

	for (stage = 0; ; stage++) {
		vcpu_run(vm, VCPU_ID);

		switch (get_ucall(vm, VCPU_ID, &uc)) {
		case UCALL_SYNC:
			if (uc.args[1] == CMD_SKIP_TEST) {
				pr_debug("Skipped.\n");
				skip_test = true;
				goto done;
			}
			handle_cmd(vm, uc.args[1]);
			break;
		case UCALL_ABORT:
			TEST_FAIL("%s at %s:%ld\n\tvalues: %#lx, %#lx",
				(const char *)uc.args[0],
				__FILE__, uc.args[1], uc.args[2], uc.args[3]);
			break;
		case UCALL_DONE:
			pr_debug("Done.\n");
			goto done;
		default:
			TEST_FAIL("Unknown ucall %lu", uc.cmd);
		}
	}

done:
	return skip_test;
}

static void run_test(enum vm_guest_mode mode, void *arg)
{
	struct test_params *p = (struct test_params *)arg;
	struct test_desc *test = p->test_desc;
	struct kvm_vm *vm;
	bool skip_test = false;

	print_test_banner(mode, p);

	vm = vm_create_with_vcpus(mode, 1, DEFAULT_GUEST_PHY_PAGES,
			get_total_guest_pages(mode, p), 0, guest_code, NULL);
	ucall_init(vm, NULL);

	reset_event_counts();
	setup_memslots(vm, mode, p);

	load_exec_code_for_test();
	setup_abort_handlers(vm, test);
	setup_guest_args(vm, test);

	if (test->guest_pre_run)
		test->guest_pre_run(vm);

	sync_global_to_guest(vm, memslot);

	skip_test = vcpu_run_loop(vm, test);

	sync_stats_from_guest(vm);
	ucall_uninit(vm);
	kvm_vm_free(vm);

	if (!skip_test)
		check_event_counts(test);
}

static void for_each_test_and_guest_mode(void (*func)(enum vm_guest_mode, void *),
		enum vm_mem_backing_src_type src_type);

static void help(char *name)
{
	puts("");
	printf("usage: %s [-h] [-s mem-type]\n", name);
	puts("");
	guest_modes_help();
	backing_src_help("-s");
	puts("");
}

int main(int argc, char *argv[])
{
	enum vm_mem_backing_src_type src_type;
	int opt;

	setbuf(stdout, NULL);

	src_type = DEFAULT_VM_MEM_SRC;

	guest_modes_append_default();

	while ((opt = getopt(argc, argv, "hm:s:")) != -1) {
		switch (opt) {
		case 'm':
			guest_modes_cmdline(optarg);
			break;
		case 's':
			src_type = parse_backing_src_type(optarg);
			break;
		case 'h':
		default:
			help(argv[0]);
			exit(0);
		}
	}

	for_each_test_and_guest_mode(run_test, src_type);
	return 0;
}

#define SNAME(s)		#s
#define SCAT(a, b)		SNAME(a ## _ ## b)

#define TEST_BASIC_ACCESS(__a, ...)						\
{										\
	.name			= SNAME(BASIC_ACCESS ## _ ## __a),		\
	.guest_test		= __a,						\
	.expected_events	= { 0 },					\
	__VA_ARGS__								\
}

#define __AF_TEST_ARGS								\
	.guest_prepare		= { guest_set_ha, guest_clear_pte_af, },	\
	.guest_test_check	= { guest_check_pte_af, },			\

#define __AF_LSE_TEST_ARGS							\
	.guest_prepare		= { guest_set_ha, guest_clear_pte_af,		\
				    guest_check_lse, },				\
	.guest_test_check	= { guest_check_pte_af, },			\

#define __PREPARE_LSE_TEST_ARGS							\
	.guest_prepare		= { guest_check_lse, },

#define TEST_HW_ACCESS_FLAG(__a)						\
	TEST_BASIC_ACCESS(__a, __AF_TEST_ARGS)

#define TEST_ACCESS_ON_HOLE_NO_FAULTS(__a, ...)					\
{										\
	.name			= SNAME(ACCESS_ON_HOLE_NO_FAULTS ## _ ## __a),	\
	.guest_test		= __a,						\
	.mem_mark_cmd		= CMD_HOLE_TEST,				\
	.expected_events	= { 0 },					\
	__VA_ARGS__								\
}

static struct test_desc tests[] = {
	/* Check that HW is setting the AF (sanity checks). */
	TEST_HW_ACCESS_FLAG(guest_test_read64),
	TEST_HW_ACCESS_FLAG(guest_test_ld_preidx),
	TEST_BASIC_ACCESS(guest_test_cas, __AF_LSE_TEST_ARGS),
	TEST_HW_ACCESS_FLAG(guest_test_write64),
	TEST_HW_ACCESS_FLAG(guest_test_st_preidx),
	TEST_HW_ACCESS_FLAG(guest_test_dc_zva),
	TEST_HW_ACCESS_FLAG(guest_test_exec),

	/* Accessing a hole shouldn't fault (more sanity checks). */
	TEST_ACCESS_ON_HOLE_NO_FAULTS(guest_test_read64),
	TEST_ACCESS_ON_HOLE_NO_FAULTS(guest_test_cas, __PREPARE_LSE_TEST_ARGS),
	TEST_ACCESS_ON_HOLE_NO_FAULTS(guest_test_ld_preidx),
	TEST_ACCESS_ON_HOLE_NO_FAULTS(guest_test_write64),
	TEST_ACCESS_ON_HOLE_NO_FAULTS(guest_test_at),
	TEST_ACCESS_ON_HOLE_NO_FAULTS(guest_test_dc_zva),
	TEST_ACCESS_ON_HOLE_NO_FAULTS(guest_test_st_preidx),

	{ 0 },
};

static void for_each_test_and_guest_mode(
		void (*func)(enum vm_guest_mode m, void *a),
		enum vm_mem_backing_src_type src_type)
{
	struct test_desc *t;

	for (t = &tests[0]; t->name; t++) {
		if (t->skip)
			continue;

		struct test_params p = {
			.src_type = src_type,
			.test_desc = t,
		};

		for_each_guest_mode(run_test, &p);
	}
}
