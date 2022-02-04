/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _VIVALDI_KEYMAP_H
#define _VIVALDI_KEYMAP_H

#include <linux/types.h>

#define VIVALDI_MIN_FN_ROW_KEY	1
#define VIVALDI_MAX_FN_ROW_KEY	24

/**
 * struct vivaldi_data - Function row keymap data for ChromeOS vivaldi keyboards
 * @function_row_physmap: An array of the encoded rows/columns for the top
 *                        row function keys, in an order from left to right
 * @num_function_row_keys: The number of top row keys in a custom keyboard
 *
 * This structure is supposed to be used by ChromeOS keyboards using
 * the vivaldi keyboard function row design.
 */
struct vivaldi_data {
	u16 function_row_physmap[VIVALDI_MAX_FN_ROW_KEY - VIVALDI_MIN_FN_ROW_KEY + 1];
	int num_function_row_keys;
};


ssize_t vivaldi_function_row_physmap_show(const struct vivaldi_data *data,
					  char *buf);

#endif /* _VIVALDI_KEYMAP_H */
