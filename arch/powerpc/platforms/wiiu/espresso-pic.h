/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Nintendo Wii U "Espresso" interrupt controller support
 * Copyright (C) 2022 The linux-wiiu Team
 */

#ifndef __ESPRESSO_PIC_H
#define __ESPRESSO_PIC_H

/*
 * Instead of using COS custom IRQ remapping, the normal IRQ mapping is used:
 *
 * IRQ  Description
 * -------------------------------------------
 * 0    Error
 * 1    Unused
 * 2    Unused
 * 3    Audio Interface (TV)
 * 4    Unused
 * 5    DSP Accelerator
 * 6    DSP
 * 7    DSP DMA
 * 8    Unused
 * 9    Unused
 * 10   GPIPPC (?)
 * 11   Unused
 * 12   Audio Interface (Gamepad)
 * 13   I2C
 * 14   Unused
 * 15   Unused
 * 16   Unused
 * 17   Unused
 * 18   Unused
 * 19   Unused
 * 20   Unused
 * 21   Unused
 * 22   Unused
 * 23   GX2
 * 24   Latte IRQ Controller
 * 25   Unused
 * 26   IPC (CPU2)
 * 27   Unused
 * 28   IPC (CPU1)
 * 29   Unused
 * 30   IPC (CPU0)
 * 31   Unused
 */

struct espresso_pic {
	__be32 icr; /* Triggered IRQs */
	__be32 imr; /* Allowed IRQs */
} __packed;

#define ESPRESSO_NR_IRQS 32

unsigned int espresso_pic_get_irq(void);
void espresso_pic_init(void);

#endif
