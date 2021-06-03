// SPDX-License-Identifier: GPL-2.0
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <unistd.h>
#include <syscall.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <err.h>
#include <string.h>
#include <stdbool.h>
#include <malloc.h>

#define PAGEMAP_PATH "/proc/self/pagemap"
#define CLEAR_REFS_PATH "/proc/self/clear_refs"
#define SMAP_PATH "/proc/self/smaps"
#define MAX_LINE_LENGTH 512

#define TEST_ITERATIONS 10000

#define PMD_SIZE_PATH "/sys/kernel/mm/transparent_hugepage/hpage_pmd_size"

int clear_refs;
int pagemap;

int pagesize;
int mmap_size;	/* Size of test region */

static void clear_all_refs(void)
{
	if (write(clear_refs, "4\n", 2) != 2)
		printf("%s: failed to clear references\n", __func__);
}

static void touch_page(char *map, int n)
{
	map[(pagesize * n) + 1]++;
}

static int check_page(char *map, uint64_t n, int clear)
{
	uint64_t off;
	uint64_t buf = 0;

	off = (n + ((uint64_t)map >> 12)) << 3;

	if (lseek(pagemap, off, SEEK_SET) == (off_t) -1)
		errx(EXIT_FAILURE, "pagemap llseek failed");

	if (read(pagemap, &buf, 8) != 8)
		errx(EXIT_FAILURE, "pagemap read failed");

	if (clear)
		clear_all_refs();

	return ((buf >> 55) & 1);
}

static void test_simple(void)
{
	int i;
	char *map;

	printf("- Test %s:\n", __func__);

	map = aligned_alloc(pagesize, mmap_size);
	if (!map)
		errx(EXIT_FAILURE, "mmap");

	clear_all_refs();

	for (i = 0 ; i < TEST_ITERATIONS; i++) {
		if (check_page(map, 2, 1) == 1) {
			errx(EXIT_FAILURE, "dirty bit was 1, but should be 0 (i=%d)", i);
			break;
		}

		touch_page(map, 2);

		if (check_page(map, 2, 1) == 0) {
			errx(EXIT_FAILURE, "dirty bit was 0, but should be 1 (i=%d)", i);
			break;
		}

	}
	free(map);

	printf("success\n");
}

static void test_vma_reuse(void)
{
	char *map, *map2;

	printf("- Test %s:\n", __func__);

	map = (char *) 0x900000000000;
	map = mmap(map, mmap_size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
	if (map == MAP_FAILED)
		errx(EXIT_FAILURE, "mmap");

	clear_all_refs();
	touch_page(map, 2);

	munmap(map, mmap_size);
	map2 = mmap(map, mmap_size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
	if (map2 == MAP_FAILED)
		errx(EXIT_FAILURE, "mmap2");

	if (map != map2)
		errx(EXIT_FAILURE, "map != map2");

	if (check_page(map, 2, 1) == 0)
		errx(-1, "map/unmap lost dirty");

	munmap(map2, mmap_size);

	printf("success\n");
}

/*
 * read_pmd_pagesize(), check_for_pattern() and check_huge() adapted
 * from 'tools/testing/selftest/vm/split_huge_page_test.c'
 */
static uint64_t read_pmd_pagesize(void)
{
	int fd;
	char buf[20];
	ssize_t num_read;

	fd = open(PMD_SIZE_PATH, O_RDONLY);
	if (fd == -1)
		errx(EXIT_FAILURE, "Open hpage_pmd_size failed");

	num_read = read(fd, buf, 19);
	if (num_read < 1) {
		close(fd);
		errx(EXIT_FAILURE, "Read hpage_pmd_size failed");
	}
	buf[num_read] = '\0';
	close(fd);

	return strtoul(buf, NULL, 10);
}

static bool check_for_pattern(FILE *fp, const char *pattern, char *buf)
{
	while (fgets(buf, MAX_LINE_LENGTH, fp) != NULL) {
		if (!strncmp(buf, pattern, strlen(pattern)))
			return true;
	}
	return false;
}

static uint64_t check_huge(void *addr)
{
	uint64_t thp = 0;
	int ret;
	FILE *fp;
	char buffer[MAX_LINE_LENGTH];
	char addr_pattern[MAX_LINE_LENGTH];

	ret = snprintf(addr_pattern, MAX_LINE_LENGTH, "%08lx-",
		       (unsigned long) addr);
	if (ret >= MAX_LINE_LENGTH)
		errx(EXIT_FAILURE, "%s: Pattern is too long\n", __func__);

	fp = fopen(SMAP_PATH, "r");
	if (!fp)
		errx(EXIT_FAILURE, "%s: Failed to open file %s\n", __func__, SMAP_PATH);

	if (!check_for_pattern(fp, addr_pattern, buffer))
		goto err_out;

	/*
	 * Fetch the AnonHugePages: in the same block and check the number of
	 * hugepages.
	 */
	if (!check_for_pattern(fp, "AnonHugePages:", buffer))
		goto err_out;

	if (sscanf(buffer, "AnonHugePages:%10ld kB", &thp) != 1)
		errx(EXIT_FAILURE, "Reading smap error\n");

err_out:
	fclose(fp);

	return thp;
}

static void test_hugepage(void)
{
	char *map;
	int i, ret;
	size_t hpage_len = read_pmd_pagesize();

	printf("- Test %s:\n", __func__);

	map = memalign(hpage_len, hpage_len);
	if (!map)
		errx(EXIT_FAILURE, "memalign");

	ret = madvise(map, hpage_len, MADV_HUGEPAGE);
	if (ret)
		errx(EXIT_FAILURE, "madvise %d", ret);

	for (i = 0; i < hpage_len; i++)
		map[i] = (char)i;

	if (!check_huge(map))
		errx(EXIT_FAILURE, "failed to allocate THP");

	clear_all_refs();
	for (i = 0 ; i < TEST_ITERATIONS ; i++) {
		if (check_page(map, 2, 1) == 1) {
			errx(EXIT_FAILURE, "dirty bit was 1, but should be 0 (i=%d)", i);
			break;
		}

		touch_page(map, 2);

		if (check_page(map, 2, 1) == 0) {
			errx(EXIT_FAILURE, "dirty bit was 0, but should be 1 (i=%d)", i);
			break;
		}
	}
	munmap(map, mmap_size);

	printf("success\n");
}

int main(int argc, char **argv)
{
	pagemap = open(PAGEMAP_PATH, O_RDONLY, 0);
	if (pagemap < 0)
		errx(EXIT_FAILURE, "Failed to open %s", PAGEMAP_PATH);

	clear_refs = open(CLEAR_REFS_PATH, O_WRONLY, 0);
	if (clear_refs < 0)
		errx(EXIT_FAILURE, "Failed to open %s", CLEAR_REFS_PATH);

	pagesize = getpagesize();
	mmap_size = 10 * pagesize;

	test_simple();
	test_vma_reuse();
	test_hugepage();

	return 0;
}
