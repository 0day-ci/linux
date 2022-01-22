// SPDX-License-Identifier: GPL-2.0-only
/*
 * POWER platform keystore
 * Copyright (C) 2010 IBM Corporation
 *
 * This pseries platform device driver provides access to
 * variables stored in platform keystore.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/device.h>
#include <asm/hvcall.h>
#include <asm/firmware.h>
#include <linux/slab.h>
#include <asm/pks.h>
#include <asm/unaligned.h>
#include <asm/machdep.h>
#include <linux/string.h>

#define MODULE_VERS "1.0"
#define MODULE_NAME "pseries-pks"

static bool configset;
static struct pks_config *config;

struct pks_var_name_one {
	struct pks_var_name var;
	struct list_head link;
};

LIST_HEAD(pks_var_name_list);

static u64 labelcount;

struct pks_auth {
	u8 version;
	u8 consumer;
	__be64 rsvd0;
	__be32 rsvd1;
	__be16 passwordlength;
	u8 password[32];
} __attribute__ ((packed, aligned(16)));

static struct pks_auth auth;

static int pseries_status_to_err(int rc)
{
	int err;

	switch (rc) {
	case H_SUCCESS:
		err = 0;
		break;
	case H_FUNCTION:
		err = -ENXIO;
		break;
	case H_P2:
	case H_P3:
	case H_P4:
	case H_P5:
	case H_P6:
		err = -EINVAL;
		break;
	case H_NOT_FOUND:
		err = -ENOENT;
		break;
	case H_BUSY:
		err = -EBUSY;
		break;
	case H_AUTHORITY:
		err = -EPERM;
		break;
	case H_NO_MEM:
		err = -ENOMEM;
		break;
	case H_RESOURCE:
		err = -EEXIST;
		break;
	case H_TOO_BIG:
		err = -EFBIG;
		break;
	default:
		err = -EINVAL;
	}

	return err;
}

static int pks_gen_password(u8 *password[])
{
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE] = {0};
	u8 consumer = 0x3;
	int rc;

	rc = plpar_hcall(H_PKS_GEN_PASSWORD,
			retbuf,
			consumer,
			0,
			virt_to_phys(*password),
			config->maxpwsize);

	return pseries_status_to_err(rc);
}

static int construct_auth(void)
{
	int rc = 0;
	u8 *password;

	auth.version = 1;
	auth.consumer = 0x3;
	auth.rsvd0 = 0;
	auth.rsvd1 = 0;
	auth.passwordlength = cpu_to_be16(config->maxpwsize);
	password = kzalloc(config->maxpwsize, GFP_KERNEL);
	if (!password)
		return -ENOMEM;

	rc = pks_gen_password(&password);
	if (rc) {
		if (rc == H_IN_USE) {
			rc = 0;
		} else {
			pr_err("Failed setting password\n");
			rc = pseries_status_to_err(rc);
			goto err;
		}
	}
	memcpy(auth.password, password, config->maxpwsize);

err:
	kfree(password);
	return rc;
}

static bool validate_name(char *name)
{
	int i = 0;

	for (i = 0; i < strlen(name); i++) {
		if (!isalnum(name[i]) && (name[i] != '-')
				      && (name[i] != '_')) {
			pr_err("invalid name, should only contain alphanumeric,hyphen(-) or underscore(_)\n");
			return false;
		}
	}

	return true;
}

static int construct_label(char *prefix, u8 *name, u16 namelen, u8 **label)
{
	int varlen;

	if (!label)
		return -EINVAL;

	if (!prefix) {
		*label = kzalloc(namelen, GFP_KERNEL);
		if (!*label)
			return -ENOMEM;
		memcpy(*label, name, namelen);
	} else {
		varlen = strlen(prefix) + namelen + 1;
		*label = kzalloc(varlen, GFP_KERNEL);
		if (!*label)
			return -ENOMEM;

		memcpy(*label, prefix, strlen(prefix));
		(*label)[strlen(prefix)] = '/';
		memcpy(*label + strlen(prefix) + 1, name, namelen);
	}

	return 0;
}

static int _pks_get_config(void)
{
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE] = {0};
	int rc;
	size_t size = sizeof(struct pks_config);

	config = kzalloc(size, GFP_KERNEL);
	if (!config)
		return -ENOMEM;

	rc = plpar_hcall(H_PKS_GET_CONFIG,
			retbuf,
			virt_to_phys(config),
			size);

	if (rc != H_SUCCESS)
		return pseries_status_to_err(rc);

	config->rsvd0 = be32_to_cpu(config->rsvd0);
	config->maxpwsize = be16_to_cpu(config->maxpwsize);
	config->maxobjlabelsize = be16_to_cpu(config->maxobjlabelsize);
	config->maxobjsize = be16_to_cpu(config->maxobjsize);
	config->totalsize = be32_to_cpu(config->totalsize);
	config->usedspace =  be32_to_cpu(config->usedspace);
	config->supportedpolicies =  be32_to_cpu(config->supportedpolicies);
	config->rsvd1 = be64_to_cpu(config->rsvd1);

	configset = true;

	return rc;
}

static int get_objectlabels(void)
{
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE] = {0};
	int rc = 0;
	u16 bufsize = 1024;
	u8 buf[1024];
	int i;
	int index;
	u16 labelsize = 0;
	u64 continuetoken = 0;
	u64 count;
	struct pks_var_name_one *vname = NULL;

	do {
		rc = plpar_hcall(H_PKS_GET_OBJECT_LABELS,
				retbuf,
				virt_to_phys(&auth),
				continuetoken,
				virt_to_phys(buf),
				bufsize);

		if (rc) {
			rc = pseries_status_to_err(rc);
			goto err;
		}

		count =  retbuf[0];
		continuetoken = retbuf[1];
		index = 0;
		for (i = 0; i < count; i++) {
			labelsize = be16_to_cpu(*(__be16 *)(&buf[index]));
			vname = kzalloc(sizeof(struct pks_var_name_one),
					GFP_KERNEL);
			vname->var.namelen = labelsize;
			vname->var.name = kzalloc(labelsize, GFP_KERNEL);
			if (!vname->var.name) {
				rc = -ENOMEM;
				goto err;
			}
			index = index + 2;
			memcpy(vname->var.name, buf + index, labelsize);
			list_add(&vname->link, &pks_var_name_list);
			index =  index + labelsize;
		}
		labelcount = labelcount + count;
		pr_info("Total number of variables are %llu\n", labelcount);
	} while (continuetoken != 0);
err:
	return rc;
}

int pks_get_var_ids_for_type(char *prefix, struct pks_var_name_list *list)
{
	int count = 0;
	int idx = 0;
	struct pks_var_name_one *vname = NULL;
	u8 *name;
	u16 namelen;

	list_for_each_entry(vname, &pks_var_name_list, link) {
		name = vname->var.name;
		if (((!prefix) && (name[0] == '/'))
		   || (prefix && (strncmp(name, prefix, strlen(prefix)))))
			continue;
		count++;
	}

	list->varcount = count;
	list->varlist = kcalloc(count, sizeof(list->varlist), GFP_KERNEL);
	if (!list->varlist)
		return -ENOMEM;

	list_for_each_entry(vname, &pks_var_name_list, link) {
		name = (char *)vname->var.name;
		if (((!prefix) && (name[0] == '/'))
		   || (prefix && (strncmp(name, prefix, strlen(prefix)))))
			continue;

		if (!prefix)
			namelen = vname->var.namelen;
		else {
			name = name + strlen(prefix) + 1;
			namelen = strlen(name) + 1;
		}
		pr_debug("var is %s of size %d\n", name, namelen);

		list->varlist[idx].namelen = namelen;
		list->varlist[idx].name = kzalloc(namelen, GFP_KERNEL);
		memcpy(list->varlist[idx].name, name, namelen);
		idx++;
	}

	return 0;
}
EXPORT_SYMBOL(pks_get_var_ids_for_type);

int pks_update_signed_var(struct pks_var var)
{
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE] = {0};
	int rc;
	u8 *label;
	u16 varlen;
	u8 *data = var.data;

	if (var.prefix)
		return -EINVAL;

	if (!validate_name(var.name))
		return -EINVAL;

	rc = construct_label(var.prefix, var.name, var.namelen, &label);
	if (rc)
		return rc;

	pr_info("Label to be written is %s of size %d\n", label, varlen);
	varlen = strlen(label) + 1;
	rc = plpar_hcall(H_PKS_SB_SIGNED_UPDATE,
			retbuf,
			virt_to_phys(&auth),
			virt_to_phys(label),
			varlen,
			var.policy,
			virt_to_phys(data),
			var.datalen);

	kfree(label);

	return pseries_status_to_err(rc);
}
EXPORT_SYMBOL(pks_update_signed_var);

int pks_write_var(struct pks_var var)
{
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE] = {0};
	int rc;
	u8 *label;
	u16 varlen;
	u8 *data = var.data;

	if ((!var.prefix) || (var.prefix[0] != '/'))
		return -EINVAL;

	if (!validate_name(var.name))
		return -EINVAL;

	rc = construct_label(var.prefix, var.name, var.namelen, &label);
	if (rc)
		return rc;

	pr_info("Label to be written is %s of size %d\n", label, varlen);
	varlen = strlen(label) + 1;
	rc = plpar_hcall(H_PKS_WRITE_OBJECT,
			retbuf,
			virt_to_phys(&auth),
			virt_to_phys(label),
			varlen,
			var.policy,
			virt_to_phys(data),
			var.datalen);

	kfree(label);

	return pseries_status_to_err(rc);
}
EXPORT_SYMBOL(pks_write_var);

int pks_remove_var(char *prefix, struct pks_var_name vname)
{
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE] = {0};
	int rc;
	u8 *label;
	u16 varlen;

	rc = construct_label(prefix, vname.name, vname.namelen, &label);
	if (rc)
		return rc;

	varlen = strlen(label) + 1;
	pr_info("Label to be removed is %s of size %d\n", label, varlen);
	rc = plpar_hcall(H_PKS_REMOVE_OBJECT,
			retbuf,
			virt_to_phys(&auth),
			virt_to_phys(label),
			varlen);

	kfree(label);

	return pseries_status_to_err(rc);
}
EXPORT_SYMBOL(pks_remove_var);


int pks_read_var(struct pks_var *var)
{
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE] = {0};
	int rc;
	u16 outlen = config->maxobjsize;
	u8 *label;
	u8 *out;
	u16 varlen;

	rc = construct_label(var->prefix, var->name, var->namelen, &label);
	if (rc)
		return rc;

	varlen = strlen(label) + 1;
	pr_info("Label to be read %s of size %d\n", label, varlen);
	out = kzalloc(outlen, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	rc = plpar_hcall(H_PKS_READ_OBJECT,
			retbuf,
			virt_to_phys(&auth),
			virt_to_phys(label),
			varlen,
			virt_to_phys(out),
			outlen);

	if (rc != H_SUCCESS) {
		pr_err("Failed to read %d\n", rc);
		rc = pseries_status_to_err(rc);
		goto err;
	}

	var->datalen = retbuf[0];
	var->policy = retbuf[1];

	var->data = kzalloc(var->datalen, GFP_KERNEL);
	if (!var->data) {
		rc = -ENOMEM;
		goto err;
	}

	memcpy(var->data, out, var->datalen);
err:
	kfree(out);
	kfree(label);

	return rc;
}
EXPORT_SYMBOL(pks_read_var);

struct pks_config *pks_get_config(void)
{

	if (!configset) {
		if (_pks_get_config())
			return NULL;
	}

	return config;
}
EXPORT_SYMBOL(pks_get_config);

int __init pseries_pks_init(void)
{
	int rc = 0;
	struct pks_var_name_one *vname = NULL;

	rc = _pks_get_config();

	if (rc) {
		pr_err("Error initializing pks\n");
		return rc;
	}

	rc = construct_auth();
	if (rc)
		return rc;

	rc = get_objectlabels();
	if (rc) {
		pr_err("Getting object labels failed. Error initializing pks\n");
		return rc;
	}

	list_for_each_entry(vname, &pks_var_name_list, link)
		pr_info("name is %s\n", vname->var.name);

	return rc;
}
arch_initcall(pseries_pks_init);
