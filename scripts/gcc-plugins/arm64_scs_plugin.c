// SPDX-License-Identifier: GPL-2.0
#include "gcc-common.h"

#define v_info(fmt, ...)							\
	do {									\
		if (verbose)							\
			fprintf(stderr, "[SCS]:" fmt,  ## __VA_ARGS__);	\
	} while (0)

#define NOSCS_ATTR_STR  "no_shadow_call_stack"
#define SCS_ASM_PUSH_STR "str x30, [x18], #8\n\t"
#define SCS_ASM_POP_STR  "ldr x30, [x18, #-8]!\n\t"

__visible int plugin_is_GPL_compatible;

static struct plugin_info arm64_scs_plugin_info = {
	.version	= "20210926vanilla",
	.help		= "enable\tactivate plugin\n"
			  "verbose\tprint all debug infos\n",
};

static bool verbose;

#if BUILDING_GCC_VERSION >= 10001
enum insn_code paciasp_num = CODE_FOR_paciasp;
enum insn_code autiasp_num = CODE_FOR_autiasp;
#elif BUILDING_GCC_VERSION >= 7003
enum insn_code paciasp_num = CODE_FOR_pacisp;
enum insn_code autiasp_num = CODE_FOR_autisp;
#else
enum insn_code paciasp_num = CODE_FOR_nothing;
enum insn_code autiasp_num = CODE_FOR_nothing;
#define TARGET_ARMV8_3 0
#endif

static rtx_insn * (*old_gen_prologue)(void);
static rtx_insn * (*old_gen_epilogue)(void);
static rtx_insn * (*old_gen_sibcall_epilogue)(void);

static rtx gen_scs_push(location_t loc)
{
	rtx insn = gen_rtx_ASM_INPUT_loc(VOIDmode, ggc_strdup(SCS_ASM_PUSH_STR), loc);

	MEM_VOLATILE_P(insn) = 1;
	return insn;
}

static rtx gen_scs_pop(location_t loc)
{
	rtx insn = gen_rtx_ASM_INPUT_loc(VOIDmode, ggc_strdup(SCS_ASM_POP_STR), loc);

	MEM_VOLATILE_P(insn) = 1;
	return insn;
}

static bool scs_func_ignored(void)
{
	bool is_ignored;

#if BUILDING_GCC_VERSION >= 8002
	is_ignored = !cfun->machine->frame.emit_frame_chain;
#else
	is_ignored = !frame_pointer_needed;
#endif

	/*
	 * Functions that do not push LR into stack are not protected.
	 * Functions that call __builin_eh_return is not protected(consistent with gcc's PAC).
	 */
	if (is_ignored || crtl->calls_eh_return) {
		v_info("No protection code inserted into func:%s in file:%s\n",
			get_name(current_function_decl), main_input_filename);
		return 1;
	}

	/* Functions with attribute NOSCS_ATTR_STR need to be unprotected */
	if (lookup_attribute(NOSCS_ATTR_STR, DECL_ATTRIBUTES(current_function_decl))) {
		v_info("No protection code inserted into %s func:%s in file:%s\n", NOSCS_ATTR_STR,
				get_name(current_function_decl), main_input_filename);
		return 1;
	}

	return 0;
}

static rtx_insn *search_insn(enum insn_code code, rtx_insn *seq)
{
	rtx_insn *insn;

	for (insn = get_insns(); insn; insn = NEXT_INSN(insn)) {
		if (code == recog(PATTERN(insn), insn, 0))
			return insn;
	}

	return NULL;
}

static bool scs_return_address_signing_enabled(void)
{
#if BUILDING_GCC_VERSION >= 7003
	return aarch64_return_address_signing_enabled();
#else
	return false;
#endif
}

static rtx_insn *scs_gen_prologue(void)
{
	rtx_insn *seq = NULL, *mark;
	rtx tmp;
	bool ret_sign_enabled;

	if (old_gen_prologue)
		seq = old_gen_prologue();

	if ((!seq) || scs_func_ignored())
		return seq;

	ret_sign_enabled = scs_return_address_signing_enabled();
	tmp = gen_scs_push(RESERVED_LOCATION_COUNT);

	start_sequence();
	emit_insn(seq);

	if (ret_sign_enabled) {
		/* For functions with pac enabled, insert scs push after the 'paciasp' insn */
		mark = search_insn(paciasp_num, get_insns());
		if (!mark)
			error(G_("Non-standard insn seqs found:\n"
				"__noscs attr should be added on func:%s,file:%s\n"),
				get_name(current_function_decl), main_input_filename);

		emit_insn_after(tmp, mark);
	} else {
		/* For functions that do not enable pac, insert scs push at the start of insns */
		mark = get_insns();
		emit_insn_before(tmp, mark);
	}

	seq = get_insns();
	end_sequence();
	return seq;
}

static rtx_insn *scs_gen_epilogue(void)
{
	rtx_insn *seq = NULL, *mark;
	rtx tmp;
	bool ret_sign_enabled;

	if (old_gen_epilogue)
		seq = old_gen_epilogue();

	if ((!seq) || scs_func_ignored())
		return seq;

	ret_sign_enabled = scs_return_address_signing_enabled();
	tmp = gen_scs_pop(RESERVED_LOCATION_COUNT);

	start_sequence();
	emit_insn(seq);

	if (ret_sign_enabled && (!TARGET_ARMV8_3)) {
		/* For functions with pac enabled, if 'autiasp' is used in epilogue
		 * (!TARGET_ARMV8_3), scs pop should inserted before this insn.
		 */
		mark = search_insn(autiasp_num, get_insns());
	} else {
		/* For functions do not enabled pac or used 'retaa' as pac check,
		 * scs pop inserted before the last 'return" insn
		 */
		mark = get_last_insn();
	}

	if (!mark)
		error(G_("Non-standard insn seqs found:\n"
			"__noscs attr should be added on func:%s,file:%s\n"),
			get_name(current_function_decl), main_input_filename);

	emit_insn_before(tmp, mark);

	seq = get_insns();
	end_sequence();
	return seq;
}

static rtx_insn *scs_gen_sibcall_epilogue(void)
{
	rtx_insn *seq = NULL, *mark;
	rtx tmp;
	bool ret_sign_enabled;

	if (old_gen_sibcall_epilogue)
		seq = old_gen_sibcall_epilogue();

	if ((!seq) || scs_func_ignored())
		return seq;

	ret_sign_enabled = scs_return_address_signing_enabled();
	tmp = gen_scs_pop(RESERVED_LOCATION_COUNT);

	start_sequence();
	emit_insn(seq);

	if (ret_sign_enabled) {
		/* If pac is enabled, sibling_call will always use 'autiasp' as pac check */
		mark = search_insn(autiasp_num, get_insns());
		if (!mark)
			error(G_("Non-standard insn seqs found:\n"
				"__noscs attr should be added on func:%s,file:%s\n"),
				get_name(current_function_decl), main_input_filename);
		emit_insn_before(tmp, mark);
	} else {
		/* If pac is disabled, insert scs pop at the end of insns */
		mark = get_last_insn();
		emit_insn_after(tmp, mark);
	}

	seq = get_insns();
	end_sequence();

	return seq;
}

static void callback_before_start_unit(void *gcc_data __unused, void *user_data __unused)
{
	old_gen_prologue = targetm.gen_prologue;
	old_gen_epilogue = targetm.gen_epilogue;
	old_gen_sibcall_epilogue = targetm.gen_sibcall_epilogue;

	targetm.gen_prologue = scs_gen_prologue;
	targetm.gen_epilogue = scs_gen_epilogue;
	targetm.gen_sibcall_epilogue = scs_gen_sibcall_epilogue;
}

static tree handle_noscs_attribute(tree *node, tree name, tree args __unused, int flags,
		bool *no_add_attrs)
{
	/* NOSCS_ATTR_STR can only be used for function declarations */
	switch (TREE_CODE(*node)) {
	case FUNCTION_DECL:
		break;
	default:
		error(G_("%qE attribute can be applies to function decl only (%qE)"), name, *node);
		gcc_unreachable();
	}

	*no_add_attrs = false;
	return NULL_TREE;
}

static struct attribute_spec noscs_attr = {};

static void scs_register_attributes(void *event_data __unused, void *data __unused)
{
	noscs_attr.name	= NOSCS_ATTR_STR;
	noscs_attr.decl_required = true;
	noscs_attr.handler = handle_noscs_attribute;
	register_attribute(&noscs_attr);
}

__visible int plugin_init(struct plugin_name_args *plugin_info, struct plugin_gcc_version *version)
{
	int i;
	bool enable = false;
	const char * const plugin_name = plugin_info->base_name;
	const int argc = plugin_info->argc;
	const struct plugin_argument * const argv = plugin_info->argv;

	if (!plugin_default_version_check(version, &gcc_version)) {
		error(G_("Incompatible gcc/plugin versions"));
		return 1;
	}

	for (i = 0; i < argc; ++i) {
		if (!strcmp(argv[i].key, "enable")) {
			enable = true;
			continue;
		}
		if (!strcmp(argv[i].key, "verbose")) {
			verbose = true;
			continue;
		}
		error(G_("unknown option '-fplugin-arg-%s-%s'"), plugin_name, argv[i].key);
	}

	if (!enable) {
		v_info("Plugin disabled for file:%s\n", main_input_filename);
		return 0;
	}

	register_callback(plugin_name, PLUGIN_INFO, NULL, &arm64_scs_plugin_info);

	register_callback(plugin_name, PLUGIN_ATTRIBUTES, scs_register_attributes, NULL);

	register_callback(plugin_name, PLUGIN_START_UNIT, callback_before_start_unit, NULL);

	return 0;
}
