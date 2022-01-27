#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Tests the aarch64 instruction generation infrastructure using test_insn
# kernel module.
$(dirname $0)/../../kselftest/module.sh "insn" test_insn
