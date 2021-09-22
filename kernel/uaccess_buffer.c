// SPDX-License-Identifier: GPL-2.0
#include <linux/compat.h>
#include <linux/prctl.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/uaccess.h>
#include <linux/uaccess_buffer.h>
#include <linux/uaccess_buffer_info.h>

#ifdef CONFIG_UACCESS_BUFFER

/*
 * We use a separate implementation of copy_to_user() that avoids the call
 * to instrument_copy_to_user() as this would otherwise lead to infinite
 * recursion.
 */
static unsigned long
uaccess_buffer_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	if (!access_ok(to, n))
		return n;
	return raw_copy_to_user(to, from, n);
}

static void uaccess_buffer_log(unsigned long addr, unsigned long size,
			      unsigned long flags)
{
	struct uaccess_buffer_entry entry;

	if (current->uaccess_buffer.size < sizeof(entry) ||
	    unlikely(uaccess_kernel()))
		return;
	entry.addr = addr;
	entry.size = size;
	entry.flags = flags;

	/*
	 * If our uaccess fails, abort the log so that the end address writeback
	 * does not occur and userspace sees zero accesses.
	 */
	if (uaccess_buffer_copy_to_user(
		    (void __user *)current->uaccess_buffer.addr, &entry,
		    sizeof(entry))) {
		current->uaccess_buffer.state = 0;
		current->uaccess_buffer.addr = current->uaccess_buffer.size = 0;
	}

	current->uaccess_buffer.addr += sizeof(entry);
	current->uaccess_buffer.size -= sizeof(entry);
}

void uaccess_buffer_log_read(const void __user *from, unsigned long n)
{
	uaccess_buffer_log((unsigned long)from, n, 0);
}
EXPORT_SYMBOL(uaccess_buffer_log_read);

void uaccess_buffer_log_write(void __user *to, unsigned long n)
{
	uaccess_buffer_log((unsigned long)to, n, UACCESS_BUFFER_FLAG_WRITE);
}
EXPORT_SYMBOL(uaccess_buffer_log_write);

int uaccess_buffer_set_logging(unsigned long addr, unsigned long size,
			       unsigned long store_end_addr)
{
	sigset_t temp_sigmask;

	current->uaccess_buffer.addr = addr;
	current->uaccess_buffer.size = size;
	current->uaccess_buffer.store_end_addr = store_end_addr;

	/*
	 * Allow 2 syscalls before resetting the state: the current one (i.e.
	 * prctl) and the next one, whose accesses we want to log.
	 */
	current->uaccess_buffer.state = 2;

	/*
	 * Temporarily mask signals so that an intervening asynchronous signal
	 * will not interfere with the logging.
	 */
	current->uaccess_buffer.saved_sigmask = current->blocked;
	sigfillset(&temp_sigmask);
	sigdelsetmask(&temp_sigmask, sigmask(SIGKILL) | sigmask(SIGSTOP));
	__set_current_blocked(&temp_sigmask);

	return 0;
}

void uaccess_buffer_syscall_entry(void)
{
	/*
	 * The current syscall may be e.g. rt_sigprocmask, and therefore we want
	 * to reset the mask before the syscall and not after, so that our
	 * temporary mask is unobservable.
	 */
	if (current->uaccess_buffer.state == 1)
		__set_current_blocked(&current->uaccess_buffer.saved_sigmask);
}

void uaccess_buffer_syscall_exit(void)
{
	if (current->uaccess_buffer.state > 0) {
		--current->uaccess_buffer.state;
		if (current->uaccess_buffer.state == 0) {
			u64 addr64 = current->uaccess_buffer.addr;

			uaccess_buffer_copy_to_user(
				(void __user *)
					current->uaccess_buffer.store_end_addr,
				&addr64, sizeof(addr64));
			current->uaccess_buffer.addr = current->uaccess_buffer.size = 0;
		}
	}
}

#endif
