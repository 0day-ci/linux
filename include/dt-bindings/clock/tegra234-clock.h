/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018-2022, NVIDIA CORPORATION. All rights reserved. */

#ifndef DT_BINDINGS_CLOCK_TEGRA234_CLOCK_H
#define DT_BINDINGS_CLOCK_TEGRA234_CLOCK_H

/**
 * @file
 * @defgroup bpmp_clock_ids Clock ID's
 * @{
 */
/**
 * @brief controls the EMC clock frequency.
 * @details Doing a clk_set_rate on this clock will select the
 * appropriate clock source, program the source rate and execute a
 * specific sequence to switch to the new clock source for both memory
 * controllers. This can be used to control the balance between memory
 * throughput and memory controller power.
 */
#define TEGRA234_CLK_EMC			31U
/** @brief output of gate CLK_ENB_FUSE */
#define TEGRA234_CLK_FUSE			40U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_SDMMC4 */
#define TEGRA234_CLK_SDMMC4			123U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_UARTA */
#define TEGRA234_CLK_UARTA			155U
/** @brief output of gate CLK_ENB_PEX1_CORE_6 */
#define TEGRA234_CLK_PEX1_C6_CORE		161U
/** @brief output of gate CLK_ENB_PEX2_CORE_7 */
#define TEGRA234_CLK_PEX2_C7_CORE		171U
/** @brief output of gate CLK_ENB_PEX2_CORE_8 */
#define TEGRA234_CLK_PEX2_C8_CORE		172U
/** @brief output of gate CLK_ENB_PEX2_CORE_9 */
#define TEGRA234_CLK_PEX2_C9_CORE		173U
/** @brief output of gate CLK_ENB_PEX2_CORE_10 */
#define TEGRA234_CLK_PEX2_C10_CORE		187U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_SDMMC_LEGACY_TM switch divider output */
#define TEGRA234_CLK_SDMMC_LEGACY_TM		219U
/** @brief output of gate CLK_ENB_PEX0_CORE_0 */
#define TEGRA234_CLK_PEX0_C0_CORE		220U
/** @brief output of gate CLK_ENB_PEX0_CORE_1 */
#define TEGRA234_CLK_PEX0_C1_CORE		221U
/** @brief output of gate CLK_ENB_PEX0_CORE_2 */
#define TEGRA234_CLK_PEX0_C2_CORE		222U
/** @brief output of gate CLK_ENB_PEX0_CORE_3 */
#define TEGRA234_CLK_PEX0_C3_CORE		223U
/** @brief output of gate CLK_ENB_PEX0_CORE_4 */
#define TEGRA234_CLK_PEX0_C4_CORE		224U
/** @brief output of gate CLK_ENB_PEX1_CORE_5 */
#define TEGRA234_CLK_PEX1_C5_CORE		225U

/** @brief PLL controlled by CLK_RST_CONTROLLER_PLLC4_BASE */
#define TEGRA234_CLK_PLLC4			237U
/** @brief 32K input clock provided by PMIC */
#define TEGRA234_CLK_CLK_32K			289U

#endif
