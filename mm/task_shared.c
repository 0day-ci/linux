// SPDX-License-Identifier: GPL-2.0
#include <linux/mm.h>
#include <linux/uio.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/highmem.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/task_shared.h>

/* Shared page */

#define TASK_USHARED_SLOTS (PAGE_SIZE/sizeof(union task_shared))

/*
 * Called once to init struct ushared_pages pointer.
 */
static int init_mm_ushared(struct mm_struct *mm)
{
	struct ushared_pages *usharedpg;

	usharedpg = kmalloc(sizeof(struct ushared_pages), GFP_KERNEL);
	if (usharedpg == NULL)
		return 1;

	INIT_LIST_HEAD(&usharedpg->plist);
	INIT_LIST_HEAD(&usharedpg->frlist);
	usharedpg->pcount = 0;
	mmap_write_lock(mm);
	if (mm->usharedpg == NULL) {
		mm->usharedpg = usharedpg;
		usharedpg = NULL;
	}
	mmap_write_unlock(mm);
	if (usharedpg != NULL)
		kfree(usharedpg);
	return 0;
}

static int init_task_ushrd(struct task_struct *t)
{
	struct task_ushrd_struct *ushrd;

	ushrd = kzalloc(sizeof(struct task_ushrd_struct), GFP_KERNEL);
	if (ushrd == NULL)
		return 1;

	mmap_write_lock(t->mm);
	if (t->task_ushrd == NULL) {
		t->task_ushrd = ushrd;
		ushrd = NULL;
	}
	mmap_write_unlock(t->mm);
	if (ushrd != NULL)
		kfree(ushrd);
	return 0;
}

/*
 * Called from __mmput(), mm is going away
 */
void mm_ushared_clear(struct mm_struct *mm)
{
	struct ushared_pg *upg;
	struct ushared_pg *tmp;
	struct ushared_pages *usharedpg;

	if (mm == NULL || mm->usharedpg == NULL)
		return;

	usharedpg = mm->usharedpg;
	if (list_empty(&usharedpg->frlist))
		goto out;

	list_for_each_entry_safe(upg, tmp, &usharedpg->frlist, fr_list) {
		list_del(&upg->fr_list);
		put_page(upg->pages[0]);
		kfree(upg);
	}
out:
	kfree(mm->usharedpg);
	mm->usharedpg = NULL;

}

void task_ushared_free(struct task_struct *t)
{
	struct task_ushrd_struct *ushrd = t->task_ushrd;
	struct mm_struct *mm = t->mm;
	struct ushared_pages *usharedpg;
	int slot;

	if (mm == NULL || mm->usharedpg == NULL || ushrd == NULL)
		return;

	usharedpg = mm->usharedpg;
	mmap_write_lock(mm);

	if (ushrd->upg == NULL)
		goto out;

	slot = (unsigned long)((unsigned long)ushrd->uaddr
		 - ushrd->upg->vaddr) / sizeof(union task_shared);
	clear_bit(slot, (unsigned long *)(&ushrd->upg->bitmap));

	/* move to head */
	if (ushrd->upg->slot_count == 0) {
		list_del(&ushrd->upg->fr_list);
		list_add(&ushrd->upg->fr_list, &usharedpg->frlist);
	}

	ushrd->upg->slot_count++;

	ushrd->uaddr = ushrd->kaddr = NULL;
	ushrd->upg = NULL;

out:
	t->task_ushrd = NULL;
	mmap_write_unlock(mm);
	kfree(ushrd);
}

/* map shared page */
static int task_shared_add_vma(struct ushared_pg *pg)
{
	struct vm_area_struct *vma;
	struct mm_struct *mm =  current->mm;
	unsigned long ret = 1;


	if (!pg->vaddr) {
		/* Try to map as high as possible, this is only a hint. */
		pg->vaddr = get_unmapped_area(NULL, TASK_SIZE - PAGE_SIZE,
					PAGE_SIZE, 0, 0);
		if (pg->vaddr & ~PAGE_MASK) {
			ret = 0;
			goto fail;
		}
	}

	vma = _install_special_mapping(mm, pg->vaddr, PAGE_SIZE,
			VM_SHARED|VM_READ|VM_MAYREAD|VM_DONTCOPY,
			&pg->ushrd_mapping);
	if (IS_ERR(vma)) {
		ret = 0;
		pg->vaddr = 0;
		goto fail;
	}

	pg->kaddr = (unsigned long)page_address(pg->pages[0]);
fail:
	return ret;
}

/*
 * Allocate a page, map user address and add to freelist
 */
static struct ushared_pg *ushared_allocpg(void)
{

	struct ushared_pg *pg;
	struct mm_struct *mm = current->mm;
	struct ushared_pages *usharedpg = mm->usharedpg;

	if (usharedpg == NULL)
		return NULL;
	pg = kzalloc(sizeof(*pg), GFP_KERNEL);

	if (unlikely(!pg))
		return NULL;
	pg->ushrd_mapping.name = "[task_shared]";
	pg->ushrd_mapping.fault = NULL;
	pg->ushrd_mapping.pages = pg->pages;
	pg->pages[0] = alloc_page(GFP_KERNEL);
	if (!pg->pages[0])
		goto out;
	pg->pages[1] = NULL;
	pg->bitmap = 0;

	/*
	 * page size should be 4096 or 8192
	 */
	pg->slot_count = TASK_USHARED_SLOTS;

	mmap_write_lock(mm);
	if (task_shared_add_vma(pg)) {
		list_add(&pg->fr_list, &usharedpg->frlist);
		usharedpg->pcount++;
		mmap_write_unlock(mm);
		return pg;
	}
	mmap_write_unlock(mm);

out:
	__free_page(pg->pages[0]);
	kfree(pg);
	return NULL;
}


/*
 * Allocate task_ushared struct for calling thread.
 */
static int task_ushared_alloc(void)
{
	struct mm_struct *mm = current->mm;
	struct ushared_pg *ent = NULL;
	struct task_ushrd_struct *ushrd;
	struct ushared_pages *usharedpg;
	int tryalloc = 0;
	int slot = -1;
	int ret = -ENOMEM;

	if (mm->usharedpg == NULL && init_mm_ushared(mm))
		return ret;

	if (current->task_ushrd == NULL && init_task_ushrd(current))
		return ret;

	usharedpg = mm->usharedpg;
	ushrd = current->task_ushrd;
repeat:
	if (mmap_write_lock_killable(mm))
		return -EINTR;

	ent = list_empty(&usharedpg->frlist) ? NULL :
		list_entry(usharedpg->frlist.next,
		struct ushared_pg, fr_list);

	if (ent == NULL || ent->slot_count == 0) {
		if (tryalloc == 0) {
			mmap_write_unlock(mm);
			(void)ushared_allocpg();
			tryalloc = 1;
			goto repeat;
		} else {
			ent = NULL;
		}
	}

	if (ent) {
		slot = find_first_zero_bit((unsigned long *)(&ent->bitmap),
		  TASK_USHARED_SLOTS);
		BUG_ON(slot >=  TASK_USHARED_SLOTS);

		set_bit(slot, (unsigned long *)(&ent->bitmap));

		ushrd->uaddr = (struct task_ushared *)(ent->vaddr +
		  (slot * sizeof(union task_shared)));
		ushrd->kaddr = (struct task_ushared *)(ent->kaddr +
		  (slot * sizeof(union task_shared)));
		ushrd->upg = ent;
		ent->slot_count--;
		/* move it to tail */
		if (ent->slot_count == 0) {
			list_del(&ent->fr_list);
			list_add_tail(&ent->fr_list, &usharedpg->frlist);
		}

	       ret = 0;
	}

out:
	mmap_write_unlock(mm);
	return ret;
}


/*
 * Task Shared : allocate if needed, and return address of shared struct for
 * this thread/task.
 */
static long task_getshared(u64 opt, u64 flags, void __user *uaddr)
{
	struct task_ushrd_struct *ushrd = current->task_ushrd;

	/* Currently only TASK_SCHEDSTAT supported */
#ifdef CONFIG_SCHED_INFO
	if (opt != TASK_SCHEDSTAT)
		return (-EINVAL);
#else
	return (-EOPNOTSUPP);
#endif

	/* We have address, return. */
	if (ushrd != NULL && ushrd->upg != NULL) {
		if (copy_to_user(uaddr, &ushrd->uaddr,
			sizeof(struct task_ushared *)))
			return (-EFAULT);
		return 0;
	}

	task_ushared_alloc();
	ushrd = current->task_ushrd;
	if (ushrd != NULL && ushrd->upg != NULL) {
		if (opt == TASK_SCHEDSTAT) {
			/* init current values */
			task_update_exec_runtime(current);
			task_update_runq_stat(current, 1);
		}
		if (copy_to_user(uaddr, &ushrd->uaddr,
			sizeof(struct task_ushared *)))
			return (-EFAULT);
		return 0;
	}
	return (-ENOMEM);
}


SYSCALL_DEFINE3(task_getshared, u64, opt, u64, flags, void __user *, uaddr)
{
	return task_getshared(opt, flags, uaddr);
}
