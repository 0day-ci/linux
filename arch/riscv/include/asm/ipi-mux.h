/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 Ventana Micro Systems Inc.
 */

#ifndef _ASM_RISCV_IPI_MUX_H
#define _ASM_RISCV_IPI_MUX_H

struct cpumask;

#ifdef CONFIG_SMP

/* Handle muxed IPIs */
void riscv_ipi_mux_handle_irq(void);

/* Create irq_domain for muxed IPIs */
struct irq_domain *riscv_ipi_mux_create(bool use_soft_irq,
			bool use_for_rfence,
			void (*clear_ipi)(void),
			void (*send_ipi)(const struct cpumask *mask));

/* Destroy irq_domain for muxed IPIs */
void riscv_ipi_mux_destroy(struct irq_domain *d);

#else

static inline void riscv_ipi_mux_handle_irq(void)
{
}

static inline struct irq_domain *riscv_ipi_mux_create(bool use_soft_irq,
			bool use_for_rfence,
			void (*clear_ipi)(void),
			void (*send_ipi)(const struct cpumask *mask))
{
	return NULL;
}

static inline void riscv_ipi_mux_destroy(struct irq_domain *d)
{
}

#endif

#endif /* _ASM_RISCV_IPI_MUX_H */
