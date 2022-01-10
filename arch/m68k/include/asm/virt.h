/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_VIRT_H
#define __ASM_VIRT_H

#define NUM_VIRT_SOURCES 200

struct virt_booter_device_data {
	unsigned long mmio;
	unsigned long irq;
};

struct virt_booter_data {
	unsigned long qemu_version;
	struct virt_booter_device_data pic;
	struct virt_booter_device_data rtc;
	struct virt_booter_device_data tty;
	struct virt_booter_device_data ctrl;
	struct virt_booter_device_data virtio;
};

extern struct virt_booter_data virt_bi_data;

extern void __init virt_init_IRQ(void);
extern void __init virt_sched_init(void);

#endif
