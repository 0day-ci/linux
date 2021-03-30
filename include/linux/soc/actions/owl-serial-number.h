/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
 */

#ifndef __SOC_ACTIONS_OWL_SERIAL_NUMBER_H__
#define __SOC_ACTIONS_OWL_SERIAL_NUMBER_H__

#if IS_ENABLED(CONFIG_OWL_SOCINFO)
u32 owl_get_soc_serial_low(void);
u32 owl_get_soc_serial_high(void);
#else
static inline u32 owl_get_soc_serial_low(void)
{ return 0; }

static inline u32 owl_get_soc_serial_high(void)
{ return 0; }
#endif /* CONFIG_OWL_SOCINFO */

#endif /* __SOC_ACTIONS_OWL_SERIAL_NUMBER_H__ */
