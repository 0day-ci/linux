/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 IBM Corporation
 * Author: Nayna Jain
 *
 * Platform keystore for pseries.
 */
#ifndef _PSERIES_PKS_H
#define _PSERIES_PKS_H


#include <linux/types.h>
#include <linux/list.h>

struct pks_var {
	char *prefix;
	u8 *name;
	u16 namelen;
	u32 policy;
	u16 datalen;
	u8 *data;
};

struct pks_var_name {
	u16 namelen;
	u8  *name;
};

struct pks_var_name_list {
	u32 varcount;
	struct pks_var_name *varlist;
};

struct pks_config {
	u8 version;
	u8 flags;
	u32 rsvd0;
	u16 maxpwsize;
	u16 maxobjlabelsize;
	u16 maxobjsize;
	u32 totalsize;
	u32 usedspace;
	u32 supportedpolicies;
	u64 rsvd1;
} __packed;

/**
 * Successful return from this API  implies PKS is available.
 * This is used to initialize kernel driver and user interfaces.
 */
extern struct pks_config *pks_get_config(void);

/**
 * Returns all the var names for this prefix.
 * This only returns name list. If the caller needs data, it has to specifically
 * call read for the required var name.
 */
int pks_get_var_ids_for_type(char *prefix, struct pks_var_name_list *list);

/**
 * Writes the specified var and its data to PKS.
 * Any caller of PKS driver should present a valid prefix type for their
 * variable. This is an exception only for signed variables exposed via
 * sysfs which do not have any prefixes.
 * The prefix should always start with '/'. For eg. '/sysfs'.
 */
extern int pks_write_var(struct pks_var var);

/**
 * Writes the specified signed var and its data to PKS.
 */
extern int pks_update_signed_var(struct pks_var var);

/**
 * Removes the specified var and its data from PKS.
 */
extern int pks_remove_var(char *prefix, struct pks_var_name vname);

/**
 * Returns the data for the specified variable.
 */
extern int pks_read_var(struct pks_var *var);

#endif
