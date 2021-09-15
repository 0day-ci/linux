// SPDX-License-Identifier: GPL-2.0
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/nmi.h>
#include <linux/cpufreq.h>
#include <asm/perf_event.h>

/*
 * Safe maximum CPU frequency in case a particular platform doesn't implement
 * cpufreq driver. Although, architecture doesn't put any restrictions on
 * maximum frequency but 5 GHz seems to be safe maximum given the available
 * Arm CPUs in the market which are clocked much less than 5 GHz. On the other
 * hand, we can't make it much higher as it would lead to a large hard-lockup
 * detection timeout on parts which are running slower (eg. 1GHz on
 * Developerbox) and doesn't possess a cpufreq driver.
 */
#define SAFE_MAX_CPU_FREQ	5000000000UL // 5 GHz
u64 hw_nmi_get_sample_period(int watchdog_thresh)
{
	unsigned int cpu = smp_processor_id();
	unsigned long max_cpu_freq;

	max_cpu_freq = cpufreq_get_hw_max_freq(cpu) * 1000UL;
	if (!max_cpu_freq)
		max_cpu_freq = SAFE_MAX_CPU_FREQ;

	return (u64)max_cpu_freq * watchdog_thresh;
}

static watchdog_nmi_status_reporter status_reporter;

static int hld_enabled_thread_fun(void *unused)
{
	struct watchdog_nmi_status status;
	watchdog_nmi_status_reporter local_reporter;
	int ret;

	wait_event(arm_pmu_wait, arm_pmu_initialized);
	status.cpu = raw_smp_processor_id();

	if (!check_pmu_nmi_ability()) {
		status.status = -ENODEV;
		goto report;
	}

	ret = hardlockup_detector_perf_enable();
	status.status = ret;

report:
	local_reporter = (watchdog_nmi_status_reporter)atomic64_fetch_and(0,
				(atomic64_t *)&status_reporter);
	if (local_reporter)
		(*local_reporter)(&status);

	return 0;
}

/* As for watchdog_nmi_disable(), using the default implement */
void watchdog_nmi_enable(unsigned int cpu)
{
	struct task_struct *t;

	/* PMU is not ready */
	if (!arm_pmu_initialized) {
		t = kthread_create_on_cpu(hld_enabled_thread_fun, NULL, cpu,
			       "arm64_hld.%u");
		if (IS_ERR(t))
			return;
		wake_up_process(t);
		return;
	}

	/* For hotplug in cpu */
	hardlockup_detector_perf_enable();
}

int __init watchdog_nmi_probe(watchdog_nmi_status_reporter notifier)
{
	/* On arm64, arm pmu is not ready at this stage */
	status_reporter = notifier;
	return -EBUSY;
}

