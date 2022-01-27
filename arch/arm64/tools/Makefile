# SPDX-License-Identifier: GPL-2.0

gen := arch/$(ARCH)/include/generated
kapi := $(gen)/asm

kapi-hdrs-y := $(kapi)/cpucaps.h
tests-hdrs-y := $(kapi)/test_logic_imm_generated.h

targets += $(addprefix ../../../,$(gen-y) $(kapi-hdrs-y))

PHONY += kapi tests

kapi:   $(kapi-hdrs-y) $(gen-y)

# Create output directory if not already present
_dummy := $(shell [ -d '$(kapi)' ] || mkdir -p '$(kapi)')

quiet_cmd_gen_cpucaps = GEN     $@
      cmd_gen_cpucaps = mkdir -p $(dir $@) && \
                     $(AWK) -f $(filter-out $(PHONY),$^) > $@

$(kapi)/cpucaps.h: $(src)/gen-cpucaps.awk $(src)/cpucaps FORCE
	$(call if_changed,gen_cpucaps)

tests:	$(tests-hdrs-y) $(gen-y)

quiet_cmd_build_gen_logic_imm = GEN     $@
      cmd_build_gen_logic_imm = $(obj)/gen_logic_imm > $@

hostprogs := gen_logic_imm
$(kapi)/test_logic_imm_generated.h: $(obj)/gen_logic_imm FORCE
	$(call if_changed,build_gen_logic_imm)
