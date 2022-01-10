// SPDX-License-Identifier: GPL-2.0

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/types.h>

#include <asm/hwtest.h>
#include <asm/irq.h>
#include <asm/irq_regs.h>
#include <asm/virt.h>

struct goldfish_pic {
	u32 status;
	u32 irq_pending;
	u32 irq_diable_all;
	u32 disable;
	u32 enable;
	u32 pad[1019];
};

extern void show_registers(struct pt_regs *);

#define gf_pic ((volatile struct goldfish_pic *)virt_bi_data.pic.mmio)

#define GF_PIC(irq) (gf_pic[(irq - IRQ_USER) / 32])
#define GF_IRQ(irq) ((irq - IRQ_USER) % 32)

static void virt_irq_enable(struct irq_data *data)
{
	GF_PIC(data->irq).enable = 1 << GF_IRQ(data->irq);
}

static void virt_irq_disable(struct irq_data *data)
{
	GF_PIC(data->irq).disable = 1 << GF_IRQ(data->irq);
}

static unsigned int virt_irq_startup(struct irq_data *data)
{
	virt_irq_enable(data);
	return 0;
}

static irqreturn_t virt_nmi_handler(int irq, void *dev_id)
{
	static volatile int in_nmi;

	if (in_nmi)
		return IRQ_HANDLED;
	in_nmi = 1;

	pr_warn("Non-Maskable Interrupt\n");
	show_registers(get_irq_regs());

	in_nmi = 0;
	return IRQ_HANDLED;
}

static struct irq_chip virt_irq_chip = {
	.name		= "virt",
	.irq_enable	= virt_irq_enable,
	.irq_disable	= virt_irq_disable,
	.irq_startup	= virt_irq_startup,
	.irq_shutdown	= virt_irq_disable,
};

static void goldfish_pic_irq(struct irq_desc *desc)
{
	u32 irq_pending;
	int irq_num;

	irq_pending = gf_pic[desc->irq_data.irq - 1].irq_pending;
	irq_num = IRQ_USER + (desc->irq_data.irq - 1) * 32;

	do {
		if (irq_pending & 1)
			generic_handle_irq(irq_num);
		++irq_num;
		irq_pending >>= 1;
	} while (irq_pending);
}

/*
 * 6 goldfish-pic for CPU IRQ #1 to IRQ #6
 * CPU IRQ #1 -> PIC #1
 *               IRQ #1 to IRQ #31 -> unused
 *               IRQ #32 -> goldfish-tty
 * CPU IRQ #2 -> PIC #2
 *               IRQ #1 to IRQ #32 -> virtio-mmio from 1 to 32
 * CPU IRQ #3 -> PIC #3
 *               IRQ #1 to IRQ #32 -> virtio-mmio from 33 to 64
 * CPU IRQ #4 -> PIC #4
 *               IRQ #1 to IRQ #32 -> virtio-mmio from 65 to 96
 * CPU IRQ #5 -> PIC #5
 *               IRQ #1 to IRQ #32 -> virtio-mmio from 97 to 128
 * CPU IRQ #6 -> PIC #6
 *               IRQ #1 -> goldfish-rtc
 *               IRQ #2 to IRQ #32 -> unused
 * CPU IRQ #7 -> NMI
 */
void __init virt_init_IRQ(void)
{
	int i;

	m68k_setup_irq_controller(&virt_irq_chip, handle_simple_irq, IRQ_USER,
				  NUM_VIRT_SOURCES - IRQ_USER);

	for (i = 0; i < 6; i++) {
		irq_set_chained_handler(virt_bi_data.pic.irq + i,
					goldfish_pic_irq);
	}

	if (request_irq(IRQ_AUTO_7, virt_nmi_handler, 0, "NMI",
			virt_nmi_handler))
		pr_err("Couldn't register NMI\n");
}
