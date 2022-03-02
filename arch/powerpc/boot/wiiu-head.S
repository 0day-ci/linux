/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Entrypoint head asm
 * This code expects to be called directly after the Espresso BootROM, which implies certain MSR
 * states.
 *
 * Nintendo Wii U bootwrapper support
 * Copyright (C) 2022 The linux-wiiu Team
 */

#include "ppc_asm.h"

.text
.global _zimage_start
_zimage_start:
	/* Clear out all BATs. MMU is already off. */
	li r8, 0

	mtspr 0x210, r8 /* IBAT0U */
	mtspr 0x212, r8 /* IBAT1U */
	mtspr 0x214, r8 /* IBAT2U */
	mtspr 0x216, r8 /* IBAT3U */
	mtspr 0x218, r8 /* DBAT0U */
	mtspr 0x21a, r8 /* DBAT1U */
	mtspr 0x21c, r8 /* DBAT2U */
	mtspr 0x21e, r8 /* DBAT3U */

	mtspr 0x230, r8 /* IBAT4U */
	mtspr 0x232, r8 /* IBAT5U */
	mtspr 0x234, r8 /* IBAT6U */
	mtspr 0x236, r8 /* IBAT7U */
	mtspr 0x238, r8 /* DBAT4U */
	mtspr 0x23a, r8 /* DBAT5U */
	mtspr 0x23c, r8 /* DBAT6U */
	mtspr 0x23e, r8 /* DBAT7U */

	/* MEM1 */
	/* 32MB, starting at 0x00000000... */
	li r8, 0x000003ff@l
	mtspr 0x21a, r8 /* DBAT1U */
	mtspr 0x212, r8 /* IBAT1U */

	/* mapped into phys 0x00000000; read-write; normal caching. */
	li r8, 0x00000002@l
	mtspr 0x21b, r8 /* DBAT1L */
	mtspr 0x213, r8 /* IBAT1L */

	/* Latte MMIO area */
	/* 64MB, starting at 0x0d800000... */
	lis r8, 0x0d8007ff@h
	ori r8, r8,0x0d800007ff@l
	mtspr 0x21c, r8 /* DBAT2U */

	/* mapped into phys 0x0d800000; read-write; writethrough/nocache/guard*/
	lis r8, 0x0d80006A@h
	ori r8, r8,0x0d80006A@l
	mtspr 0x21d, r8 /* DBAT2L */

	/* Bootloader comms area (actually 0x89200000) */
	/* 128MiB, starting at 0x88000000... */
	lis r8, 0x88000fff@h
	ori r8, r8, 0x88000fff@l
	mtspr 0x21e, r8 /* DBAT3U */

	/* mapped into phys 0x88000000; read-write; normal caching. */
	lis r8, 0x88000002@h
	ori r8, r8, 0x88000002@l
	mtspr 0x21f, r8 /* DBAT3L */

	/* Turn on BAT 4 through 7, configure CPU */
	mfspr r8, 0x3f3 /* HID4 */
	oris r8, r8, 0x3200 /* L2FM: 64-byte fetch, BPD: max depth is 4, SBE */
	mtspr 0x3f3, r8

	/* Turn on and invalidate caches */
	mfspr r8, 0x3f0 /* HID0 */
	/* HID0_ICE | HID0_ICFI | HID0_DCE | HID0_DCFI */
	ori r8, r8, (1 << 15) | (1 << 11) | (1 << 14) | (1 << 10)
	mtspr 0x3f0, r8
	isync

	li r3, 0
	li r4, 0
	li r5, 0

	bl get_lr
get_lr:
	mflr r8
	/* Get absolute address of done */
	addi r8, r8, done - get_lr

	/* Set up MSR */
	/* MSR_DR | MSR_IR | MSR_FP */
	/* Data/instruction MMUs, FPU because why not */
	ori r9, r3, (1 << 4) | (1 << 5) | (1 << 13)

	/* Apply our new MSR, starting execution at "done" */
	mtsrr0 r8
	mtsrr1 r9
	sync
	rfi
done:
	b _zimage_start_lib
