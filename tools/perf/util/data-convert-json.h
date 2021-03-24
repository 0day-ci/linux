/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __DATA_CONVERT_JSON_H
#define __DATA_CONVERT_JSON_H
#include "data-convert.h"

int bt_convert__perf2json(const char *input_name, const char *to_ctf,
			 struct perf_data_convert_opts *opts);

#endif /* __DATA_CONVERT_JSON_H */
