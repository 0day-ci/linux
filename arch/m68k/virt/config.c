// SPDX-License-Identifier: GPL-2.0

#include <linux/serial_core.h>
#include <clocksource/timer-goldfish.h>

#include <asm/bootinfo.h>
#include <asm/bootinfo-virt.h>
#include <asm/byteorder.h>
#include <asm/machdep.h>
#include <asm/virt.h>
#include <asm/config.h>

struct virt_booter_data virt_bi_data;

struct virt_ctrl {
	u32 features;
	u32 cmd;
};

enum {
	CMD_NOOP,
	CMD_RESET,
	CMD_HALT,
	CMD_PANIC,
};

#define virt_ctrl ((volatile struct virt_ctrl *)virt_bi_data.ctrl.mmio)

static void virt_get_model(char *str)
{
	/* str is 80 characters long */
	sprintf(str, "QEMU Virtual M68K Machine (%u.%u.%u)",
		(u8)(virt_bi_data.qemu_version >> 24),
		(u8)(virt_bi_data.qemu_version >> 16),
		(u8)(virt_bi_data.qemu_version >> 8));
}

static void virt_halt(void)
{
	virt_ctrl->cmd = CMD_HALT;
	local_irq_disable();
	while (1)
		;
}

static void virt_reset(void)
{
	virt_ctrl->cmd = CMD_RESET;
	local_irq_disable();
	while (1)
		;
}

/*
 * Parse a virtual-m68k-specific record in the bootinfo
 */

int __init virt_parse_bootinfo(const struct bi_record *record)
{
	int unknown = 0;
	const void *data = record->data;

	switch (be16_to_cpu(record->tag)) {
	case BI_VIRT_QEMU_VERSION:
		virt_bi_data.qemu_version = be32_to_cpup(data);
		break;
	case BI_VIRT_GF_PIC_BASE:
		virt_bi_data.pic.mmio = be32_to_cpup(data);
		data += 4;
		virt_bi_data.pic.irq = be32_to_cpup(data);
		break;
	case BI_VIRT_GF_RTC_BASE:
		virt_bi_data.rtc.mmio = be32_to_cpup(data);
		data += 4;
		virt_bi_data.rtc.irq = be32_to_cpup(data);
		break;
	case BI_VIRT_GF_TTY_BASE:
		virt_bi_data.tty.mmio = be32_to_cpup(data);
		data += 4;
		virt_bi_data.tty.irq = be32_to_cpup(data);
		break;
	case BI_VIRT_CTRL_BASE:
		virt_bi_data.ctrl.mmio = be32_to_cpup(data);
		data += 4;
		virt_bi_data.ctrl.irq = be32_to_cpup(data);
		break;
	case BI_VIRT_VIRTIO_BASE:
		virt_bi_data.virtio.mmio = be32_to_cpup(data);
		data += 4;
		virt_bi_data.virtio.irq = be32_to_cpup(data);
		break;
	default:
		unknown = 1;
		break;
	}
	return unknown;
}

static void __init virt_sched_init(void)
{
	goldfish_timer_init(virt_bi_data.rtc.irq,
			    (void *)virt_bi_data.rtc.mmio);
}

void __init config_virt(void)
{
	char earlycon[24];

	snprintf(earlycon, sizeof(earlycon), "early_gf_tty,0x%08lx",
		 virt_bi_data.tty.mmio);
	setup_earlycon(earlycon);

	mach_init_IRQ = virt_init_IRQ;
	mach_sched_init = virt_sched_init;
	mach_get_model = virt_get_model;
	mach_reset = virt_reset;
	mach_halt = virt_halt;
	mach_power_off = virt_halt;
}
