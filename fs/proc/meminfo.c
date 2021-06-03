// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/mman.h>
#include <linux/mmzone.h>
#include <linux/proc_fs.h>
#include <linux/percpu.h>
#include <linux/seq_file.h>
#include <linux/swap.h>
#include <linux/vmstat.h>
#include <linux/atomic.h>
#include <linux/vmalloc.h>
#ifdef CONFIG_CMA
#include <linux/cma.h>
#endif
#ifdef CONFIG_MEMCG
#include <linux/memcontrol.h>
#endif
#include <asm/page.h>
#include "internal.h"

void __attribute__((weak)) arch_report_meminfo(struct seq_file *m)
{
}

static void proc_fill_meminfo(struct meminfo *mi)
{
	int lru;
	long cached;

	si_meminfo(&mi->si);
	si_swapinfo(&mi->si);

	for (lru = LRU_BASE; lru < NR_LRU_LISTS; lru++)
		mi->pages[lru] = global_node_page_state(NR_LRU_BASE + lru);

	cached = global_node_page_state(NR_FILE_PAGES) - total_swapcache_pages() - mi->si.bufferram;
	if (cached < 0)
		cached = 0;

	mi->cached = cached;
	mi->swapcached = total_swapcache_pages();
	mi->slab_reclaimable = global_node_page_state_pages(NR_SLAB_RECLAIMABLE_B);
	mi->slab_unreclaimable = global_node_page_state_pages(NR_SLAB_UNRECLAIMABLE_B);
	mi->anon_pages = global_node_page_state(NR_ANON_MAPPED);
	mi->mapped = global_node_page_state(NR_FILE_MAPPED);
	mi->nr_pagetable = global_node_page_state(NR_PAGETABLE);
	mi->dirty_pages = global_node_page_state(NR_FILE_DIRTY);
	mi->writeback_pages = global_node_page_state(NR_WRITEBACK);
}

#ifdef CONFIG_MEMCG
static inline void fill_meminfo(struct meminfo *mi, struct task_struct *task)
{
	mem_fill_meminfo(mi, task);
}
#else
static inline void fill_meminfo(struct meminfo *mi, struct task_struct *task)
{
	proc_fill_meminfo(mi);
}
#endif

static void show_val_kb(struct seq_file *m, const char *s, unsigned long num)
{
	seq_put_decimal_ull_width(m, s, num << (PAGE_SHIFT - 10), 8);
	seq_write(m, " kB\n", 4);
}

static int meminfo_proc_show_mi(struct seq_file *m, struct meminfo *mi)
{
	show_val_kb(m, "MemTotal:       ", mi->si.totalram);
	show_val_kb(m, "MemFree:        ", mi->si.freeram);
	show_val_kb(m, "MemAvailable:   ", si_mem_available_mi(mi));
	show_val_kb(m, "Buffers:        ", mi->si.bufferram);
	show_val_kb(m, "Cached:         ", mi->cached);
	show_val_kb(m, "SwapCached:     ", mi->swapcached);
	show_val_kb(m, "Active:         ", mi->pages[LRU_ACTIVE_ANON] + mi->pages[LRU_ACTIVE_FILE]);
	show_val_kb(m, "Inactive:       ", mi->pages[LRU_INACTIVE_ANON] + mi->pages[LRU_INACTIVE_FILE]);
	show_val_kb(m, "Active(anon):   ", mi->pages[LRU_ACTIVE_ANON]);
	show_val_kb(m, "Inactive(anon): ", mi->pages[LRU_INACTIVE_ANON]);
	show_val_kb(m, "Active(file):   ", mi->pages[LRU_ACTIVE_FILE]);
	show_val_kb(m, "Inactive(file): ", mi->pages[LRU_INACTIVE_FILE]);
	show_val_kb(m, "Unevictable:    ", mi->pages[LRU_UNEVICTABLE]);

#ifdef CONFIG_HIGHMEM
	show_val_kb(m, "HighTotal:      ", mi->si.totalhigh);
	show_val_kb(m, "HighFree:       ", mi->si.freehigh);
	show_val_kb(m, "LowTotal:       ", mi->si.totalram - mi->si.totalhigh);
	show_val_kb(m, "LowFree:        ", mi->si.freeram - mi->si.freehigh);
#endif

	show_val_kb(m, "SwapTotal:      ", mi->si.totalswap);
	show_val_kb(m, "SwapFree:       ", mi->si.freeswap);
	show_val_kb(m, "Dirty:          ", mi->dirty_pages);
	show_val_kb(m, "Writeback:      ", mi->writeback_pages);

	show_val_kb(m, "AnonPages:      ", mi->anon_pages);
	show_val_kb(m, "Mapped:         ", mi->mapped);
	show_val_kb(m, "Shmem:          ", mi->si.sharedram);
	show_val_kb(m, "Slab:           ", mi->slab_reclaimable + mi->slab_unreclaimable);
	show_val_kb(m, "SReclaimable:   ", mi->slab_reclaimable);
	show_val_kb(m, "SUnreclaim:     ", mi->slab_unreclaimable);
	show_val_kb(m, "PageTables:     ", mi->nr_pagetable);

	return 0;
}

static int meminfo_proc_show(struct seq_file *m, void *v)
{

	struct meminfo mi;

	proc_fill_meminfo(&mi);
	meminfo_proc_show_mi(m, &mi);

	show_val_kb(m, "Mlocked:        ", global_zone_page_state(NR_MLOCK));

#ifndef CONFIG_MMU
	show_val_kb(m, "MmapCopy:       ",
		    (unsigned long)atomic_long_read(&mmap_pages_allocated));
#endif

	show_val_kb(m, "KReclaimable:   ", mi.slab_reclaimable +
		    global_node_page_state(NR_KERNEL_MISC_RECLAIMABLE));
	seq_printf(m, "KernelStack:    %8lu kB\n",
		   global_node_page_state(NR_KERNEL_STACK_KB));
#ifdef CONFIG_SHADOW_CALL_STACK
	seq_printf(m, "ShadowCallStack:%8lu kB\n",
		   global_node_page_state(NR_KERNEL_SCS_KB));
#endif

	show_val_kb(m, "NFS_Unstable:   ", 0);
	show_val_kb(m, "Bounce:         ",
		    global_zone_page_state(NR_BOUNCE));
	show_val_kb(m, "WritebackTmp:   ",
		    global_node_page_state(NR_WRITEBACK_TEMP));
	show_val_kb(m, "CommitLimit:    ", vm_commit_limit());
	show_val_kb(m, "Committed_AS:   ", vm_memory_committed());
	seq_printf(m, "VmallocTotal:   %8lu kB\n",
		   (unsigned long)VMALLOC_TOTAL >> 10);
	show_val_kb(m, "VmallocUsed:    ", vmalloc_nr_pages());
	show_val_kb(m, "VmallocChunk:   ", 0ul);
	show_val_kb(m, "Percpu:         ", pcpu_nr_pages());

#ifdef CONFIG_MEMORY_FAILURE
	seq_printf(m, "HardwareCorrupted: %5lu kB\n",
		   atomic_long_read(&num_poisoned_pages) << (PAGE_SHIFT - 10));
#endif

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	show_val_kb(m, "AnonHugePages:  ",
		    global_node_page_state(NR_ANON_THPS));
	show_val_kb(m, "ShmemHugePages: ",
		    global_node_page_state(NR_SHMEM_THPS));
	show_val_kb(m, "ShmemPmdMapped: ",
		    global_node_page_state(NR_SHMEM_PMDMAPPED));
	show_val_kb(m, "FileHugePages:  ",
		    global_node_page_state(NR_FILE_THPS));
	show_val_kb(m, "FilePmdMapped:  ",
		    global_node_page_state(NR_FILE_PMDMAPPED));
#endif

#ifdef CONFIG_CMA
	show_val_kb(m, "CmaTotal:       ", totalcma_pages);
	show_val_kb(m, "CmaFree:        ",
		    global_zone_page_state(NR_FREE_CMA_PAGES));
#endif

	hugetlb_report_meminfo(m);

	arch_report_meminfo(m);

	return 0;
}

int proc_meminfo_show(struct seq_file *m, struct pid_namespace *ns,
		     struct pid *pid, struct task_struct *task)
{
	struct meminfo mi;

	fill_meminfo(&mi, task);

	meminfo_proc_show_mi(m, &mi);
	hugetlb_report_meminfo(m);
	arch_report_meminfo(m);

	return 0;
}

static int __init proc_meminfo_init(void)
{
	proc_create_single("meminfo", 0, NULL, meminfo_proc_show);
	return 0;
}
fs_initcall(proc_meminfo_init);
