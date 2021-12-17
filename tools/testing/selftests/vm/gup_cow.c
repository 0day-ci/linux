// SPDX-License-Identifier: GPL-2.0-only
/*
 * GUP (Get User Pages) interaction with COW (Copy On Write) tests.
 *
 * Copyright 2021, Red Hat, Inc.
 *
 * Author(s): David Hildenbrand <david@redhat.com>
 */
#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include "../kselftest.h"

#define barrier() asm volatile("" ::: "memory")

static size_t pagesize;
static size_t thpsize;
static size_t hugetlbsize;

struct shared_mem {
	bool parent_ready;
	bool child_ready;
};
struct shared_mem *shared;

static size_t detect_thpsize(void)
{
	int fd = open("/sys/kernel/mm/transparent_hugepage/hpage_pmd_size",
		      O_RDONLY);
	size_t size = 0;
	char buf[15];
	int ret;

	if (fd < 0)
		return 0;

	ret = pread(fd, buf, sizeof(buf), 0);
	if (ret < 0 || ret == sizeof(buf))
		goto out;
	buf[ret] = 0;

	size = strtoul(buf, NULL, 10);
out:
	close(fd);
	if (size < pagesize)
		size = 0;
	return size;
}

static uint64_t pagemap_get_entry(int fd, void *addr)
{
	const unsigned long pfn = (unsigned long)addr / pagesize;
	uint64_t entry;
	int ret;

	ret = pread(fd, &entry, sizeof(entry), pfn * sizeof(entry));
	if (ret != sizeof(entry))
		ksft_exit_fail_msg("reading pagemap failed\n");
	return entry;
}

static bool page_is_populated(void *addr)
{
	int fd = open("/proc/self/pagemap", O_RDONLY);
	uint64_t entry;
	bool ret;

	if (fd < 0)
		ksft_exit_fail_msg("opening pagemap failed\n");

	/* Present or swapped. */
	entry = pagemap_get_entry(fd, addr);
	ret = !!(entry & 0xc000000000000000ull);
	close(fd);
	return ret;
}

static int child_vmsplice_fn(unsigned char *mem, size_t size)
{
	struct iovec iov = {
		.iov_base = mem,
		.iov_len = size,
	};
	size_t cur, total, transferred;
	char *old, *new;
	int fds[2];

	old = malloc(size);
	new = malloc(size);

	/* Backup the original content. */
	memcpy(old, mem, size);

	if (pipe(fds) < 0)
		return -errno;

	/* Trigger a read-only pin. */
	transferred = vmsplice(fds[1], &iov, 1, 0);
	if (transferred < 0)
		return -errno;
	if (transferred == 0)
		return -EINVAL;

	/* Unmap it from our page tables. */
	if (munmap(mem, size) < 0)
		return -errno;

	/* Wait until the parent modified it. */
	barrier();
	shared->child_ready = true;
	barrier();
	while (!shared->parent_ready)
		barrier();
	barrier();

	/* See if we still read the old values. */
	total = 0;
	while (total < transferred) {
		cur = read(fds[0], new + total, transferred - total);
		if (cur < 0)
			return -errno;
		total += cur;
	}

	return memcmp(old, new, transferred);
}

static void test_child_ro_gup(unsigned char *mem, size_t size)
{
	int ret;

	/* Populate the page. */
	memset(mem, 0, size);

	shared->parent_ready = false;
	shared->child_ready = false;
	barrier();

	ret = fork();
	if (ret < 0) {
		ksft_exit_fail_msg("fork failed\n");
	} else if (!ret) {
		ret = child_vmsplice_fn(mem, size);
		exit(ret);
	}

	barrier();
	while (!shared->child_ready)
		barrier();
	/* Modify the page. */
	barrier();
	memset(mem, 0xff, size);
	barrier();
	shared->parent_ready = true;

	wait(&ret);
	if (WIFEXITED(ret))
		ret = WEXITSTATUS(ret);
	else
		ret = -EINVAL;

	ksft_test_result(!ret, "child has correct MAP_PRIVATE semantics\n");
}

static void test_anon_ro_gup_child(void)
{
	unsigned char *mem;
	int ret;

	ksft_print_msg("[RUN] %s\n", __func__);

	mem = mmap(NULL, pagesize, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mem == MAP_FAILED) {
		ksft_test_result_fail("mmap failed\n");
		return;
	}

	ret = madvise(mem, pagesize, MADV_NOHUGEPAGE);
	/* Ignore if not around on a kernel. */
	if (ret && ret != -EINVAL) {
		ksft_test_result_fail("madvise failed\n");
		goto out;
	}

	test_child_ro_gup(mem, pagesize);
out:
	munmap(mem, pagesize);
}

static void test_anon_thp_ro_gup_child(void)
{
	unsigned char *mem, *mmap_mem;
	size_t mmap_size;
	int ret;

	ksft_print_msg("[RUN] %s\n", __func__);

	if (!thpsize)
		ksft_test_result_skip("THP size not detected\n");

	mmap_size = 2 * thpsize;
	mmap_mem = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mmap_mem == MAP_FAILED) {
		ksft_test_result_fail("mmap failed\n");
		return;
	}

	mem = (unsigned char *)(((uintptr_t)mmap_mem + thpsize) & ~(thpsize - 1));

	ret = madvise(mem, thpsize, MADV_HUGEPAGE);
	if (ret) {
		ksft_test_result_fail("madvise(MADV_HUGEPAGE) failed\n");
		goto out;
	}

	/*
	 * Touch the first sub-page and test of we get another sub-page
	 * populated.
	 */
	mem[0] = 0;
	if (!page_is_populated(mem + pagesize)) {
		ksft_test_result_skip("Did not get a THP populated\n");
		goto out;
	}

	test_child_ro_gup(mem, thpsize);
out:
	munmap(mmap_mem, mmap_size);
}

static void test_anon_hugetlb_ro_gup_child(void)
{
	unsigned char *mem, *dummy;

	ksft_print_msg("[RUN] %s\n", __func__);

	if (!hugetlbsize)
		ksft_test_result_skip("hugetlb size not detected\n");

	ksft_print_msg("[INFO] Assuming hugetlb size of %zd bytes\n",
			hugetlbsize);

	mem = mmap(NULL, hugetlbsize, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
	if (mem == MAP_FAILED) {
		ksft_test_result_skip("need more free huge pages\n");
		return;
	}

	/*
	 * We need a total of two hugetlb pages to handle COW/unsharing
	 * properly.
	 */
	dummy = mmap(NULL, hugetlbsize, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
	if (dummy == MAP_FAILED) {
		ksft_test_result_skip("need more free huge pages\n");
		goto out;
	}
	munmap(dummy, hugetlbsize);

	test_child_ro_gup(mem, hugetlbsize);
out:
	munmap(mem, hugetlbsize);
}

int main(int argc, char **argv)
{
	int err;

	pagesize = getpagesize();
	thpsize = detect_thpsize();
	/* For simplicity, we'll rely on the thp size. */
	hugetlbsize = thpsize;

	ksft_print_header();
	ksft_set_plan(3);

	/* We need an easy way to talk to our child. */
	shared = mmap(NULL, pagesize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (shared == MAP_FAILED)
		ksft_exit_fail_msg("mmap(MAP_SHARED)\n");

	/*
	 * Tests for the security issue reported by Jann Horn that originally
	 * resulted in CVE-2020-29374. More generally, it's a violation of
	 * POSIX MAP_PRIVATE semantics, because some other process can modify
	 * pages that are supposed to be private to one process.
	 *
	 * So let's test that process-private pages stay private using the
	 * known vmsplice reproducer.
	 */
	test_anon_ro_gup_child();
	test_anon_thp_ro_gup_child();
	test_anon_hugetlb_ro_gup_child();

	err = ksft_get_fail_cnt();
	if (err)
		ksft_exit_fail_msg("%d out of %d tests failed\n",
				   err, ksft_test_num());
	return ksft_exit_pass();
}
