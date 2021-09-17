// SPDX-License-Identifier: GPL-2.0+
/*
 * Author: Hanlu Li <lihanlu@loongson.cn>
 *         Huacai Chen <chenhuacai@loongson.cn>
 *
 * Copyright (C) 2020-2021 Loongson Technology Corporation Limited
 */

#undef DEBUG

#include <linux/moduleloader.h>
#include <linux/elf.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/kernel.h>

static int rela_stack_push(s64 stack_value, s64 *rela_stack, size_t *rela_stack_top)
{
	if (*rela_stack_top >= RELA_STACK_DEPTH)
		return -ENOEXEC;

	rela_stack[(*rela_stack_top)++] = stack_value;
	pr_debug("%s stack_value = 0x%llx\n", __func__, stack_value);

	return 0;
}

static int rela_stack_pop(s64 *stack_value, s64 *rela_stack, size_t *rela_stack_top)
{
	if (*rela_stack_top == 0)
		return -ENOEXEC;

	*stack_value = rela_stack[--(*rela_stack_top)];
	pr_debug("%s stack_value = 0x%llx\n", __func__, *stack_value);

	return 0;
}

static int apply_r_larch_none(struct module *me, u32 *location, Elf_Addr v,
				s64 *rela_stack, size_t *rela_stack_top)
{
	return 0;
}

static int apply_r_larch_32(struct module *me, u32 *location, Elf_Addr v,
				s64 *rela_stack, size_t *rela_stack_top)
{
	*location = v;
	return 0;
}

static int apply_r_larch_64(struct module *me, u32 *location, Elf_Addr v,
				s64 *rela_stack, size_t *rela_stack_top)
{
	*(Elf_Addr *)location = v;
	return 0;
}

static int apply_r_larch_mark_la(struct module *me, u32 *location, Elf_Addr v,
					s64 *rela_stack, size_t *rela_stack_top)
{
	return 0;
}

static int apply_r_larch_mark_pcrel(struct module *me, u32 *location, Elf_Addr v,
					s64 *rela_stack, size_t *rela_stack_top)
{
	return 0;
}

static int apply_r_larch_sop_push_pcrel(struct module *me, u32 *location, Elf_Addr v,
						s64 *rela_stack, size_t *rela_stack_top)
{
	return rela_stack_push(v - (u64)location, rela_stack, rela_stack_top);
}

static int apply_r_larch_sop_push_absolute(struct module *me, u32 *location, Elf_Addr v,
						s64 *rela_stack, size_t *rela_stack_top)
{
	return rela_stack_push(v, rela_stack, rela_stack_top);
}

static int apply_r_larch_sop_push_dup(struct module *me, u32 *location, Elf_Addr v,
						s64 *rela_stack, size_t *rela_stack_top)
{
	int err = 0;
	s64 opr1;

	err = rela_stack_pop(&opr1, rela_stack, rela_stack_top);
	if (err)
		return err;
	err = rela_stack_push(opr1, rela_stack, rela_stack_top);
	if (err)
		return err;
	err = rela_stack_push(opr1, rela_stack, rela_stack_top);
	if (err)
		return err;

	return 0;
}

static int apply_r_larch_sop_push_plt_pcrel(struct module *me, u32 *location, Elf_Addr v,
						s64 *rela_stack, size_t *rela_stack_top)
{
	return apply_r_larch_sop_push_pcrel(me, location, v, rela_stack, rela_stack_top);
}

static int apply_r_larch_sop_sub(struct module *me, u32 *location, Elf_Addr v,
					s64 *rela_stack, size_t *rela_stack_top)
{
	int err = 0;
	s64 opr1, opr2;

	err = rela_stack_pop(&opr2, rela_stack, rela_stack_top);
	if (err)
		return err;
	err = rela_stack_pop(&opr1, rela_stack, rela_stack_top);
	if (err)
		return err;
	err = rela_stack_push(opr1 - opr2, rela_stack, rela_stack_top);
	if (err)
		return err;

	return 0;
}

static int apply_r_larch_sop_sl(struct module *me, u32 *location, Elf_Addr v,
					s64 *rela_stack, size_t *rela_stack_top)
{
	int err = 0;
	s64 opr1, opr2;

	err = rela_stack_pop(&opr2, rela_stack, rela_stack_top);
	if (err)
		return err;
	err = rela_stack_pop(&opr1, rela_stack, rela_stack_top);
	if (err)
		return err;
	err = rela_stack_push(opr1 << opr2, rela_stack, rela_stack_top);
	if (err)
		return err;

	return 0;
}

static int apply_r_larch_sop_sr(struct module *me, u32 *location, Elf_Addr v,
					s64 *rela_stack, size_t *rela_stack_top)
{
	int err = 0;
	s64 opr1, opr2;

	err = rela_stack_pop(&opr2, rela_stack, rela_stack_top);
	if (err)
		return err;
	err = rela_stack_pop(&opr1, rela_stack, rela_stack_top);
	if (err)
		return err;
	err = rela_stack_push(opr1 >> opr2, rela_stack, rela_stack_top);
	if (err)
		return err;

	return 0;
}

static int apply_r_larch_sop_add(struct module *me, u32 *location, Elf_Addr v,
					s64 *rela_stack, size_t *rela_stack_top)
{
	int err = 0;
	s64 opr1, opr2;

	err = rela_stack_pop(&opr2, rela_stack, rela_stack_top);
	if (err)
		return err;
	err = rela_stack_pop(&opr1, rela_stack, rela_stack_top);
	if (err)
		return err;
	err = rela_stack_push(opr1 + opr2, rela_stack, rela_stack_top);
	if (err)
		return err;

	return 0;
}
static int apply_r_larch_sop_and(struct module *me, u32 *location, Elf_Addr v,
					s64 *rela_stack, size_t *rela_stack_top)
{
	int err = 0;
	s64 opr1, opr2;

	err = rela_stack_pop(&opr2, rela_stack, rela_stack_top);
	if (err)
		return err;
	err = rela_stack_pop(&opr1, rela_stack, rela_stack_top);
	if (err)
		return err;
	err = rela_stack_push(opr1 & opr2, rela_stack, rela_stack_top);
	if (err)
		return err;

	return 0;
}

static int apply_r_larch_sop_if_else(struct module *me, u32 *location, Elf_Addr v,
					s64 *rela_stack, size_t *rela_stack_top)
{
	int err = 0;
	s64 opr1, opr2, opr3;

	err = rela_stack_pop(&opr3, rela_stack, rela_stack_top);
	if (err)
		return err;
	err = rela_stack_pop(&opr2, rela_stack, rela_stack_top);
	if (err)
		return err;
	err = rela_stack_pop(&opr1, rela_stack, rela_stack_top);
	if (err)
		return err;
	err = rela_stack_push(opr1 ? opr2 : opr3, rela_stack, rela_stack_top);
	if (err)
		return err;

	return 0;
}

static int apply_r_larch_sop_pop_32_s_10_5(struct module *me, u32 *location, Elf_Addr v,
						s64 *rela_stack, size_t *rela_stack_top)
{
	int err = 0;
	s64 opr1;

	err = rela_stack_pop(&opr1, rela_stack, rela_stack_top);
	if (err)
		return err;

	/* check 5-bit signed */
	if ((opr1 & ~(u64)0xf) &&
	    (opr1 & ~(u64)0xf) != ~(u64)0xf) {
		pr_err("module %s: opr1 = 0x%llx overflow! dangerous %s relocation\n",
			me->name, opr1, __func__);
		return -ENOEXEC;
	}

	/* (*(uint32_t *) PC) [14 ... 10] = opr [4 ... 0] */
	*location = (*location & (~(u32)0x7c00)) | ((opr1 & 0x1f) << 10);

	return 0;
}

static int apply_r_larch_sop_pop_32_u_10_12(struct module *me, u32 *location, Elf_Addr v,
						s64 *rela_stack, size_t *rela_stack_top)
{
	int err = 0;
	s64 opr1;

	err = rela_stack_pop(&opr1, rela_stack, rela_stack_top);
	if (err)
		return err;

	/* check 12-bit unsigned */
	if (opr1 & ~(u64)0xfff) {
		pr_err("module %s: opr1 = 0x%llx overflow! dangerous %s relocation\n",
			me->name, opr1, __func__);
		return -ENOEXEC;
	}

	/* (*(uint32_t *) PC) [21 ... 10] = opr [11 ... 0] */
	*location = (*location & (~(u32)0x3ffc00)) | ((opr1 & 0xfff) << 10);

	return 0;
}

static int apply_r_larch_sop_pop_32_s_10_12(struct module *me, u32 *location, Elf_Addr v,
						s64 *rela_stack, size_t *rela_stack_top)
{
	int err = 0;
	s64 opr1;

	err = rela_stack_pop(&opr1, rela_stack, rela_stack_top);
	if (err)
		return err;

	/* check 12-bit signed */
	if ((opr1 & ~(u64)0x7ff) &&
	    (opr1 & ~(u64)0x7ff) != ~(u64)0x7ff) {
		pr_err("module %s: opr1 = 0x%llx overflow! dangerous %s relocation\n",
			me->name, opr1, __func__);
		return -ENOEXEC;
	}

	/* (*(uint32_t *) PC) [21 ... 10] = opr [11 ... 0] */
	*location = (*location & (~(u32)0x3ffc00)) | ((opr1 & 0xfff) << 10);

	return 0;
}

static int apply_r_larch_sop_pop_32_s_10_16(struct module *me, u32 *location, Elf_Addr v,
						s64 *rela_stack, size_t *rela_stack_top)
{
	int err = 0;
	s64 opr1;

	err = rela_stack_pop(&opr1, rela_stack, rela_stack_top);
	if (err)
		return err;

	/* check 16-bit signed */
	if ((opr1 & ~(u64)0x7fff) &&
	    (opr1 & ~(u64)0x7fff) != ~(u64)0x7fff) {
		pr_err("module %s: opr1 = 0x%llx overflow! dangerous %s relocation\n",
			me->name, opr1, __func__);
		return -ENOEXEC;
	}

	/* (*(uint32_t *) PC) [25 ... 10] = opr [15 ... 0] */
	*location = (*location & 0xfc0003ff) | ((opr1 & 0xffff) << 10);

	return 0;
}

static int apply_r_larch_sop_pop_32_s_10_16_s2(struct module *me, u32 *location, Elf_Addr v,
						s64 *rela_stack, size_t *rela_stack_top)
{
	int err = 0;
	s64 opr1;

	err = rela_stack_pop(&opr1, rela_stack, rela_stack_top);
	if (err)
		return err;

	/* check 4-aligned */
	if (opr1 % 4) {
		pr_err("module %s: opr1 = 0x%llx unaligned! dangerous %s relocation\n",
			me->name, opr1, __func__);
		return -ENOEXEC;
	}

	opr1 >>= 2;
	/* check 18-bit signed */
	if ((opr1 & ~(u64)0x7fff) &&
	    (opr1 & ~(u64)0x7fff) != ~(u64)0x7fff) {
		pr_err("module %s: opr1 = 0x%llx overflow! dangerous %s relocation\n",
			me->name, opr1, __func__);
		return -ENOEXEC;
	}

	/* (*(uint32_t *) PC) [25 ... 10] = opr [17 ... 2] */
	*location = (*location & 0xfc0003ff) | ((opr1 & 0xffff) << 10);

	return 0;
}

static int apply_r_larch_sop_pop_32_s_5_20(struct module *me, u32 *location, Elf_Addr v,
						s64 *rela_stack, size_t *rela_stack_top)
{
	int err = 0;
	s64 opr1;

	err = rela_stack_pop(&opr1, rela_stack, rela_stack_top);
	if (err)
		return err;

	/* check 20-bit signed */
	if ((opr1 & ~(u64)0x7ffff) &&
	    (opr1 & ~(u64)0x7ffff) != ~(u64)0x7ffff) {
		pr_err("module %s: opr1 = 0x%llx overflow! dangerous %s relocation\n",
			me->name, opr1, __func__);
		return -ENOEXEC;
	}

	/* (*(uint32_t *) PC) [24 ... 5] = opr [19 ... 0] */
	*location = (*location & (~(u32)0x1ffffe0)) | ((opr1 & 0xfffff) << 5);

	return 0;
}

static int apply_r_larch_sop_pop_32_s_0_5_10_16_s2(struct module *me, u32 *location, Elf_Addr v,
							s64 *rela_stack, size_t *rela_stack_top)
{
	int err = 0;
	s64 opr1;

	err = rela_stack_pop(&opr1, rela_stack, rela_stack_top);
	if (err)
		return err;

	/* check 4-aligned */
	if (opr1 % 4) {
		pr_err("module %s: opr1 = 0x%llx unaligned! dangerous %s relocation\n",
			me->name, opr1, __func__);
		return -ENOEXEC;
	}

	opr1 >>= 2;
	/* check 23-bit signed */
	if ((opr1 & ~(u64)0xfffff) &&
	    (opr1 & ~(u64)0xfffff) != ~(u64)0xfffff) {
		pr_err("module %s: opr1 = 0x%llx overflow! dangerous %s relocation\n",
			me->name, opr1, __func__);
		return -ENOEXEC;
	}

	/*
	 * (*(uint32_t *) PC) [4 ... 0] = opr [22 ... 18]
	 * (*(uint32_t *) PC) [25 ... 10] = opr [17 ... 2]
	 */
	*location = (*location & 0xfc0003e0)
		| ((opr1 & 0x1f0000) >> 16) | ((opr1 & 0xffff) << 10);

	return 0;
}

static int apply_r_larch_sop_pop_32_s_0_10_10_16_s2(struct module *me, u32 *location, Elf_Addr v,
							s64 *rela_stack, size_t *rela_stack_top)
{
	int err = 0;
	s64 opr1;

	err = rela_stack_pop(&opr1, rela_stack, rela_stack_top);
	if (err)
		return err;

	/* check 4-aligned */
	if (opr1 % 4) {
		pr_err("module %s: opr1 = 0x%llx unaligned! dangerous %s relocation\n",
			me->name, opr1, __func__);
		return -ENOEXEC;
	}

	opr1 >>= 2;
	/* check 28-bit signed */
	if ((opr1 & ~(u64)0x1ffffff) &&
	    (opr1 & ~(u64)0x1ffffff) != ~(u64)0x1ffffff) {
		pr_err("module %s: opr1 = 0x%llx overflow! dangerous %s relocation\n",
			me->name, opr1, __func__);
		return -ENOEXEC;
	}

	/*
	 * (*(uint32_t *) PC) [9 ... 0] = opr [27 ... 18]
	 * (*(uint32_t *) PC) [25 ... 10] = opr [17 ... 2]
	 */
	*location = (*location & 0xfc000000)
		| ((opr1 & 0x3ff0000) >> 16) | ((opr1 & 0xffff) << 10);

	return 0;
}

static int apply_r_larch_sop_pop_32_u(struct module *me, u32 *location, Elf_Addr v,
					s64 *rela_stack, size_t *rela_stack_top)
{
	int err = 0;
	s64 opr1;

	err = rela_stack_pop(&opr1, rela_stack, rela_stack_top);
	if (err)
		return err;

	/* check 32-bit unsigned */
	if (opr1 & ~(u64)0xffffffff) {
		pr_err("module %s: opr1 = 0x%llx overflow! dangerous %s relocation\n",
			me->name, opr1, __func__);
		return -ENOEXEC;
	}

	/* (*(uint32_t *) PC) = opr */
	*location = (u32)opr1;

	return 0;
}

static int apply_r_larch_add32(struct module *me, u32 *location, Elf_Addr v,
				s64 *rela_stack, size_t *rela_stack_top)
{
	*(s32 *)location += v;
	return 0;
}

static int apply_r_larch_add64(struct module *me, u32 *location, Elf_Addr v,
				s64 *rela_stack, size_t *rela_stack_top)
{
	*(s64 *)location += v;
	return 0;
}

static int apply_r_larch_sub32(struct module *me, u32 *location, Elf_Addr v,
				s64 *rela_stack, size_t *rela_stack_top)
{
	*(s32 *)location -= v;
	return 0;
}

static int apply_r_larch_sub64(struct module *me, u32 *location, Elf_Addr v,
				s64 *rela_stack, size_t *rela_stack_top)
{
	*(s64 *)location -= v;
	return 0;
}

/*
 * reloc_handlers_rela() - Apply a particular relocation to a module
 * @me: the module to apply the reloc to
 * @location: the address at which the reloc is to be applied
 * @v: the value of the reloc, with addend for RELA-style
 * @rela_stack: the stack used for store relocation info, LOCAL to THIS module
 * @rela_stac_top: where the stack operation(pop/push) applies to
 *
 * Return: 0 upon success, else -ERRNO
 */
typedef int (*reloc_rela_handler)(struct module *me, u32 *location, Elf_Addr v,
				s64 *rela_stack, size_t *rela_stack_top);

/* The handlers for known reloc types */
static reloc_rela_handler reloc_rela_handlers[] = {
	[R_LARCH_NONE]				= apply_r_larch_none,
	[R_LARCH_32]				= apply_r_larch_32,
	[R_LARCH_64]				= apply_r_larch_64,
	[R_LARCH_MARK_LA]			= apply_r_larch_mark_la,
	[R_LARCH_MARK_PCREL]			= apply_r_larch_mark_pcrel,
	[R_LARCH_SOP_PUSH_PCREL]		= apply_r_larch_sop_push_pcrel,
	[R_LARCH_SOP_PUSH_ABSOLUTE]		= apply_r_larch_sop_push_absolute,
	[R_LARCH_SOP_PUSH_DUP]			= apply_r_larch_sop_push_dup,
	[R_LARCH_SOP_PUSH_PLT_PCREL]		= apply_r_larch_sop_push_plt_pcrel,
	[R_LARCH_SOP_SUB]			= apply_r_larch_sop_sub,
	[R_LARCH_SOP_SL]			= apply_r_larch_sop_sl,
	[R_LARCH_SOP_SR]			= apply_r_larch_sop_sr,
	[R_LARCH_SOP_ADD]			= apply_r_larch_sop_add,
	[R_LARCH_SOP_AND]			= apply_r_larch_sop_and,
	[R_LARCH_SOP_IF_ELSE]			= apply_r_larch_sop_if_else,
	[R_LARCH_SOP_POP_32_S_10_5]		= apply_r_larch_sop_pop_32_s_10_5,
	[R_LARCH_SOP_POP_32_U_10_12]		= apply_r_larch_sop_pop_32_u_10_12,
	[R_LARCH_SOP_POP_32_S_10_12]		= apply_r_larch_sop_pop_32_s_10_12,
	[R_LARCH_SOP_POP_32_S_10_16]		= apply_r_larch_sop_pop_32_s_10_16,
	[R_LARCH_SOP_POP_32_S_10_16_S2]		= apply_r_larch_sop_pop_32_s_10_16_s2,
	[R_LARCH_SOP_POP_32_S_5_20]		= apply_r_larch_sop_pop_32_s_5_20,
	[R_LARCH_SOP_POP_32_S_0_5_10_16_S2]	= apply_r_larch_sop_pop_32_s_0_5_10_16_s2,
	[R_LARCH_SOP_POP_32_S_0_10_10_16_S2]	= apply_r_larch_sop_pop_32_s_0_10_10_16_s2,
	[R_LARCH_SOP_POP_32_U]			= apply_r_larch_sop_pop_32_u,
	[R_LARCH_ADD32]				= apply_r_larch_add32,
	[R_LARCH_ADD64]				= apply_r_larch_add64,
	[R_LARCH_SUB32]				= apply_r_larch_sub32,
	[R_LARCH_SUB64]				= apply_r_larch_sub64,
};

int apply_relocate(Elf_Shdr *sechdrs, const char *strtab,
		   unsigned int symindex, unsigned int relsec,
		   struct module *me)
{
	int i, err;
	unsigned int type;
	s64 rela_stack[RELA_STACK_DEPTH];
	size_t rela_stack_top = 0;
	reloc_rela_handler handler;
	void *location;
	Elf_Addr v;
	Elf_Sym *sym;
	Elf_Rel *rel = (void *) sechdrs[relsec].sh_addr;

	pr_debug("%s: Applying relocate section %u to %u\n", __func__, relsec,
	       sechdrs[relsec].sh_info);

	rela_stack_top = 0;
	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rel); i++) {
		/* This is where to make the change */
		location = (void *)sechdrs[sechdrs[relsec].sh_info].sh_addr + rel[i].r_offset;
		/* This is the symbol it is referring to */
		sym = (Elf_Sym *)sechdrs[symindex].sh_addr + ELF_R_SYM(rel[i].r_info);
		if (IS_ERR_VALUE(sym->st_value)) {
			/* Ignore unresolved weak symbol */
			if (ELF_ST_BIND(sym->st_info) == STB_WEAK)
				continue;
			pr_warn("%s: Unknown symbol %s\n", me->name, strtab + sym->st_name);
			return -ENOENT;
		}

		type = ELF_R_TYPE(rel[i].r_info);

		if (type < ARRAY_SIZE(reloc_rela_handlers))
			handler = reloc_rela_handlers[type];
		else
			handler = NULL;

		if (!handler) {
			pr_err("%s: Unknown relocation type %u\n", me->name, type);
			return -EINVAL;
		}

		v = sym->st_value;
		err = handler(me, location, v, rela_stack, &rela_stack_top);
		if (err)
			return err;
	}

	return 0;
}

int apply_relocate_add(Elf_Shdr *sechdrs, const char *strtab,
		       unsigned int symindex, unsigned int relsec,
		       struct module *me)
{
	int i, err;
	unsigned int type;
	s64 rela_stack[RELA_STACK_DEPTH];
	size_t rela_stack_top = 0;
	reloc_rela_handler handler;
	void *location;
	Elf_Addr v;
	Elf_Sym *sym;
	Elf_Rela *rel = (void *) sechdrs[relsec].sh_addr;

	pr_debug("%s: Applying relocate section %u to %u\n", __func__, relsec,
	       sechdrs[relsec].sh_info);

	rela_stack_top = 0;
	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rel); i++) {
		/* This is where to make the change */
		location = (void *)sechdrs[sechdrs[relsec].sh_info].sh_addr + rel[i].r_offset;
		/* This is the symbol it is referring to */
		sym = (Elf_Sym *)sechdrs[symindex].sh_addr + ELF_R_SYM(rel[i].r_info);
		if (IS_ERR_VALUE(sym->st_value)) {
			/* Ignore unresolved weak symbol */
			if (ELF_ST_BIND(sym->st_info) == STB_WEAK)
				continue;
			pr_warn("%s: Unknown symbol %s\n", me->name, strtab + sym->st_name);
			return -ENOENT;
		}

		type = ELF_R_TYPE(rel[i].r_info);

		if (type < ARRAY_SIZE(reloc_rela_handlers))
			handler = reloc_rela_handlers[type];
		else
			handler = NULL;

		if (!handler) {
			pr_err("%s: Unknown relocation type %u\n", me->name, type);
			return -EINVAL;
		}

		pr_debug("type %d st_value %llx r_addend %llx loc %llx\n",
		       (int)ELF64_R_TYPE(rel[i].r_info),
		       sym->st_value, rel[i].r_addend, (u64)location);

		v = sym->st_value + rel[i].r_addend;
		err = handler(me, location, v, rela_stack, &rela_stack_top);
		if (err)
			return err;
	}

	return 0;
}
