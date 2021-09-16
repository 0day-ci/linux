/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Driver for the IDT ClockMatrix(TM) and 82p33xxx families of
 * timing and synchronization devices.
 *
 * Copyright (C) 2019 Integrated Device Technology, Inc., a Renesas Company.
 */

#ifndef __UAPI_LINUX_RSMU_CDEV_H
#define __UAPI_LINUX_RSMU_CDEV_H

#include <linux/types.h>
#include <linux/ioctl.h>

/* Set dpll combomode */
struct rsmu_combomode {
	__u8 dpll;
	__u8 mode;
};

/* Get dpll state */
struct rsmu_get_state {
	__u8 dpll;
	__u8 state;
};

/* Get dpll ffo (fractional frequency offset) in ppqt */
struct rsmu_get_ffo {
	__u8 dpll;
	__s64 ffo;
};

/*
 * RSMU IOCTL List
 */
#define RSMU_MAGIC '?'

/**
 * struct rsmu_combomode
 * @dpll: dpll index (Digital Phase Lock Loop)
 * @mode: combomode setting, see enum rsmu_dpll_combomode
 *
 * ioctl to set SMU combo mode.Combo mode provides physical layer frequency
 * support from the Ethernet Equipment Clock to the PTP clock
 */
#define RSMU_SET_COMBOMODE  _IOW(RSMU_MAGIC, 1, struct rsmu_combomode)

/**
 * struct rsmu_get_state
 * @dpll: dpll index (Digital Phase Lock Loop)
 * @state: dpll state, see enum rsmu_class_state
 *
 * ioctl to get SMU dpll state. Application can call this API to tell if
 * SMU is locked to the GNSS signal
 */
#define RSMU_GET_STATE  _IOR(RSMU_MAGIC, 2, struct rsmu_get_state)

/**
 * struct rsmu_get_state
 * @dpll: dpll index (Digital Phase Lock Loop)
 * @ffo: dpll's ffo (fractional frequency offset) in ppqt
 *
 * ioctl to get SMU dpll ffo (fractional frequency offset).
 */
#define RSMU_GET_FFO  _IOR(RSMU_MAGIC, 3, struct rsmu_get_ffo)
#endif /* __UAPI_LINUX_RSMU_CDEV_H */
