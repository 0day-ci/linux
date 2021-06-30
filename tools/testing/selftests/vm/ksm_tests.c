// SPDX-License-Identifier: GPL-2.0

#include <string.h>
#include <sys/mman.h>
#include <stdbool.h>
#include <time.h>

#include "../kselftest.h"

#define KSM_SYSFS_PATH "/sys/kernel/mm/ksm/"
#define KSM_SCAN_LIMIT_SEC_DEFAULT 120
#define KSM_PAGE_COUNT_DEFAULT 10l
#define KSM_FP(s) (KSM_SYSFS_PATH s)
#define KSM_PROT_STR_DEFAULT "rw"

struct ksm_sysfs {
	unsigned long max_page_sharing;
	unsigned long merge_across_nodes;
	unsigned long pages_to_scan;
	unsigned long run;
	unsigned long sleep_millisecs;
	unsigned long stable_node_chains_prune_millisecs;
	unsigned long use_zero_pages;
};

static int ksm_write_sysfs(const char *file_path, unsigned long val)
{
	FILE *f = fopen(file_path, "w");

	if (!f) {
		fprintf(stderr, "f %s\n", file_path);
		perror("fopen");
		return 1;
	}
	if (fprintf(f, "%lu", val) < 0) {
		perror("fprintf");
		return 1;
	}
	fclose(f);

	return 0;
}

static int ksm_read_sysfs(const char *file_path, unsigned long *val)
{
	FILE *f = fopen(file_path, "r");

	if (!f) {
		fprintf(stderr, "f %s\n", file_path);
		perror("fopen");
		return 1;
	}
	if (fscanf(f, "%lu", val) != 1) {
		perror("fscanf");
		return 1;
	}
	fclose(f);

	return 0;
}

static int str_to_prot(char *prot_str)
{
	int prot = 0;

	if ((strchr(prot_str, 'r')) != NULL)
		prot |= PROT_READ;
	if ((strchr(prot_str, 'w')) != NULL)
		prot |= PROT_WRITE;
	if ((strchr(prot_str, 'x')) != NULL)
		prot |= PROT_EXEC;

	return prot;
}

static void print_help(void)
{
	printf("usage: ksm_tests [-h] [-a prot] [-p page_count] [-l timeout]\n");
	printf(" -a: specify the access protections of pages.\n"
	       "     <prot> must be of the form [rwx].\n"
	       "     Default: %s\n", KSM_PROT_STR_DEFAULT);
	printf(" -p: specify the number of pages to test.\n"
	       "     Default: %ld\n", KSM_PAGE_COUNT_DEFAULT);
	printf(" -l: limit the maximum running time (in seconds) for a test.\n"
	       "     Default: %d seconds\n", KSM_SCAN_LIMIT_SEC_DEFAULT);

	exit(0);
}

static bool assert_ksm_pages_count(long dupl_page_count)
{
	unsigned long max_page_sharing, pages_sharing, pages_shared;

	if (ksm_read_sysfs(KSM_FP("pages_shared"), &pages_shared) ||
	    ksm_read_sysfs(KSM_FP("pages_sharing"), &pages_sharing) ||
	    ksm_read_sysfs(KSM_FP("max_page_sharing"), &max_page_sharing))
		return false;

	/*
	 * Since there must be at least 2 pages for merging and 1 page can be
	 * shared with the limited amount of pages (max_page_sharing), sometimes
	 * there are 'leftover' pages that cannot be merged. For example, if there
	 * are 11 pages with max_page_sharing = 10, then only 10 pages will be
	 * merged and the 11th page won't be affected. As a result, when the number
	 * of duplicate pages is divided by max_page_sharing and the remainder is 1,
	 * pages_shared and pages_sharing values will be equal between dupl_page_count
	 * and dupl_page_count - 1.
	 */
	if (dupl_page_count % max_page_sharing == 1 ||
	    dupl_page_count % max_page_sharing == 0) {
		if (pages_shared == dupl_page_count / max_page_sharing &&
		    pages_sharing == pages_shared * (max_page_sharing - 1))
			return true;
		else
			return false;
	}

	if (pages_shared == dupl_page_count / max_page_sharing + 1 &&
	    pages_sharing == dupl_page_count - pages_shared)
		return true;

	return false;
}

static int ksm_save_def(struct ksm_sysfs *ksm_sysfs)
{
	if (ksm_read_sysfs(KSM_FP("max_page_sharing"), &ksm_sysfs->max_page_sharing) ||
	    ksm_read_sysfs(KSM_FP("merge_across_nodes"), &ksm_sysfs->merge_across_nodes) ||
	    ksm_read_sysfs(KSM_FP("sleep_millisecs"), &ksm_sysfs->sleep_millisecs) ||
	    ksm_read_sysfs(KSM_FP("pages_to_scan"), &ksm_sysfs->pages_to_scan) ||
	    ksm_read_sysfs(KSM_FP("run"), &ksm_sysfs->run) ||
	    ksm_read_sysfs(KSM_FP("stable_node_chains_prune_millisecs"),
			   &ksm_sysfs->stable_node_chains_prune_millisecs) ||
	    ksm_read_sysfs(KSM_FP("use_zero_pages"), &ksm_sysfs->use_zero_pages))
		return 1;

	return 0;
}

static int ksm_restore(struct ksm_sysfs *ksm_sysfs)
{
	if (ksm_write_sysfs(KSM_FP("max_page_sharing"), ksm_sysfs->max_page_sharing) ||
	    ksm_write_sysfs(KSM_FP("merge_across_nodes"), ksm_sysfs->merge_across_nodes) ||
	    ksm_write_sysfs(KSM_FP("pages_to_scan"), ksm_sysfs->pages_to_scan) ||
	    ksm_write_sysfs(KSM_FP("run"), ksm_sysfs->run) ||
	    ksm_write_sysfs(KSM_FP("sleep_millisecs"), ksm_sysfs->sleep_millisecs) ||
	    ksm_write_sysfs(KSM_FP("stable_node_chains_prune_millisecs"),
			    ksm_sysfs->stable_node_chains_prune_millisecs) ||
	    ksm_write_sysfs(KSM_FP("use_zero_pages"), ksm_sysfs->use_zero_pages))
		return 1;

	return 0;
}

static int check_ksm_merge(int mapping, int prot, long page_count, int timeout)
{
	int ret = KSFT_FAIL;
	size_t page_size = sysconf(_SC_PAGESIZE);
	unsigned long cur_scan, init_scan;
	void *map_area;
	struct timespec start_time, cur_time;

	printf("Testing KSM MADV_MERGEABLE with %ld identical pages\n", page_count);

	if (ksm_write_sysfs(KSM_FP("sleep_millisecs"), 0) ||
	    ksm_write_sysfs(KSM_FP("pages_to_scan"), page_count))
		return ret;

	if (ksm_read_sysfs(KSM_FP("full_scans"), &init_scan))
		return ret;

	cur_scan = init_scan;

	map_area = mmap(NULL, page_size * page_count, PROT_WRITE, mapping, -1, 0);
	if (!map_area) {
		perror("mmap");
		return ret;
	}

	memset(map_area, '*', page_size * page_count);

	if (mprotect(map_area, page_size * page_count, prot)) {
		perror("mprotect");
		goto err_out;
	}

	if (madvise(map_area, page_size * page_count, MADV_MERGEABLE)) {
		perror("madvise");
		goto err_out;
	}

	if (ksm_write_sysfs(KSM_FP("run"), 1))
		goto err_out;

	if (clock_gettime(CLOCK_MONOTONIC_RAW, &start_time)) {
		perror("clock_gettime");
		goto err_out;
	}

	/* Since merging occurs only after 2 scans, make sure to get at least 2 full scans */
	while (cur_scan < init_scan + 2) {
		if (ksm_read_sysfs(KSM_FP("full_scans"), &cur_scan))
			goto err_out;

		if (clock_gettime(CLOCK_MONOTONIC_RAW, &cur_time)) {
			perror("clock_gettime");
			goto err_out;
		}

		if ((cur_time.tv_sec - start_time.tv_sec) > timeout) {
			printf("Scan time limit exceeded\n");
			goto err_out;
		}
	}

	/* verify that the right number of pages are merged */
	if (assert_ksm_pages_count(page_count)) {
		printf("OK\n");
		munmap(map_area, page_size * page_count);
		return KSFT_PASS;
	}

err_out:
	printf("Not OK\n");
	munmap(map_area, page_size * page_count);
	return KSFT_FAIL;
}

int main(int argc, char *argv[])
{
	int ret, opt;
	struct ksm_sysfs ksm_sysfs_old;
	long page_count = KSM_PAGE_COUNT_DEFAULT;
	int ksm_scan_limit_sec = KSM_SCAN_LIMIT_SEC_DEFAULT;
	int prot = 0;

	if (access(KSM_SYSFS_PATH, F_OK)) {
		printf("Config KSM not enabled\n");
		return KSFT_SKIP;
	}

	while ((opt = getopt(argc, argv, "ha:p:l:")) != -1) {
		switch (opt) {
		case 'a':
			prot = str_to_prot(optarg);
			break;
		case 'p':
			page_count = atol(optarg);
			if (page_count <= 0) {
				printf("The number of pages must be greater than 0\n");
				return KSFT_FAIL;
			}
			break;
		case 'l':
			ksm_scan_limit_sec = atoi(optarg);
			if (ksm_scan_limit_sec <= 0) {
				printf("Timeout value must be greater than 0\n");
				return KSFT_FAIL;
			}
			break;
		case 'h':
			print_help();
			break;
		default:
			return KSFT_FAIL;
		}
	}

	if (prot == 0)
		prot = str_to_prot(KSM_PROT_STR_DEFAULT);

	if (ksm_save_def(&ksm_sysfs_old)) {
		printf("Cannot save default tunables\n");
		return KSFT_FAIL;
	}

	/* unmerge all pages if there are any */
	if (ksm_write_sysfs(KSM_FP("run"), 2))
		return KSFT_FAIL;

	ret = check_ksm_merge(MAP_PRIVATE | MAP_ANONYMOUS, prot, page_count, ksm_scan_limit_sec);

	if (ksm_restore(&ksm_sysfs_old)) {
		printf("Cannot restore default tunables\n");
		return KSFT_FAIL;
	}

	return ret;
}
