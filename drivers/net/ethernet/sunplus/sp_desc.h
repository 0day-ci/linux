/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright Sunplus Technology Co., Ltd.
 *       All rights reserved.
 */

#ifndef __SP_DESC_H__
#define __SP_DESC_H__

#include "sp_define.h"

void rx_descs_flush(struct sp_common *comm);
void tx_descs_clean(struct sp_common *comm);
void rx_descs_clean(struct sp_common *comm);
void descs_clean(struct sp_common *comm);
void descs_free(struct sp_common *comm);
void tx_descs_init(struct sp_common *comm);
int  rx_descs_init(struct sp_common *comm);
int  descs_alloc(struct sp_common *comm);
int  descs_init(struct sp_common *comm);

#endif
