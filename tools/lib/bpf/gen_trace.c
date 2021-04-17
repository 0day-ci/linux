// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
/* Copyright (c) 2021 Facebook */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <linux/filter.h>
#include "btf.h"
#include "bpf.h"
#include "libbpf.h"
#include "libbpf_internal.h"
#include "hashmap.h"
#include "bpf_gen_internal.h"

#define MAX_USED_MAPS 64
#define MAX_USED_PROGS 32

/* The following structure describes the stack layout of the loader program.
 * In addition R6 contains the pointer to context.
 * R7 contains the result of the last sys_bpf command (typically error or FD).
 */
struct loader_stack {
	__u32 btf_fd;
	__u32 map_fd[MAX_USED_MAPS];
	__u32 prog_fd[MAX_USED_PROGS];
	__u32 inner_map_fd;
	__u32 last_btf_id;
	__u32 last_attach_btf_obj_fd;
};
#define stack_off(field) (__s16)(-sizeof(struct loader_stack) + offsetof(struct loader_stack, field))

static int bpf_gen__realloc_insn_buf(struct bpf_gen *gen, __u32 size)
{
	size_t off = gen->insn_cur - gen->insn_start;

	if (gen->error)
		return -ENOMEM;
	if (off + size > UINT32_MAX) {
		gen->error = -ERANGE;
		return -ERANGE;
	}
	gen->insn_start = realloc(gen->insn_start, off + size);
	if (!gen->insn_start) {
		gen->error = -ENOMEM;
		return -ENOMEM;
	}
	gen->insn_cur = gen->insn_start + off;
	return 0;
}

static int bpf_gen__realloc_data_buf(struct bpf_gen *gen, __u32 size)
{
	size_t off = gen->data_cur - gen->data_start;

	if (gen->error)
		return -ENOMEM;
	if (off + size > UINT32_MAX) {
		gen->error = -ERANGE;
		return -ERANGE;
	}
	gen->data_start = realloc(gen->data_start, off + size);
	if (!gen->data_start) {
		gen->error = -ENOMEM;
		return -ENOMEM;
	}
	gen->data_cur = gen->data_start + off;
	return 0;
}

static void bpf_gen__emit(struct bpf_gen *gen, struct bpf_insn insn)
{
	if (bpf_gen__realloc_insn_buf(gen, sizeof(insn)))
		return;
	memcpy(gen->insn_cur, &insn, sizeof(insn));
	gen->insn_cur += sizeof(insn);
}

static void bpf_gen__emit2(struct bpf_gen *gen, struct bpf_insn insn1, struct bpf_insn insn2)
{
	bpf_gen__emit(gen, insn1);
	bpf_gen__emit(gen, insn2);
}

void bpf_gen__init(struct bpf_gen *gen, int log_level)
{
	gen->log_level = log_level;
	bpf_gen__emit(gen, BPF_MOV64_REG(BPF_REG_6, BPF_REG_1));
	bpf_gen__emit(gen, BPF_ST_MEM(BPF_W, BPF_REG_10, stack_off(last_attach_btf_obj_fd), 0));
}

static int bpf_gen__add_data(struct bpf_gen *gen, const void *data, __u32 size)
{
	void *prev;

	if (bpf_gen__realloc_data_buf(gen, size))
		return 0;
	prev = gen->data_cur;
	memcpy(gen->data_cur, data, size);
	gen->data_cur += size;
	return prev - gen->data_start;
}

static int insn_bytes_to_bpf_size(__u32 sz)
{
	switch (sz) {
	case 8: return BPF_DW;
	case 4: return BPF_W;
	case 2: return BPF_H;
	case 1: return BPF_B;
	default: return -1;
	}
}

/* *(u64 *)(blob + off) = (u64)(void *)(blob + data) */
static void bpf_gen__emit_rel_store(struct bpf_gen *gen, int off, int data)
{
	bpf_gen__emit2(gen, BPF_LD_IMM64_RAW_FULL(BPF_REG_0, BPF_PSEUDO_MAP_IDX_VALUE, 0, 0, 0, data));
	bpf_gen__emit2(gen, BPF_LD_IMM64_RAW_FULL(BPF_REG_1, BPF_PSEUDO_MAP_IDX_VALUE, 0, 0, 0, off));
	bpf_gen__emit(gen, BPF_STX_MEM(BPF_DW, BPF_REG_1, BPF_REG_0, 0));
}

/* *(u64 *)(blob + off) = (u64)(void *)(%sp + stack_off) */
static void bpf_gen__emit_rel_store_sp(struct bpf_gen *gen, int off, int stack_off)
{
	bpf_gen__emit(gen, BPF_MOV64_REG(BPF_REG_0, BPF_REG_10));
	bpf_gen__emit(gen, BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, stack_off));
	bpf_gen__emit2(gen, BPF_LD_IMM64_RAW_FULL(BPF_REG_1, BPF_PSEUDO_MAP_IDX_VALUE, 0, 0, 0, off));
	bpf_gen__emit(gen, BPF_STX_MEM(BPF_DW, BPF_REG_1, BPF_REG_0, 0));
}

static void bpf_gen__move_ctx2blob(struct bpf_gen *gen, int off, int size, int ctx_off)
{
	bpf_gen__emit(gen, BPF_LDX_MEM(insn_bytes_to_bpf_size(size), BPF_REG_0, BPF_REG_6, ctx_off));
	bpf_gen__emit2(gen, BPF_LD_IMM64_RAW_FULL(BPF_REG_1, BPF_PSEUDO_MAP_IDX_VALUE, 0, 0, 0, off));
	bpf_gen__emit(gen, BPF_STX_MEM(insn_bytes_to_bpf_size(size), BPF_REG_1, BPF_REG_0, 0));
}

static void bpf_gen__move_stack2blob(struct bpf_gen *gen, int off, int size, int stack_off)
{
	bpf_gen__emit(gen, BPF_LDX_MEM(insn_bytes_to_bpf_size(size), BPF_REG_0, BPF_REG_10, stack_off));
	bpf_gen__emit2(gen, BPF_LD_IMM64_RAW_FULL(BPF_REG_1, BPF_PSEUDO_MAP_IDX_VALUE, 0, 0, 0, off));
	bpf_gen__emit(gen, BPF_STX_MEM(insn_bytes_to_bpf_size(size), BPF_REG_1, BPF_REG_0, 0));
}

static void bpf_gen__move_stack2ctx(struct bpf_gen *gen, int ctx_off, int size, int stack_off)
{
	bpf_gen__emit(gen, BPF_LDX_MEM(insn_bytes_to_bpf_size(size), BPF_REG_0, BPF_REG_10, stack_off));
	bpf_gen__emit(gen, BPF_STX_MEM(insn_bytes_to_bpf_size(size), BPF_REG_6, BPF_REG_0, ctx_off));
}

static void bpf_gen__emit_sys_bpf(struct bpf_gen *gen, int cmd, int attr, int attr_size)
{
	bpf_gen__emit(gen, BPF_MOV64_IMM(BPF_REG_1, cmd));
	bpf_gen__emit2(gen, BPF_LD_IMM64_RAW_FULL(BPF_REG_2, BPF_PSEUDO_MAP_IDX_VALUE, 0, 0, 0, attr));
	bpf_gen__emit(gen, BPF_MOV64_IMM(BPF_REG_3, attr_size));
	bpf_gen__emit(gen, BPF_EMIT_CALL(BPF_FUNC_sys_bpf));
	/* remember the result in R7 */
	bpf_gen__emit(gen, BPF_MOV64_REG(BPF_REG_7, BPF_REG_0));
}

static void bpf_gen__emit_check_err(struct bpf_gen *gen)
{
	bpf_gen__emit(gen, BPF_JMP_IMM(BPF_JSGE, BPF_REG_7, 0, 2));
	bpf_gen__emit(gen, BPF_MOV64_REG(BPF_REG_0, BPF_REG_7));
	bpf_gen__emit(gen, BPF_EXIT_INSN());
}

static void __bpf_gen__debug(struct bpf_gen *gen, int reg1, int reg2, const char *fmt, va_list args)
{
	char buf[1024];
	int addr, len, ret;

	if (!gen->log_level)
		return;
	ret = vsnprintf(buf, sizeof(buf), fmt, args);
	if (ret < 1024 - 7 && reg1 >= 0 && reg2 < 0)
		strcat(buf, " r=%d");
	len = strlen(buf) + 1;
	addr = bpf_gen__add_data(gen, buf, len);

	bpf_gen__emit2(gen, BPF_LD_IMM64_RAW_FULL(BPF_REG_1, BPF_PSEUDO_MAP_IDX_VALUE, 0, 0, 0, addr));
	bpf_gen__emit(gen, BPF_MOV64_IMM(BPF_REG_2, len));
	if (reg1 >= 0)
		bpf_gen__emit(gen, BPF_MOV64_REG(BPF_REG_3, reg1));
	if (reg2 >= 0)
		bpf_gen__emit(gen, BPF_MOV64_REG(BPF_REG_4, reg2));
	bpf_gen__emit(gen, BPF_EMIT_CALL(BPF_FUNC_trace_printk));
}

static void bpf_gen__debug_regs(struct bpf_gen *gen, int reg1, int reg2, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	__bpf_gen__debug(gen, reg1, reg2, fmt, args);
	va_end(args);
}

static void bpf_gen__debug_ret(struct bpf_gen *gen, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	__bpf_gen__debug(gen, BPF_REG_7, -1, fmt, args);
	va_end(args);
}

static void bpf_gen__emit_sys_close(struct bpf_gen *gen, int stack_off)
{
	bpf_gen__emit(gen, BPF_LDX_MEM(BPF_W, BPF_REG_1, BPF_REG_10, stack_off));
	bpf_gen__emit(gen, BPF_JMP_IMM(BPF_JSLE, BPF_REG_1, 0, 2 + (gen->log_level ? 6 : 0)));
	bpf_gen__emit(gen, BPF_MOV64_REG(BPF_REG_9, BPF_REG_1));
	bpf_gen__emit(gen, BPF_EMIT_CALL(BPF_FUNC_sys_close));
	bpf_gen__debug_regs(gen, BPF_REG_9, BPF_REG_0, "close(%%d) = %%d");
}

int bpf_gen__finish(struct bpf_gen *gen)
{
	int i;

	bpf_gen__emit_sys_close(gen, stack_off(btf_fd));
	for (i = 0; i < gen->nr_progs; i++)
		bpf_gen__move_stack2ctx(gen, offsetof(struct bpf_loader_ctx,
						      u[gen->nr_maps + i].map_fd), 4,
					stack_off(prog_fd[i]));
	for (i = 0; i < gen->nr_maps; i++)
		bpf_gen__move_stack2ctx(gen, offsetof(struct bpf_loader_ctx,
						      u[i].prog_fd), 4,
					stack_off(map_fd[i]));
	bpf_gen__emit(gen, BPF_MOV64_IMM(BPF_REG_0, 0));
	bpf_gen__emit(gen, BPF_EXIT_INSN());
	pr_debug("bpf_gen__finish %d\n", gen->error);
	return gen->error;
}

void bpf_gen__load_btf(struct bpf_gen *gen, const void *btf_raw_data, __u32 btf_raw_size)
{
	union bpf_attr attr = {};
	int attr_size = offsetofend(union bpf_attr, btf_log_level);
	int btf_data, btf_load_attr;

	pr_debug("btf_load: size %d\n", btf_raw_size);
	btf_data = bpf_gen__add_data(gen, btf_raw_data, btf_raw_size);

	attr.btf_size = btf_raw_size;
	btf_load_attr = bpf_gen__add_data(gen, &attr, attr_size);

	/* populate union bpf_attr with user provided log details */
	bpf_gen__move_ctx2blob(gen, btf_load_attr + offsetof(union bpf_attr, btf_log_level), 4,
			       offsetof(struct bpf_loader_ctx, log_level));
	bpf_gen__move_ctx2blob(gen, btf_load_attr + offsetof(union bpf_attr, btf_log_size), 4,
			       offsetof(struct bpf_loader_ctx, log_size));
	bpf_gen__move_ctx2blob(gen, btf_load_attr + offsetof(union bpf_attr, btf_log_buf), 8,
			       offsetof(struct bpf_loader_ctx, log_buf));
	/* populate union bpf_attr with a pointer to the BTF data */
	bpf_gen__emit_rel_store(gen, btf_load_attr + offsetof(union bpf_attr, btf), btf_data);
	/* emit BTF_LOAD command */
	bpf_gen__emit_sys_bpf(gen, BPF_BTF_LOAD, btf_load_attr, attr_size);
	bpf_gen__debug_ret(gen, "btf_load size %d", btf_raw_size);
	bpf_gen__emit_check_err(gen);
	/* remember btf_fd in the stack, if successful */
	bpf_gen__emit(gen, BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_7, stack_off(btf_fd)));
}

void bpf_gen__map_create(struct bpf_gen *gen, struct bpf_create_map_attr *map_attr, int map_idx)
{
	union bpf_attr attr = {};
	int attr_size = offsetofend(union bpf_attr, btf_vmlinux_value_type_id);
	bool close_inner_map_fd = false;
	int map_create_attr;

	attr.map_type = map_attr->map_type;
	attr.key_size = map_attr->key_size;
	attr.value_size = map_attr->value_size;
	attr.map_flags = map_attr->map_flags;
	memcpy(attr.map_name, map_attr->name,
	       min((unsigned)strlen(map_attr->name), BPF_OBJ_NAME_LEN - 1));
	attr.numa_node = map_attr->numa_node;
	attr.map_ifindex = map_attr->map_ifindex;
	attr.max_entries = map_attr->max_entries;
	switch (attr.map_type) {
	case BPF_MAP_TYPE_PERF_EVENT_ARRAY:
	case BPF_MAP_TYPE_CGROUP_ARRAY:
	case BPF_MAP_TYPE_STACK_TRACE:
	case BPF_MAP_TYPE_ARRAY_OF_MAPS:
	case BPF_MAP_TYPE_HASH_OF_MAPS:
	case BPF_MAP_TYPE_DEVMAP:
	case BPF_MAP_TYPE_DEVMAP_HASH:
	case BPF_MAP_TYPE_CPUMAP:
	case BPF_MAP_TYPE_XSKMAP:
	case BPF_MAP_TYPE_SOCKMAP:
	case BPF_MAP_TYPE_SOCKHASH:
	case BPF_MAP_TYPE_QUEUE:
	case BPF_MAP_TYPE_STACK:
	case BPF_MAP_TYPE_RINGBUF:
		break;
	default:
		attr.btf_key_type_id = map_attr->btf_key_type_id;
		attr.btf_value_type_id = map_attr->btf_value_type_id;
	}

	pr_debug("map_create: %s idx %d type %d value_type_id %d\n",
		 attr.map_name, map_idx, map_attr->map_type, attr.btf_value_type_id);

	map_create_attr = bpf_gen__add_data(gen, &attr, attr_size);
	if (attr.btf_value_type_id)
		/* populate union bpf_attr with btf_fd saved in the stack earlier */
		bpf_gen__move_stack2blob(gen, map_create_attr + offsetof(union bpf_attr, btf_fd), 4,
					 stack_off(btf_fd));
	switch (attr.map_type) {
	case BPF_MAP_TYPE_ARRAY_OF_MAPS:
	case BPF_MAP_TYPE_HASH_OF_MAPS:
		bpf_gen__move_stack2blob(gen, map_create_attr + offsetof(union bpf_attr, inner_map_fd),
					 4, stack_off(inner_map_fd));
		close_inner_map_fd = true;
		break;
	default:;
	}
	/* emit MAP_CREATE command */
	bpf_gen__emit_sys_bpf(gen, BPF_MAP_CREATE, map_create_attr, attr_size);
	bpf_gen__debug_ret(gen, "map_create %s idx %d type %d value_size %d",
			   attr.map_name, map_idx, map_attr->map_type, attr.value_size);
	bpf_gen__emit_check_err(gen);
	/* remember map_fd in the stack, if successful */
	if (map_idx < 0) {
		bpf_gen__emit(gen, BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_7, stack_off(inner_map_fd)));
	} else {
		if (map_idx != gen->nr_maps) {
			gen->error = -EDOM; /* internal bug */
			return;
		}
		bpf_gen__emit(gen, BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_7, stack_off(map_fd[map_idx])));
		gen->nr_maps++;
	}
	if (close_inner_map_fd)
		bpf_gen__emit_sys_close(gen, stack_off(inner_map_fd));
}

void bpf_gen__record_find_name(struct bpf_gen *gen, const char *attach_name,
			       enum bpf_attach_type type)
{
	const char *prefix;
	int kind, len, name;

	btf_get_kernel_prefix_kind(type, &prefix, &kind);
	pr_debug("find_btf_id '%s%s'\n", prefix, attach_name);
	len = strlen(prefix);
	if (len)
		name = bpf_gen__add_data(gen, prefix, len);
	name = bpf_gen__add_data(gen, attach_name, strlen(attach_name) + 1);
	name -= len;

	bpf_gen__emit(gen, BPF_MOV64_IMM(BPF_REG_1, 0));
	bpf_gen__emit2(gen, BPF_LD_IMM64_RAW_FULL(BPF_REG_2, BPF_PSEUDO_MAP_IDX_VALUE, 0, 0, 0, name));
	bpf_gen__emit(gen, BPF_MOV64_IMM(BPF_REG_3, kind));
	bpf_gen__emit(gen, BPF_MOV64_REG(BPF_REG_4, BPF_REG_10));
	bpf_gen__emit(gen, BPF_ALU64_IMM(BPF_ADD, BPF_REG_4, stack_off(last_attach_btf_obj_fd)));
	bpf_gen__emit(gen, BPF_MOV64_IMM(BPF_REG_5, 0));
	bpf_gen__emit(gen, BPF_EMIT_CALL(BPF_FUNC_btf_find_by_name_kind));
	bpf_gen__emit(gen, BPF_MOV64_REG(BPF_REG_7, BPF_REG_0));
	bpf_gen__debug_ret(gen, "find_by_name_kind(%s%s,%d)", prefix, attach_name, kind);
	bpf_gen__emit_check_err(gen);
	/* remember btf_id */
	bpf_gen__emit(gen, BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_7, stack_off(last_btf_id)));
}

void bpf_gen__record_extern(struct bpf_gen *gen, const char *name, int kind, int insn_idx)
{
	struct relo_desc *relo;

	relo = libbpf_reallocarray(gen->relos, gen->relo_cnt + 1, sizeof(*relo));
	if (!relo) {
		gen->error = -ENOMEM;
		return;
	}
	gen->relos = relo;
	relo += gen->relo_cnt;
	relo->name = name;
	relo->kind = kind;
	relo->insn_idx = insn_idx;
	gen->relo_cnt++;
}

static void bpf_gen__emit_relo(struct bpf_gen *gen, struct relo_desc *relo, int insns)
{
	int name, insn;

	pr_debug("relo: %s at %d\n", relo->name, relo->insn_idx);
	name = bpf_gen__add_data(gen, relo->name, strlen(relo->name) + 1);

	bpf_gen__emit(gen, BPF_MOV64_IMM(BPF_REG_1, 0));
	bpf_gen__emit2(gen, BPF_LD_IMM64_RAW_FULL(BPF_REG_2, BPF_PSEUDO_MAP_IDX_VALUE, 0, 0, 0, name));
	bpf_gen__emit(gen, BPF_MOV64_IMM(BPF_REG_3, relo->kind));
	bpf_gen__emit(gen, BPF_MOV64_REG(BPF_REG_4, BPF_REG_10));
	bpf_gen__emit(gen, BPF_ALU64_IMM(BPF_ADD, BPF_REG_4, stack_off(last_attach_btf_obj_fd)));
	bpf_gen__emit(gen, BPF_MOV64_IMM(BPF_REG_5, 0));
	bpf_gen__emit(gen, BPF_EMIT_CALL(BPF_FUNC_btf_find_by_name_kind));
	bpf_gen__emit(gen, BPF_MOV64_REG(BPF_REG_7, BPF_REG_0));
	bpf_gen__debug_ret(gen, "find_by_name_kind(%s,%d)", relo->name, relo->kind);
	bpf_gen__emit_check_err(gen);
	/* store btf_id into insn[insn_idx].imm */
	insn = (int)(long)&((struct bpf_insn *)(long)insns)[relo->insn_idx].imm;
	bpf_gen__emit2(gen, BPF_LD_IMM64_RAW_FULL(BPF_REG_0, BPF_PSEUDO_MAP_IDX_VALUE, 0, 0, 0, insn));
	bpf_gen__emit(gen, BPF_STX_MEM(BPF_W, BPF_REG_0, BPF_REG_7, 0));
}

void bpf_gen__prog_load(struct bpf_gen *gen, struct bpf_prog_load_params *load_attr, int prog_idx)
{
	union bpf_attr attr = {};
	int attr_size = offsetofend(union bpf_attr, fd_array);
	int prog_load_attr, license, insns, func_info, line_info, i;

	pr_debug("prog_load: type %d insns_cnt %zd\n",
		 load_attr->prog_type, load_attr->insn_cnt);
	/* add license string to blob of bytes */
	license = bpf_gen__add_data(gen, load_attr->license, strlen(load_attr->license) + 1);
	/* add insns to blob of bytes */
	insns = bpf_gen__add_data(gen, load_attr->insns,
				  load_attr->insn_cnt * sizeof(struct bpf_insn));

	attr.prog_type = load_attr->prog_type;
	attr.expected_attach_type = load_attr->expected_attach_type;
	attr.attach_btf_id = load_attr->attach_btf_id;
	attr.prog_ifindex = load_attr->prog_ifindex;
	attr.kern_version = 0;
	attr.insn_cnt = (__u32)load_attr->insn_cnt;
	attr.prog_flags = load_attr->prog_flags;

	attr.func_info_rec_size = load_attr->func_info_rec_size;
	attr.func_info_cnt = load_attr->func_info_cnt;
	func_info = bpf_gen__add_data(gen, load_attr->func_info,
				      attr.func_info_cnt * attr.func_info_rec_size);

	attr.line_info_rec_size = load_attr->line_info_rec_size;
	attr.line_info_cnt = load_attr->line_info_cnt;
	line_info = bpf_gen__add_data(gen, load_attr->line_info,
				      attr.line_info_cnt * attr.line_info_rec_size);

	memcpy(attr.prog_name, load_attr->name,
	       min((unsigned)strlen(load_attr->name), BPF_OBJ_NAME_LEN - 1));
	prog_load_attr = bpf_gen__add_data(gen, &attr, attr_size);

	/* populate union bpf_attr with a pointer to license */
	bpf_gen__emit_rel_store(gen, prog_load_attr + offsetof(union bpf_attr, license), license);

	/* populate union bpf_attr with a pointer to instructions */
	bpf_gen__emit_rel_store(gen, prog_load_attr + offsetof(union bpf_attr, insns), insns);

	/* populate union bpf_attr with a pointer to func_info */
	bpf_gen__emit_rel_store(gen, prog_load_attr + offsetof(union bpf_attr, func_info), func_info);

	/* populate union bpf_attr with a pointer to line_info */
	bpf_gen__emit_rel_store(gen, prog_load_attr + offsetof(union bpf_attr, line_info), line_info);

	/* populate union bpf_attr fd_array with a pointer to stack where map_fds are saved */
	bpf_gen__emit_rel_store_sp(gen, prog_load_attr + offsetof(union bpf_attr, fd_array),
				   stack_off(map_fd[0]));

	/* populate union bpf_attr with user provided log details */
	bpf_gen__move_ctx2blob(gen, prog_load_attr + offsetof(union bpf_attr, log_level), 4,
			       offsetof(struct bpf_loader_ctx, log_level));
	bpf_gen__move_ctx2blob(gen, prog_load_attr + offsetof(union bpf_attr, log_size), 4,
			       offsetof(struct bpf_loader_ctx, log_size));
	bpf_gen__move_ctx2blob(gen, prog_load_attr + offsetof(union bpf_attr, log_buf), 8,
			       offsetof(struct bpf_loader_ctx, log_buf));
	/* populate union bpf_attr with btf_fd saved in the stack earlier */
	bpf_gen__move_stack2blob(gen, prog_load_attr + offsetof(union bpf_attr, prog_btf_fd), 4,
				 stack_off(btf_fd));
	if (attr.attach_btf_id) {
		/* populate union bpf_attr with btf_id and obj_fd found by helper */
		bpf_gen__move_stack2blob(gen, prog_load_attr + offsetof(union bpf_attr, attach_btf_id), 4,
					 stack_off(last_btf_id));
		bpf_gen__move_stack2blob(gen, prog_load_attr + offsetof(union bpf_attr, attach_btf_obj_fd), 4,
					 stack_off(last_attach_btf_obj_fd));
	}
	for (i = 0; i < gen->relo_cnt; i++)
		bpf_gen__emit_relo(gen, gen->relos + i, insns);
	if (gen->relo_cnt) {
		free(gen->relos);
		gen->relo_cnt = 0;
		gen->relos = NULL;
	}
	/* emit PROG_LOAD command */
	bpf_gen__emit_sys_bpf(gen, BPF_PROG_LOAD, prog_load_attr, attr_size);
	bpf_gen__debug_ret(gen, "prog_load %s insn_cnt %d", attr.prog_name, attr.insn_cnt);
	bpf_gen__emit_check_err(gen);
	/* remember prog_fd in the stack, if successful */
	bpf_gen__emit(gen, BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_7, stack_off(prog_fd[gen->nr_progs])));
	if (attr.attach_btf_id)
		bpf_gen__emit_sys_close(gen, stack_off(last_attach_btf_obj_fd));
	gen->nr_progs++;
}

void bpf_gen__map_update_elem(struct bpf_gen *gen, int map_idx, void *pvalue, __u32 value_size)
{
	union bpf_attr attr = {};
	int attr_size = offsetofend(union bpf_attr, flags);
	int map_update_attr, value, key;
	int zero = 0;

	pr_debug("map_update_elem: idx %d\n", map_idx);
	value = bpf_gen__add_data(gen, pvalue, value_size);
	key = bpf_gen__add_data(gen, &zero, sizeof(zero));
	map_update_attr = bpf_gen__add_data(gen, &attr, attr_size);
	bpf_gen__move_stack2blob(gen, map_update_attr + offsetof(union bpf_attr, map_fd), 4,
				 stack_off(map_fd[map_idx]));
	bpf_gen__emit_rel_store(gen, map_update_attr + offsetof(union bpf_attr, key), key);
	bpf_gen__emit_rel_store(gen, map_update_attr + offsetof(union bpf_attr, value), value);
	/* emit MAP_UPDATE_ELEM command */
	bpf_gen__emit_sys_bpf(gen, BPF_MAP_UPDATE_ELEM, map_update_attr, attr_size);
	bpf_gen__debug_ret(gen, "update_elem idx %d value_size %d", map_idx, value_size);
	bpf_gen__emit_check_err(gen);
}

void bpf_gen__map_freeze(struct bpf_gen *gen, int map_idx)
{
	union bpf_attr attr = {};
	int attr_size = offsetofend(union bpf_attr, map_fd);
	int map_freeze_attr;

	pr_debug("map_freeze: idx %d\n", map_idx);
	map_freeze_attr = bpf_gen__add_data(gen, &attr, attr_size);
	bpf_gen__move_stack2blob(gen, map_freeze_attr + offsetof(union bpf_attr, map_fd), 4,
				 stack_off(map_fd[map_idx]));
	/* emit MAP_FREEZE command */
	bpf_gen__emit_sys_bpf(gen, BPF_MAP_FREEZE, map_freeze_attr, attr_size);
	bpf_gen__debug_ret(gen, "map_freeze");
	bpf_gen__emit_check_err(gen);
}
