// SPDX-License-Identifier: GPL-2.0
#include "gcc-common.h"

#define v_info(fmt, ...)							\
	do {									\
		if (verbose)							\
			fprintf(stderr, "[SCS]: " fmt,  ## __VA_ARGS__);	\
	} while (0)

#define NOSCS_ATTR_STR  "no_shadow_call_stack"
#define SCS_ASM_PUSH_STR "str x30, [x18], #8\n\t"
#define SCS_ASM_POP_STR  "ldr x30, [x18, #-8]!\n\t"

__visible int plugin_is_GPL_compatible;

static struct plugin_info arm64_scs_plugin_info = {
	.version	= "20210926vanilla",
	.help		= "disable\tdo not activate plugin\n"
			  "verbose\tprint all debug infos\n",
};

static bool verbose;

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

static bool arm64_scs_gate(void)
{
	bool is_ignored;

#if BUILDING_GCC_VERSION >= 8002
	is_ignored = !cfun->machine->frame.emit_frame_chain;
#else
	is_ignored = !frame_pointer_needed;
#endif

	/* No need to insert protection code into functions that do not push LR into stack */
	if (is_ignored) {
		v_info("No protection code inserted into func:%s in file:%s\n",
			get_name(current_function_decl), main_input_filename);
		return 0;
	}

	gcc_assert(cfun->machine->frame.wb_candidate2 == R30_REGNUM);

	/* Don't insert protection code into functions with NOSCS_ATTR_STR attribute */
	if (lookup_attribute(NOSCS_ATTR_STR, DECL_ATTRIBUTES(current_function_decl))) {
		v_info("No protection code inserted into %s func:%s in file:%s\n", NOSCS_ATTR_STR,
				get_name(current_function_decl), main_input_filename);
		return 0;
	}
	return 1;
}

enum scs_state {
	/* The first valid instruction has not been found in the current instruction sequence */
	SCS_SEARCHING_FIRST_INSN,
	/* Currently searching for the return rtx instruction in this function */
	SCS_SEARCHING_FUNC_RETURN,
	/* Found an EPILOGUE_BEGIN before the function return instruction */
	SCS_FOUND_ONE_EPILOGUE_NOTE,
};

static unsigned int arm64_scs_execute(void)
{
	rtx_insn *insn;
	enum scs_state state = SCS_SEARCHING_FIRST_INSN;

	for (insn = get_insns(); insn; insn = NEXT_INSN(insn)) {
		rtx mark = NULL;

		switch (GET_CODE(insn)) {
		case NOTE:
		case BARRIER:
		case CODE_LABEL:
		case INSN:
		case DEBUG_INSN:
		case JUMP_INSN:
		case JUMP_TABLE_DATA:
			break;
		case CALL_INSN:
			if (SIBLING_CALL_P(insn)) {
				error(G_("Sibling call found in func:%s, file:%s\n"),
						get_name(current_function_decl),
						main_input_filename);
				gcc_unreachable();
			}
			break;
		default:
			error(G_("Invalid rtx_insn seqs found with type:%s in func:%s, file:%s\n"),
					GET_RTX_NAME(GET_CODE(insn)),
					get_name(current_function_decl), main_input_filename);
			gcc_unreachable();
			break;
		}

		if (state == SCS_SEARCHING_FIRST_INSN) {
			/* A function that needs to be instrumented should not found epilogue
			 * before its first insn
			 */
			gcc_assert(!(NOTE_P(insn) && (NOTE_KIND(insn) == NOTE_INSN_EPILOGUE_BEG)));

			if (NOTE_P(insn) || INSN_DELETED_P(insn))
				continue;

			state = SCS_SEARCHING_FUNC_RETURN;

			/* Insert scs pop before the first instruction found */
			mark = gen_scs_push(RESERVED_LOCATION_COUNT);
			emit_insn_before(mark, insn);
		}

		/* Find the corresponding epilogue before 'RETURN' instruction (if any) */
		if (state == SCS_SEARCHING_FUNC_RETURN) {
			if (NOTE_P(insn) && (NOTE_KIND(insn) == NOTE_INSN_EPILOGUE_BEG)) {
				state = SCS_FOUND_ONE_EPILOGUE_NOTE;
				continue;
			}
		}

		if (!JUMP_P(insn))
			continue;

		/* A function return insn was found */
		if (ANY_RETURN_P(PATTERN(insn))) {
			/* There should be an epilogue before 'RETURN' inst */
			if (GET_CODE(PATTERN(insn)) == RETURN) {
				gcc_assert(state == SCS_FOUND_ONE_EPILOGUE_NOTE);
				state = SCS_SEARCHING_FUNC_RETURN;
			}

			/* There is no epilogue before 'SIMPLE_RETURN' insn */
			if (GET_CODE(PATTERN(insn)) == SIMPLE_RETURN)
				gcc_assert(state == SCS_SEARCHING_FUNC_RETURN);

			/* Insert scs pop instruction(s) before return insn */
			mark = gen_scs_pop(RESERVED_LOCATION_COUNT);
			emit_insn_before(mark, insn);
		}
	}
	return 0;
}

static tree handle_noscs_attribute(tree *node, tree name, tree args __unused, int flags,
		bool *no_add_attrs)
{
	*no_add_attrs = true;

	gcc_assert(DECL_P(*node));
	switch (TREE_CODE(*node)) {
	default:
		error(G_("%qE attribute can be applies to function decl only (%qE)"), name, *node);
		gcc_unreachable();

	case FUNCTION_DECL:	/* the attribute is only used for function declarations */
		break;
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

static void (*old_override_options_after_change)(void);

static void scs_override_options_after_change(void)
{
	if (old_override_options_after_change)
		old_override_options_after_change();

	flag_optimize_sibling_calls = 0;
}

static void callback_before_start_unit(void *gcc_data __unused, void *user_data __unused)
{
	/* Turn off sibling call to avoid inserting duplicate scs pop codes */
	old_override_options_after_change = targetm.override_options_after_change;
	targetm.override_options_after_change = scs_override_options_after_change;

	flag_optimize_sibling_calls = 0;
}

#define PASS_NAME arm64_scs
#define TODO_FLAGS_FINISH (TODO_dump_func | TODO_verify_rtl_sharing)
#include "gcc-generate-rtl-pass.h"

__visible int plugin_init(struct plugin_name_args *plugin_info, struct plugin_gcc_version *version)
{
	int i;
	const char * const plugin_name = plugin_info->base_name;
	const int argc = plugin_info->argc;
	const struct plugin_argument * const argv = plugin_info->argv;
	bool enable = true;

	PASS_INFO(arm64_scs, "shorten", 1, PASS_POS_INSERT_BEFORE);

	if (!plugin_default_version_check(version, &gcc_version)) {
		error(G_("Incompatible gcc/plugin versions"));
		return 1;
	}

	if (strncmp(lang_hooks.name, "GNU C", 5) && !strncmp(lang_hooks.name, "GNU C+", 6)) {
		inform(UNKNOWN_LOCATION, G_("%s supports C only, not %s"), plugin_name,
				lang_hooks.name);
		enable = false;
	}

	for (i = 0; i < argc; ++i) {
		if (!strcmp(argv[i].key, "disable")) {
			enable = false;
			continue;
		}
		if (!strcmp(argv[i].key, "verbose")) {
			verbose = true;
			continue;
		}
		error(G_("unknown option '-fplugin-arg-%s-%s'"), plugin_name, argv[i].key);
	}

	register_callback(plugin_name, PLUGIN_INFO, NULL, &arm64_scs_plugin_info);

	register_callback(plugin_name, PLUGIN_ATTRIBUTES, scs_register_attributes, NULL);

	if (!enable) {
		v_info("Plugin disabled for file:%s\n", main_input_filename);
		return 0;
	}

	register_callback(plugin_name, PLUGIN_START_UNIT, callback_before_start_unit, NULL);

	register_callback(plugin_name, PLUGIN_PASS_MANAGER_SETUP, NULL, &arm64_scs_pass_info);

	return 0;
}
