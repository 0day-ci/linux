/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Vendor specific definitions for EHCI on Atmel/Microchip SoCs.
 *
 * © 2021 Alexander Dahl <ada@thorsis.com>
 */
#ifndef EHCI_ATMEL_H
#define EHCI_ATMEL_H

/* device specific register offsets, taken from SAMA5D2 datasheet */

#define AT91_UHPHS_INSNREG06    0xA8        /* AHB Error Status Register */

#define AT91_UHPHS_INSNREG07    0xAC        /* AHB Master Error Address Register */

#define AT91_UHPHS_INSNREG08    0xB0        /* HSIC Enable/Disable Register */
#define AT91_UHPHS_HSIC_EN      (1 << 2)    /* HSIC Enable/Disable */

#endif /* ECHI_ATMEL_H */
