// SPDX-License-Identifier: GPL-2.0-only
/*
 * KUnit tests for arch/arm64/kvm/sys_regs.c.
 */

#include <linux/module.h>
#include <kunit/test.h>
#include <kunit/test.h>
#include <linux/kvm_host.h>
#include <asm/cpufeature.h>
#include "asm/sysreg.h"

/* Some utilities for minimal vcpu/kvm setup for existing testings. */
static struct kvm_vcpu *test_vcpu_init(struct kunit *test, u32 id,
				       struct kvm *kvm)
{
	struct kvm_vcpu *vcpu;

	vcpu = kunit_kzalloc(test, sizeof(*vcpu), GFP_KERNEL);
	if (!vcpu)
		return NULL;

	vcpu->cpu = -1;
	vcpu->kvm = kvm;
	vcpu->vcpu_id = id;

	return vcpu;
}

static void test_vcpu_fini(struct kunit *test, struct kvm_vcpu *vcpu)
{
	kunit_kfree(test, vcpu);
}

static struct kvm *test_kvm_init(struct kunit *test)
{
	struct kvm *kvm;

	kvm = kunit_kzalloc(test, sizeof(struct kvm), GFP_KERNEL);
	if (!kvm)
		return NULL;

	return kvm;
}

static void test_kvm_fini(struct kunit *test, struct kvm *kvm)
{
	kunit_kfree(test, kvm);
}

static struct kvm_vcpu *test_kvm_vcpu_init(struct kunit *test)
{
	struct kvm_vcpu *vcpu;
	struct kvm *kvm;

	kvm = test_kvm_init(test);
	if (!kvm)
		return NULL;

	vcpu = test_vcpu_init(test, 0, kvm);
	if (!vcpu) {
		test_kvm_fini(test, kvm);
		return NULL;
	}
	return vcpu;
}

static void test_kvm_vcpu_fini(struct kunit *test, struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;

	test_vcpu_fini(test, vcpu);
	if (kvm)
		test_kvm_fini(test, kvm);
}

/* Test parameter information to test arm64_check_feature_one() */
struct check_feature_one_test {
	enum feature_check_type type;
	int	value;
	int	limit;
	int	expected;
};

struct check_feature_one_test feature_one_params[] = {
	{FCT_LOWER_SAFE,  0,  0,  0},
	{FCT_LOWER_SAFE, -1, -1,  0},
	{FCT_LOWER_SAFE,  1,  1,  0},
	{FCT_LOWER_SAFE,  1,  2,  0},
	{FCT_LOWER_SAFE, -1,  0,  0},
	{FCT_LOWER_SAFE,  2,  1, -1},
	{FCT_LOWER_SAFE, -1, -2, -1},

	{FCT_HIGHER_SAFE,  0,  0,  0},
	{FCT_HIGHER_SAFE, -1, -1,  0},
	{FCT_HIGHER_SAFE,  1,  1,  0},
	{FCT_HIGHER_SAFE,  1,  2, -1},
	{FCT_HIGHER_SAFE, -1,  0, -1},
	{FCT_HIGHER_SAFE,  2,  1,  0},
	{FCT_HIGHER_SAFE, -1, -2,  0},

	{FCT_HIGHER_OR_ZERO_SAFE,  0,  0,  0},
	{FCT_HIGHER_OR_ZERO_SAFE, -1, -1,  0},
	{FCT_HIGHER_OR_ZERO_SAFE,  1,  1,  0},
	{FCT_HIGHER_OR_ZERO_SAFE,  1,  2, -1},
	{FCT_HIGHER_OR_ZERO_SAFE, -1,  0, -1},
	{FCT_HIGHER_OR_ZERO_SAFE,  2,  1,  0},
	{FCT_HIGHER_OR_ZERO_SAFE, -1, -2,  0},
	{FCT_HIGHER_OR_ZERO_SAFE,  0,  2,  0},

	{FCT_EXACT,  0,  0,  0},
	{FCT_EXACT, -1, -1,  0},
	{FCT_EXACT,  1,  1,  0},
	{FCT_EXACT,  1,  2, -1},
	{FCT_EXACT, -1,  0, -1},
	{FCT_EXACT,  2,  1, -1},
	{FCT_EXACT, -1, -2, -1},

	{FCT_IGNORE,  0,  0,  0},
	{FCT_IGNORE, -1, -1,  0},
	{FCT_IGNORE,  1,  1,  0},
	{FCT_IGNORE,  1,  2,  0},
	{FCT_IGNORE, -1,  0,  0},
	{FCT_IGNORE,  2,  1,  0},
	{FCT_IGNORE, -1, -2,  0},
};

static void feature_one_case_to_desc(struct check_feature_one_test *t,
				     char *desc)
{
	snprintf(desc, KUNIT_PARAM_DESC_SIZE,
		 "type:%x, value:%d, limit:%d\n", t->type, t->value, t->limit);
}

void arm64_check_feature_one_test(struct kunit *test)
{
	const struct check_feature_one_test *ft = test->param_value;

	KUNIT_EXPECT_EQ(test,
			arm64_check_feature_one(ft->type, ft->value, ft->limit),
			ft->expected);
}

KUNIT_ARRAY_PARAM(feature_one, feature_one_params, feature_one_case_to_desc);


/* Test parameter information to test arm64_check_features */
struct check_features_test {
	u64	check_types;
	u64	value;
	u64	limit;
	int	expected;
};

#define	U_FEAT_TEST(shift, type, value, limit, exp)	\
	{U_FCT(shift, type), (u64)value << shift, (u64)limit << shift, exp}

#define	S_FEAT_TEST(shift, type, value, limit, exp)	\
	{S_FCT(shift, type), (u64)value << shift, (u64)limit << shift, exp}

struct check_features_test features_params[] = {
	/* Unsigned */
	U_FEAT_TEST(0, FCT_LOWER_SAFE, 1, 2, 0),
	U_FEAT_TEST(0, FCT_HIGHER_SAFE, 1, 2, -E2BIG),
	U_FEAT_TEST(0, FCT_HIGHER_OR_ZERO_SAFE, 1, 2, -E2BIG),
	U_FEAT_TEST(0, FCT_EXACT, 1, 2, -E2BIG),
	U_FEAT_TEST(0, FCT_IGNORE, 1, 2, 0),
	U_FEAT_TEST(0, FCT_LOWER_SAFE, 1, 0xf, 0),
	U_FEAT_TEST(0, FCT_HIGHER_SAFE, 1, 0xf, -E2BIG),
	U_FEAT_TEST(0, FCT_HIGHER_OR_ZERO_SAFE, 1, 0xf, -E2BIG),
	U_FEAT_TEST(0, FCT_EXACT, 1, 0xf, -E2BIG),
	U_FEAT_TEST(0, FCT_IGNORE, 1, 0xf, 0),
	U_FEAT_TEST(60, FCT_LOWER_SAFE, 1, 2, 0),
	U_FEAT_TEST(60, FCT_HIGHER_SAFE, 1, 2, -E2BIG),
	U_FEAT_TEST(60, FCT_HIGHER_OR_ZERO_SAFE, 1, 2, -E2BIG),
	U_FEAT_TEST(60, FCT_EXACT, 1, 2, -E2BIG),
	U_FEAT_TEST(60, FCT_IGNORE, 1, 2, 0),
	U_FEAT_TEST(60, FCT_LOWER_SAFE, 1, 0xf, 0),
	U_FEAT_TEST(60, FCT_HIGHER_SAFE, 1, 0xf, -E2BIG),
	U_FEAT_TEST(60, FCT_HIGHER_OR_ZERO_SAFE, 1, 0xf, -E2BIG),
	U_FEAT_TEST(60, FCT_EXACT, 1, 0xf, -E2BIG),
	U_FEAT_TEST(60, FCT_IGNORE, 1, 0xf, 0),

	/* Signed */
	S_FEAT_TEST(0, FCT_LOWER_SAFE, 1, 2, 0),
	S_FEAT_TEST(0, FCT_HIGHER_SAFE, 1, 2, -E2BIG),
	S_FEAT_TEST(0, FCT_HIGHER_OR_ZERO_SAFE, 1, 2, -E2BIG),
	S_FEAT_TEST(0, FCT_EXACT, 1, 2, -E2BIG),
	S_FEAT_TEST(0, FCT_IGNORE, 1, 2, 0),
	S_FEAT_TEST(0, FCT_LOWER_SAFE, 1, 0xf, -E2BIG),
	S_FEAT_TEST(0, FCT_HIGHER_SAFE, 1, 0xf, 0),
	S_FEAT_TEST(0, FCT_HIGHER_OR_ZERO_SAFE, 1, 0xf, 0),
	S_FEAT_TEST(0, FCT_EXACT, 1, 0xf, -E2BIG),
	S_FEAT_TEST(0, FCT_IGNORE, 1, 0xf, 0),
	S_FEAT_TEST(60, FCT_LOWER_SAFE, 1, 2, 0),
	S_FEAT_TEST(60, FCT_HIGHER_SAFE, 1, 2, -E2BIG),
	S_FEAT_TEST(60, FCT_HIGHER_OR_ZERO_SAFE, 1, 2, -E2BIG),
	S_FEAT_TEST(60, FCT_EXACT, 1, 2, -E2BIG),
	S_FEAT_TEST(60, FCT_IGNORE, 1, 2, 0),
	S_FEAT_TEST(60, FCT_LOWER_SAFE, 1, 0xf, -E2BIG),
	S_FEAT_TEST(60, FCT_HIGHER_SAFE, 1, 0xf, 0),
	S_FEAT_TEST(60, FCT_HIGHER_OR_ZERO_SAFE, 1, 0xf, 0),
	S_FEAT_TEST(60, FCT_EXACT, 1, 0xf, -E2BIG),
	S_FEAT_TEST(60, FCT_IGNORE, 1, 0xf, 0),
};

static void features_case_to_desc(struct check_features_test *t, char *desc)
{
	snprintf(desc, KUNIT_PARAM_DESC_SIZE,
		 "check_types:0x%llx, value:0x%llx, limit:0x%llx\n",
		 t->check_types, t->value, t->limit);
}

KUNIT_ARRAY_PARAM(features, features_params, features_case_to_desc);


void arm64_check_features_test(struct kunit *test)
{
	const struct check_features_test *ft = test->param_value;

	KUNIT_EXPECT_EQ(test,
		arm64_check_features(ft->check_types, ft->value, ft->limit),
		ft->expected);
}


/* Test parameter information to test vcpu_id_reg_feature_frac_check */
struct feat_info {
	u32	id;
	u32	shift;
	u32	value;
	u32	limit;
	u8	check_type;
};

struct frac_check_test {
	struct feat_info feat;
	struct feat_info frac_feat;
	int ret;
};

#define	FEAT(id, shift, value, limit, type)	{id, shift, value, limit, type}

struct frac_check_test frac_params[] = {
	{
		FEAT(SYS_ID_AA64PFR1_EL1, 12, 1, 2, U_FCT(0, FCT_LOWER_SAFE)),
		FEAT(SYS_ID_AA64PFR1_EL1, 32, 1, 1, U_FCT(0, FCT_LOWER_SAFE)),
		0,
	},
	{
		FEAT(SYS_ID_AA64PFR1_EL1, 12, 1, 2, U_FCT(0, FCT_LOWER_SAFE)),
		FEAT(SYS_ID_AA64PFR1_EL1, 32, 1, 2, U_FCT(0, FCT_LOWER_SAFE)),
		0,
	},
	{
		FEAT(SYS_ID_AA64PFR1_EL1, 12, 1, 2, U_FCT(0, FCT_LOWER_SAFE)),
		FEAT(SYS_ID_AA64PFR1_EL1, 32, 2, 1, U_FCT(0, FCT_LOWER_SAFE)),
		0,
	},
	{
		FEAT(SYS_ID_AA64PFR1_EL1, 12, 1, 1, U_FCT(0, FCT_LOWER_SAFE)),
		FEAT(SYS_ID_AA64PFR1_EL1, 32, 1, 1, U_FCT(0, FCT_LOWER_SAFE)),
		0,
	},
	{
		FEAT(SYS_ID_AA64PFR1_EL1, 12, 1, 1, U_FCT(0, FCT_LOWER_SAFE)),
		FEAT(SYS_ID_AA64PFR1_EL1, 32, 1, 2, U_FCT(0, FCT_LOWER_SAFE)),
		0,
	},
	{
		FEAT(SYS_ID_AA64PFR1_EL1, 12, 1, 1, U_FCT(0, FCT_LOWER_SAFE)),
		FEAT(SYS_ID_AA64PFR1_EL1, 32, 2, 1, U_FCT(0, FCT_LOWER_SAFE)),
		-E2BIG,
	},

};

static void frac_case_to_desc(struct frac_check_test *t, char *desc)
{
	struct feat_info *feat = &t->feat;
	struct feat_info *frac = &t->frac_feat;

	snprintf(desc, KUNIT_PARAM_DESC_SIZE,
		 "feat - shift:%d, val:%d, lim:%d, frac - shift:%d, val:%d, lim:%d, type:%x\n",
		 feat->shift, feat->value, feat->limit,
		 frac->shift, frac->value, frac->limit, frac->check_type);
}

KUNIT_ARRAY_PARAM(frac, frac_params, frac_case_to_desc);

static void vcpu_id_reg_feature_frac_check_test(struct kunit *test)
{
	struct kvm_vcpu *vcpu;
	u32 id, frac_id;
	struct id_reg_info id_data, frac_id_data;
	struct id_reg_info *idr, *frac_idr;
	struct feature_frac frac_data, *frac = &frac_data;
	const struct frac_check_test *frct = test->param_value;

	vcpu = test_kvm_vcpu_init(test);
	KUNIT_EXPECT_TRUE(test, vcpu);
	if (!vcpu)
		return;

	id = frct->feat.id;
	frac_id = frct->frac_feat.id;

	frac->id = id;
	frac->shift = frct->feat.shift;
	frac->frac_id = frac_id;
	frac->frac_shift = frct->frac_feat.shift;
	frac->frac_ftr_check = frct->frac_feat.check_type;

	idr = GET_ID_REG_INFO(id);
	frac_idr = GET_ID_REG_INFO(frac_id);

	/* Save the original id_reg_info (and restore later) */
	memcpy(&id_data, idr, sizeof(id_data));
	memcpy(&frac_id_data, frac_idr, sizeof(frac_id_data));

	/* The id could be same as the frac_id */
	idr->vcpu_limit_val = (u64)frct->feat.limit << frac->shift;
	frac_idr->vcpu_limit_val |=
			(u64)frct->frac_feat.limit << frac->frac_shift;

	__vcpu_sys_reg(vcpu, IDREG_SYS_IDX(id)) =
			(u64)frct->feat.value << frac->shift;
	__vcpu_sys_reg(vcpu, IDREG_SYS_IDX(frac_id)) |=
			(u64)frct->frac_feat.value << frac->frac_shift;

	KUNIT_EXPECT_EQ(test,
			vcpu_id_reg_feature_frac_check(vcpu, frac),
			frct->ret);

	test_kvm_vcpu_fini(test, vcpu);

	/* Restore id_reg_info */
	memcpy(idr, &id_data, sizeof(id_data));
	memcpy(frac_idr, &frac_id_data, sizeof(frac_id_data));
}

/*
 * Test parameter information to test validate_id_aa64mmfr0_tgran2
 * and validate_id_aa64mmfr0_el1_test.
 */
struct tgran_test {
	int gran2_field;
	int gran2;
	int gran2_lim;
	int gran1;
	int gran1_lim;
	int ret;
};

struct tgran_test tgran4_2_test_params[] = {
	{ID_AA64MMFR0_TGRAN4_2_SHIFT, 2, 2,  0,  0, 0},
	{ID_AA64MMFR0_TGRAN4_2_SHIFT, 2, 1,  0,  0, -E2BIG},
	{ID_AA64MMFR0_TGRAN4_2_SHIFT, 1, 2,  0,  0, 0},
	{ID_AA64MMFR0_TGRAN4_2_SHIFT, 0, 0,  0,  0, 0},
	{ID_AA64MMFR0_TGRAN4_2_SHIFT, 0, 1, -1,  0, 0},
	{ID_AA64MMFR0_TGRAN4_2_SHIFT, 0, 1,  0,  0, -E2BIG},
	{ID_AA64MMFR0_TGRAN4_2_SHIFT, 0, 2, -1,  0, 0},
	{ID_AA64MMFR0_TGRAN4_2_SHIFT, 0, 2,  1,  0, -E2BIG},
	{ID_AA64MMFR0_TGRAN4_2_SHIFT, 1, 0,  0, -1,  0},
	{ID_AA64MMFR0_TGRAN4_2_SHIFT, 1, 0,  0,  0,  0},
	{ID_AA64MMFR0_TGRAN4_2_SHIFT, 2, 0,  0, -1,  -E2BIG},
	{ID_AA64MMFR0_TGRAN4_2_SHIFT, 2, 0,  0,  0,  0},
	{ID_AA64MMFR0_TGRAN4_2_SHIFT, 2, 0,  0,  2,  0},
};

struct tgran_test tgran64_2_test_params[] = {
	{ID_AA64MMFR0_TGRAN64_2_SHIFT, 2, 2,  0,  0, 0},
	{ID_AA64MMFR0_TGRAN64_2_SHIFT, 2, 1,  0,  0, -E2BIG},
	{ID_AA64MMFR0_TGRAN64_2_SHIFT, 1, 2,  0,  0, 0},
	{ID_AA64MMFR0_TGRAN64_2_SHIFT, 0, 0,  0,  0, 0},
	{ID_AA64MMFR0_TGRAN64_2_SHIFT, 0, 1, -1,  0, 0},
	{ID_AA64MMFR0_TGRAN64_2_SHIFT, 0, 1,  0,  0, -E2BIG},
	{ID_AA64MMFR0_TGRAN64_2_SHIFT, 0, 2, -1,  0, 0},
	{ID_AA64MMFR0_TGRAN64_2_SHIFT, 0, 2,  1,  0, -E2BIG},
	{ID_AA64MMFR0_TGRAN64_2_SHIFT, 1, 0,  0, -1, 0},
	{ID_AA64MMFR0_TGRAN64_2_SHIFT, 1, 0,  0,  0, 0},
	{ID_AA64MMFR0_TGRAN64_2_SHIFT, 2, 0,  0, -1, -E2BIG},
	{ID_AA64MMFR0_TGRAN64_2_SHIFT, 2, 0,  0,  0, 0},
	{ID_AA64MMFR0_TGRAN64_2_SHIFT, 2, 0,  0,  2, 0},
};

struct tgran_test tgran16_2_test_params[] = {
	{ID_AA64MMFR0_TGRAN16_2_SHIFT, 2, 2,  0,  0, 0},
	{ID_AA64MMFR0_TGRAN16_2_SHIFT, 2, 1,  0,  0, -E2BIG},
	{ID_AA64MMFR0_TGRAN16_2_SHIFT, 1, 2,  0,  0, 0},
	{ID_AA64MMFR0_TGRAN16_2_SHIFT, 0, 0,  0,  0, 0},
	{ID_AA64MMFR0_TGRAN16_2_SHIFT, 0, 1,  0,  0, 0},
	{ID_AA64MMFR0_TGRAN16_2_SHIFT, 0, 1,  1,  0, -E2BIG},
	{ID_AA64MMFR0_TGRAN16_2_SHIFT, 0, 2,  0,  0, 0},
	{ID_AA64MMFR0_TGRAN16_2_SHIFT, 0, 2,  2,  0, -E2BIG},
	{ID_AA64MMFR0_TGRAN16_2_SHIFT, 1, 0,  0,  0, 0},
	{ID_AA64MMFR0_TGRAN16_2_SHIFT, 1, 0,  0,  1, 0},
	{ID_AA64MMFR0_TGRAN16_2_SHIFT, 2, 0,  0,  0, -E2BIG},
	{ID_AA64MMFR0_TGRAN16_2_SHIFT, 2, 0,  0,  1, 0},
	{ID_AA64MMFR0_TGRAN16_2_SHIFT, 2, 0,  0,  2, 0},
};

static void tgran2_case_to_desc(struct tgran_test *t, char *desc)
{
	snprintf(desc, KUNIT_PARAM_DESC_SIZE,
		 "gran2(field=%d): val=%d, lim=%d gran1: val=%d limit=%d\n",
		 t->gran2_field, t->gran2, t->gran2_lim,
		 t->gran1, t->gran1_lim);
}

KUNIT_ARRAY_PARAM(tgran4_2, tgran4_2_test_params, tgran2_case_to_desc);
KUNIT_ARRAY_PARAM(tgran64_2, tgran64_2_test_params, tgran2_case_to_desc);
KUNIT_ARRAY_PARAM(tgran16_2, tgran16_2_test_params, tgran2_case_to_desc);

#define	MAKE_MMFR0_TGRAN(shift1, gran1, shift2, gran2)		\
	(((u64)((gran1) & 0xf) << (shift1)) |			\
	 ((u64)((gran2) & 0xf) << (shift2)))

static int tgran2_to_tgran1_shift(int tgran2_shift)
{
	int tgran1_shift = -1;

	switch (tgran2_shift) {
	case ID_AA64MMFR0_TGRAN4_2_SHIFT:
		tgran1_shift = ID_AA64MMFR0_TGRAN4_SHIFT;
		break;
	case ID_AA64MMFR0_TGRAN64_2_SHIFT:
		tgran1_shift = ID_AA64MMFR0_TGRAN64_SHIFT;
		break;
	case ID_AA64MMFR0_TGRAN16_2_SHIFT:
		tgran1_shift = ID_AA64MMFR0_TGRAN16_SHIFT;
		break;
	default:
		break;
	}

	return tgran1_shift;
}

static void validate_id_aa64mmfr0_tgran2_test(struct kunit *test)
{
	const struct tgran_test *t = test->param_value;
	int shift1, shift2;
	u64 v, lim;

	shift2 = t->gran2_field;
	shift1 = tgran2_to_tgran1_shift(shift2);
	v = MAKE_MMFR0_TGRAN(shift1, t->gran1, shift2, t->gran2);
	lim = MAKE_MMFR0_TGRAN(shift1, t->gran1_lim, shift2, t->gran2_lim);

	KUNIT_EXPECT_EQ(test, aa64mmfr0_tgran2_check(shift2, v, lim), t->ret);
}

static void validate_id_aa64pfr0_el1_test(struct kunit *test)
{
	struct id_reg_info *id_reg;
	struct kvm_vcpu *vcpu;
	u64 v;

	vcpu = test_kvm_vcpu_init(test);
	KUNIT_EXPECT_TRUE(test, vcpu);
	if (!vcpu)
		return;

	id_reg = GET_ID_REG_INFO(SYS_ID_AA64PFR0_EL1);

	v = 0;
	KUNIT_EXPECT_EQ(test, validate_id_aa64pfr0_el1(vcpu, id_reg, v), 0);

	v = 0x000010000;	/* ASIMD = 0, FP = 1 */
	KUNIT_EXPECT_NE(test, validate_id_aa64pfr0_el1(vcpu, id_reg, v), 0);

	v = 0x000100000;	/* ASIMD = 1, FP = 0 */
	KUNIT_EXPECT_NE(test, validate_id_aa64pfr0_el1(vcpu, id_reg, v), 0);

	v = 0x000ff0000;	/* ASIMD = 0xf, FP = 0xf */
	KUNIT_EXPECT_EQ(test, validate_id_aa64pfr0_el1(vcpu, id_reg, v), 0);

	v = 0x100000000;	/* SVE =1, ASIMD = 0, FP = 0 */
	KUNIT_EXPECT_NE(test, validate_id_aa64pfr0_el1(vcpu, id_reg, v), 0);
	if (!system_supports_sve()) {
		test_kvm_vcpu_fini(test, vcpu);
		kunit_skip(test, "No SVE support. Partial skip)");
		/* Not reached */
	}

	vcpu->arch.flags |= KVM_ARM64_GUEST_HAS_SVE;

	v = 0x100000000;	/* SVE =1, ASIMD = 0, FP = 0 */
	KUNIT_EXPECT_EQ(test, validate_id_aa64pfr0_el1(vcpu, id_reg, v), 0);

	v = 0x100ff0000;	/* SVE =1, ASIMD = 0, FP = 0 */
	KUNIT_EXPECT_NE(test, validate_id_aa64pfr0_el1(vcpu, id_reg, v), 0);

	vcpu->arch.flags &= ~KVM_ARM64_GUEST_HAS_SVE;

	v = 0x1000000;		/* GIC = 1 */
	KUNIT_EXPECT_NE(test, validate_id_aa64pfr0_el1(vcpu, id_reg, v), 0);

	vcpu->kvm->arch.vgic.in_kernel = true;
	v = 0x1000000;		/* GIC = 1 */
	KUNIT_EXPECT_NE(test, validate_id_aa64pfr0_el1(vcpu, id_reg, v), 0);

	vcpu->kvm->arch.vgic.vgic_model = KVM_DEV_TYPE_ARM_VGIC_V2;
	v = 0x1000000;		/* GIC = 1 */
	KUNIT_EXPECT_NE(test, validate_id_aa64pfr0_el1(vcpu, id_reg, v), 0);

	v = 0;		/* GIC = 0 */
	KUNIT_EXPECT_EQ(test, validate_id_aa64pfr0_el1(vcpu, id_reg, v), 0);

	v = 0x1000000;		/* GIC = 1 */
	vcpu->kvm->arch.vgic.vgic_model = KVM_DEV_TYPE_ARM_VGIC_V3;
	KUNIT_EXPECT_EQ(test, validate_id_aa64pfr0_el1(vcpu, id_reg, v), 0);

	test_kvm_vcpu_fini(test, vcpu);
}

static void validate_id_aa64pfr1_el1_test(struct kunit *test)
{
	struct id_reg_info *id_reg;
	struct kvm_vcpu *vcpu;
	u64 v;

	vcpu = test_kvm_vcpu_init(test);
	KUNIT_EXPECT_TRUE(test, vcpu);
	if (!vcpu)
		return;

	id_reg = GET_ID_REG_INFO(SYS_ID_AA64PFR1_EL1);

	v = 0;
	KUNIT_EXPECT_EQ(test, validate_id_aa64pfr1_el1(vcpu, id_reg, v), 0);

	v = 0x100;	/* MTE = 1*/
	KUNIT_EXPECT_NE(test, validate_id_aa64pfr1_el1(vcpu, id_reg, v), 0);

	if (!system_supports_mte()) {
		test_kvm_vcpu_fini(test, vcpu);
		kunit_skip(test, "(No MTE support. Partial skip)");
		/* Not reached */
	}

	vcpu->kvm->arch.mte_enabled = true;

	v = 0x100;	/* MTE = 1*/
	KUNIT_EXPECT_EQ(test, validate_id_aa64pfr1_el1(vcpu, id_reg, v), 0);

	v = 0x0;
	vcpu->kvm->arch.mte_enabled = true;
	KUNIT_EXPECT_NE(test, validate_id_aa64pfr1_el1(vcpu, id_reg, v), 0);

	test_kvm_vcpu_fini(test, vcpu);
}

static void validate_id_aa64isar0_el1_test(struct kunit *test)
{
	struct id_reg_info *id_reg;
	struct kvm_vcpu *vcpu;
	u64 v;

	vcpu = test_kvm_vcpu_init(test);
	KUNIT_EXPECT_TRUE(test, vcpu);
	if (!vcpu)
		return;

	id_reg = GET_ID_REG_INFO(SYS_ID_AA64ISAR0_EL1);

	v = 0;
	KUNIT_EXPECT_EQ(test, validate_id_aa64isar0_el1(vcpu, id_reg, v), 0);

	v = 0x01000000000;	/* SM4 = 0, SM3 = 1 */
	KUNIT_EXPECT_NE(test, validate_id_aa64isar0_el1(vcpu, id_reg, v), 0);

	v = 0x10000000000;	/* SM4 = 1, SM3 = 0 */
	KUNIT_EXPECT_NE(test, validate_id_aa64isar0_el1(vcpu, id_reg, v), 0);

	v = 0x11000000000;	/* SM3 = SM4 = 1 */
	KUNIT_EXPECT_EQ(test, validate_id_aa64isar0_el1(vcpu, id_reg, v), 0);

	v = 0x000000100;	/* SHA2 = 0, SHA1 = 1 */
	KUNIT_EXPECT_NE(test, validate_id_aa64isar0_el1(vcpu, id_reg, v), 0);

	v = 0x000001000;	/* SHA2 = 1, SHA1 = 0 */
	KUNIT_EXPECT_NE(test, validate_id_aa64isar0_el1(vcpu, id_reg, v), 0);

	v = 0x000001100;	/* SHA2 = 1, SHA1 = 1 */
	KUNIT_EXPECT_EQ(test, validate_id_aa64isar0_el1(vcpu, id_reg, v), 0);

	v = 0x100002000;	/* SHA3 = 1, SHA2 = 2 */
	KUNIT_EXPECT_NE(test, validate_id_aa64isar0_el1(vcpu, id_reg, v), 0);

	v = 0x000002000;	/* SHA3 = 0, SHA2 = 2 */
	KUNIT_EXPECT_NE(test, validate_id_aa64isar0_el1(vcpu, id_reg, v), 0);

	v = 0x100001000;	/* SHA3 = 1, SHA2 = 1 */
	KUNIT_EXPECT_NE(test, validate_id_aa64isar0_el1(vcpu, id_reg, v), 0);

	v = 0x200000000;	/* SHA3 = 2, SHA1 = 0 */
	KUNIT_EXPECT_NE(test, validate_id_aa64isar0_el1(vcpu, id_reg, v), 0);

	v = 0x200001100;	/* SHA3 = 2, SHA2= 1, SHA1 = 1 */
	KUNIT_EXPECT_EQ(test, validate_id_aa64isar0_el1(vcpu, id_reg, v), 0);

	v = 0x300003300;	/* SHA3 = 3, SHA2 = 3, SHA1 = 3 */
	KUNIT_EXPECT_EQ(test, validate_id_aa64isar0_el1(vcpu, id_reg, v), 0);

	test_kvm_vcpu_fini(test, vcpu);
}

static void validate_id_aa64isar1_el1_test(struct kunit *test)
{
	struct id_reg_info *id_reg;
	struct kvm_vcpu *vcpu;
	u64 v;

	vcpu = test_kvm_vcpu_init(test);
	KUNIT_EXPECT_TRUE(test, vcpu);
	if (!vcpu)
		return;

	id_reg = GET_ID_REG_INFO(SYS_ID_AA64ISAR1_EL1);

	v = 0;
	KUNIT_EXPECT_EQ(test, validate_id_aa64isar1_el1(vcpu, id_reg, v), 0);

	v = 0x11000110;	/* GPI = 1, GPA = 1, API = 1, APA = 1 */
	KUNIT_EXPECT_NE(test, validate_id_aa64isar1_el1(vcpu, id_reg, v), 0);

	v = 0x11000100;	/* GPI = 1, GPA = 1, API = 1 */
	KUNIT_EXPECT_NE(test, validate_id_aa64isar1_el1(vcpu, id_reg, v), 0);

	v = 0x11000010;	/* GPI = 1, GPA = 1, APA = 1 */
	KUNIT_EXPECT_NE(test, validate_id_aa64isar1_el1(vcpu, id_reg, v), 0);

	v = 0x10000110;	/* GPI = 1, API = 1, APA = 1 */
	KUNIT_EXPECT_NE(test, validate_id_aa64isar1_el1(vcpu, id_reg, v), 0);

	v = 0x01000110;	/* GPA = 1, API = 1, APA = 1 */
	KUNIT_EXPECT_NE(test, validate_id_aa64isar1_el1(vcpu, id_reg, v), 0);

	if (!system_has_full_ptr_auth()) {
		test_kvm_vcpu_fini(test, vcpu);
		kunit_skip(test, "(No PTRAUTH support. Partial skip)");
		/* Not reached */
	}

	vcpu->arch.flags |= KVM_ARM64_GUEST_HAS_PTRAUTH;

	v = 0x10000100;	/* GPI = 1, API = 1 */
	KUNIT_EXPECT_EQ(test, validate_id_aa64isar1_el1(vcpu, id_reg, v), 0);

	v = 0x10000010;	/* GPI = 1, APA = 1 */
	KUNIT_EXPECT_EQ(test, validate_id_aa64isar1_el1(vcpu, id_reg, v), 0);

	v = 0x01000100;	/* GPA = 1, API = 1 */
	KUNIT_EXPECT_EQ(test, validate_id_aa64isar1_el1(vcpu, id_reg, v), 0);

	v = 0x01000010;	/* GPA = 1, APA = 1 */
	KUNIT_EXPECT_EQ(test, validate_id_aa64isar1_el1(vcpu, id_reg, v), 0);

	v = 0;
	KUNIT_EXPECT_NE(test, validate_id_aa64isar1_el1(vcpu, id_reg, v), 0);

	test_kvm_vcpu_fini(test, vcpu);
}

static void validate_id_aa64mmfr0_el1_test(struct kunit *test)
{
	struct id_reg_info id_data, *id_reg;
	const struct tgran_test *t4, *t64, *t16;
	struct kvm_vcpu *vcpu;
	int field4, field4_2, field64, field64_2, field16, field16_2;
	u64 v, v4, lim4, v64, lim64, v16, lim16;
	int i, j, ret;

	id_reg = GET_ID_REG_INFO(SYS_ID_AA64MMFR0_EL1);

	/* Save the original id_reg_info (and restore later) */
	memcpy(&id_data, id_reg, sizeof(id_data));

	vcpu = test_kvm_vcpu_init(test);

	t4 = test->param_value;
	field4_2 = t4->gran2_field;
	field4 = tgran2_to_tgran1_shift(field4_2);
	v4 = MAKE_MMFR0_TGRAN(field4, t4->gran1, field4_2, t4->gran2);
	lim4 = MAKE_MMFR0_TGRAN(field4, t4->gran1_lim, field4_2, t4->gran2_lim);

	/*
	 * For each given gran4_2 params, test validate_id_aa64mmfr0_el1
	 * with each of tgran64_2 and tgran16_2 params.
	 */
	for (i = 0; i < ARRAY_SIZE(tgran64_2_test_params); i++) {
		t64 = &tgran64_2_test_params[i];
		field64_2 = t64->gran2_field;
		field64 = tgran2_to_tgran1_shift(field64_2);
		v64 = MAKE_MMFR0_TGRAN(field64, t64->gran1,
				       field64_2, t64->gran2);
		lim64 = MAKE_MMFR0_TGRAN(field64, t64->gran1_lim,
					 field64_2, t64->gran2_lim);

		for (j = 0; j < ARRAY_SIZE(tgran16_2_test_params); j++) {
			t16 = &tgran16_2_test_params[j];

			field16_2 = t16->gran2_field;
			field16 = tgran2_to_tgran1_shift(field16_2);
			v16 = MAKE_MMFR0_TGRAN(field16, t16->gran1,
					       field16_2, t16->gran2);
			lim16 = MAKE_MMFR0_TGRAN(field16, t16->gran1_lim,
						 field16_2, t16->gran2_lim);

			/* Build id_aa64mmfr0_el1 from tgran16/64/4 values */
			v = v16 | v64 | v4;
			id_reg->vcpu_limit_val = lim16 | lim64 | lim4;

			ret = t4->ret ? t4->ret : t64->ret;
			ret = ret ? ret : t16->ret;
			KUNIT_EXPECT_EQ(test,
				validate_id_aa64mmfr0_el1(vcpu, id_reg, v),
				ret);
		}
	}

	/* Restore id_reg_info */
	memcpy(id_reg, &id_data, sizeof(id_data));
	test_kvm_vcpu_fini(test, vcpu);
}

static void validate_id_aa64dfr0_el1_test(struct kunit *test)
{
	struct id_reg_info *id_reg;
	struct kvm_vcpu *vcpu;
	u64 v;

	id_reg = GET_ID_REG_INFO(SYS_ID_AA64DFR0_EL1);
	vcpu = test_kvm_vcpu_init(test);
	KUNIT_EXPECT_TRUE(test, vcpu);
	if (!vcpu)
		return;

	v = 0;
	KUNIT_EXPECT_EQ(test, validate_id_aa64dfr0_el1(vcpu, id_reg, v), 0);

	v = 0x10001000;	/* CTX_CMPS = 2, BRPS = 1 */
	KUNIT_EXPECT_EQ(test, validate_id_aa64dfr0_el1(vcpu, id_reg, v), 0);

	v = 0x20001000;	/* CTX_CMPS = 2, BRPS = 1 */
	KUNIT_EXPECT_NE(test, validate_id_aa64dfr0_el1(vcpu, id_reg, v), 0);

	v = 0xf00;	/* PMUVER = 0xf */
	KUNIT_EXPECT_EQ(test, validate_id_aa64dfr0_el1(vcpu, id_reg, v), 0);

	v = 0x100;	/* PMUVER = 1 */
	KUNIT_EXPECT_NE(test, validate_id_aa64dfr0_el1(vcpu, id_reg, v), 0);

	set_bit(KVM_ARM_VCPU_PMU_V3, vcpu->arch.features);

	v = 0x100;	/* PMUVER = 1 */
	KUNIT_EXPECT_EQ(test, validate_id_aa64dfr0_el1(vcpu, id_reg, v), 0);

	v = 0x0;	/* PMUVER = 0 */
	KUNIT_EXPECT_NE(test, validate_id_aa64dfr0_el1(vcpu, id_reg, v), 0);

	test_kvm_vcpu_fini(test, vcpu);
}

static void validate_id_dfr0_el1_test(struct kunit *test)
{
	struct id_reg_info *id_reg;
	struct kvm_vcpu *vcpu;
	u64 v;

	id_reg = GET_ID_REG_INFO(SYS_ID_DFR0_EL1);
	vcpu = test_kvm_vcpu_init(test);
	KUNIT_EXPECT_TRUE(test, vcpu);
	if (!vcpu)
		return;

	v = 0;
	KUNIT_EXPECT_EQ(test, validate_id_dfr0_el1(vcpu, id_reg, v), 0);

	v = 0xf000000;	/* PERFMON = 0xf */
	KUNIT_EXPECT_EQ(test, validate_id_dfr0_el1(vcpu, id_reg, v), 0);

	v = 0x1000000;	/* PERFMON = 1 */
	KUNIT_EXPECT_NE(test, validate_id_dfr0_el1(vcpu, id_reg, v), 0);

	v = 0x2000000;	/* PERFMON = 2 */
	KUNIT_EXPECT_NE(test, validate_id_dfr0_el1(vcpu, id_reg, v), 0);

	v = 0x3000000;	/* PERFMON = 3 */
	KUNIT_EXPECT_NE(test, validate_id_dfr0_el1(vcpu, id_reg, v), 0);

	set_bit(KVM_ARM_VCPU_PMU_V3, vcpu->arch.features);

	v = 0x1000000;	/* PERFMON = 1 */
	KUNIT_EXPECT_NE(test, validate_id_dfr0_el1(vcpu, id_reg, v), 0);

	v = 0x2000000;	/* PERFMON = 2 */
	KUNIT_EXPECT_NE(test, validate_id_dfr0_el1(vcpu, id_reg, v), 0);

	v = 0x3000000;	/* PERFMON = 3 */
	KUNIT_EXPECT_EQ(test, validate_id_dfr0_el1(vcpu, id_reg, v), 0);

	v = 0xf000000;	/* PERFMON = 0xf */
	KUNIT_EXPECT_NE(test, validate_id_dfr0_el1(vcpu, id_reg, v), 0);

	test_kvm_vcpu_fini(test, vcpu);
}

static void validate_mvfr1_el1_test(struct kunit *test)
{
	struct id_reg_info *id_reg;
	struct kvm_vcpu *vcpu;
	u64 v;

	id_reg = GET_ID_REG_INFO(SYS_MVFR1_EL1);
	vcpu = test_kvm_vcpu_init(test);
	KUNIT_EXPECT_TRUE(test, vcpu);
	if (!vcpu)
		return;

	v = 0;
	KUNIT_EXPECT_EQ(test, validate_mvfr1_el1(vcpu, id_reg, v), 0);

	v = 0x2100000;	/* FPHP = 2, SIMDHP = 1 */
	KUNIT_EXPECT_EQ(test, validate_mvfr1_el1(vcpu, id_reg, v), 0);

	v = 0x3200000;	/* FPHP = 3, SIMDHP = 2 */
	KUNIT_EXPECT_EQ(test, validate_mvfr1_el1(vcpu, id_reg, v), 0);

	v = 0x1100000;	/* FPHP = 1, SIMDHP = 1 */
	KUNIT_EXPECT_NE(test, validate_mvfr1_el1(vcpu, id_reg, v), 0);

	v = 0x2200000;	/* FPHP = 2, SIMDHP = 2 */
	KUNIT_EXPECT_NE(test, validate_mvfr1_el1(vcpu, id_reg, v), 0);

	v = 0x3300000;	/* FPHP = 3, SIMDHP = 3 */
	KUNIT_EXPECT_NE(test, validate_mvfr1_el1(vcpu, id_reg, v), 0);

	v = (u64)-1;
	KUNIT_EXPECT_NE(test, validate_mvfr1_el1(vcpu, id_reg, v), 0);

	test_kvm_vcpu_fini(test, vcpu);
}

static void feature_trap_activate_test(struct kunit *test)
{
	struct kvm_vcpu *vcpu;
	struct feature_config_ctrl config_data, *config = &config_data;
	u64 cfg_mask, cfg_val;

	vcpu = test_kvm_vcpu_init(test);
	KUNIT_EXPECT_TRUE(test, vcpu);
	if (!vcpu)
		return;

	vcpu->arch.hcr_el2 = 0;
	config->ftr_reg = SYS_ID_AA64MMFR1_EL1;
	config->ftr_shift = 4;
	config->ftr_min = 2;
	config->ftr_signed = FTR_UNSIGNED;

	/* Test for hcr_el2 */
	config->cfg_reg = VCPU_HCR_EL2;
	cfg_mask = 0x30000800000;
	cfg_val = 0x30000800000;
	config->cfg_mask = cfg_mask;
	config->cfg_val = cfg_val;

	vcpu->arch.hcr_el2 = 0;
	feature_trap_activate(vcpu, config);
	KUNIT_EXPECT_EQ(test, vcpu->arch.hcr_el2 & cfg_mask, cfg_val);

	cfg_mask = 0x30000800000;
	cfg_val = 0;
	config->cfg_mask = cfg_mask;
	config->cfg_val = cfg_val;

	vcpu->arch.hcr_el2 = 0;
	feature_trap_activate(vcpu, config);
	KUNIT_EXPECT_EQ(test, vcpu->arch.hcr_el2 & cfg_mask, cfg_val);

	/* Test for mdcr_el2 */
	config->cfg_reg = VCPU_MDCR_EL2;
	cfg_mask = 0x30000800000;
	cfg_val = 0x30000800000;
	config->cfg_mask = cfg_mask;
	config->cfg_val = cfg_val;

	vcpu->arch.mdcr_el2 = 0;
	feature_trap_activate(vcpu, config);
	KUNIT_EXPECT_EQ(test, vcpu->arch.mdcr_el2 & cfg_mask, cfg_val);

	cfg_mask = 0x30000800000;
	cfg_val = 0x0;
	config->cfg_mask = cfg_mask;
	config->cfg_val = cfg_val;

	vcpu->arch.mdcr_el2 = 0;
	feature_trap_activate(vcpu, config);
	KUNIT_EXPECT_EQ(test, vcpu->arch.mdcr_el2 & cfg_mask, cfg_val);

	/* Test for cptr_el2 */
	config->cfg_reg = VCPU_CPTR_EL2;
	cfg_mask = 0x30000800000;
	cfg_val = 0x30000800000;
	config->cfg_mask = cfg_mask;
	config->cfg_val = cfg_val;

	vcpu->arch.cptr_el2 = 0;
	feature_trap_activate(vcpu, config);
	KUNIT_EXPECT_EQ(test, vcpu->arch.cptr_el2 & cfg_mask, cfg_val);

	cfg_mask = 0x30000800000;
	cfg_val = 0x0;
	config->cfg_mask = cfg_mask;
	config->cfg_val = cfg_val;

	vcpu->arch.cptr_el2 = 0;
	feature_trap_activate(vcpu, config);
	KUNIT_EXPECT_EQ(test, vcpu->arch.cptr_el2 & cfg_mask, cfg_val);

	test_kvm_vcpu_fini(test, vcpu);
}

static bool test_need_trap_aa64dfr0(struct kvm_vcpu *vcpu)
{
	u64 val;

	val = __vcpu_sys_reg(vcpu, IDREG_SYS_IDX(SYS_ID_AA64DFR0_EL1));
	return ((val & 0xf) == 0);
}

static void id_reg_features_trap_activate_test(struct kunit *test)
{
	struct kvm_vcpu *vcpu;
	u32 id;
	u64 cfg_mask0, cfg_val0, cfg_mask1, cfg_val1, cfg_mask2, cfg_val2;
	u64 cfg_mask, cfg_val, id_reg_sys_val;
	struct id_reg_info id_reg_data;
	struct feature_config_ctrl *config, config0, config1, config2;
	struct feature_config_ctrl *trap_features[] = {
		&config0, &config1, &config2, NULL,
	};

	vcpu = test_kvm_vcpu_init(test);
	KUNIT_EXPECT_TRUE(test, vcpu);
	if (!vcpu)
		return;

	id_reg_sys_val = 0x7777777777777777;
	id = SYS_ID_AA64DFR0_EL1;
	id_reg_data.sys_reg = id;
	id_reg_data.sys_val = id_reg_sys_val;
	id_reg_data.vcpu_limit_val  = (u64)-1;
	id_reg_data.trap_features =
			(const struct feature_config_ctrl *(*)[])trap_features;

	cfg_mask0 = 0x3;
	cfg_val0 = 0x3;
	config = &config0;
	memset(config, 0, sizeof(*config));
	config->ftr_reg = id;
	config->ftr_shift = 60;
	config->ftr_min = 2;
	config->ftr_signed = FTR_UNSIGNED;
	config->cfg_reg = VCPU_HCR_EL2;
	config->cfg_mask = cfg_mask0;
	config->cfg_val = cfg_val0;

	cfg_mask1 = 0x70000040;
	cfg_val1 = 0x30000040;
	config = &config1;
	memset(config, 0, sizeof(*config));
	config->ftr_reg = id;
	config->ftr_need_trap = test_need_trap_aa64dfr0;
	config->ftr_signed = FTR_UNSIGNED;
	config->cfg_reg = VCPU_HCR_EL2;
	config->cfg_mask = cfg_mask1;
	config->cfg_val = cfg_val1;

	/* Feature with signed ID register field */
	cfg_mask2 = 0x70000000800;
	cfg_val2 = 0x30000000800;
	config = &config2;
	memset(config, 0, sizeof(*config));
	config->ftr_reg = id;
	config->ftr_shift = 4;
	config->ftr_min = 0;
	config->ftr_signed = FTR_SIGNED;
	config->cfg_reg = VCPU_HCR_EL2;
	config->cfg_mask = cfg_mask2;
	config->cfg_val = cfg_val2;

	/* Enable features for config0, 1 and 2 */
	__vcpu_sys_reg(vcpu, IDREG_SYS_IDX(id)) = id_reg_sys_val;

	vcpu->arch.hcr_el2 = 0;
	id_reg_features_trap_activate(vcpu, &id_reg_data);
	KUNIT_EXPECT_EQ(test, vcpu->arch.hcr_el2, 0);

	/* Disable features for config0 only */
	__vcpu_sys_reg(vcpu, IDREG_SYS_IDX(id)) = 0x1;
	cfg_mask = cfg_mask0;
	cfg_val = cfg_val0;

	vcpu->arch.hcr_el2 = 0;
	id_reg_features_trap_activate(vcpu, &id_reg_data);
	KUNIT_EXPECT_EQ(test, vcpu->arch.hcr_el2 & cfg_mask, cfg_val);

	/* Disable features for config0 and config1 */
	__vcpu_sys_reg(vcpu, IDREG_SYS_IDX(id)) = 0x0;
	cfg_mask = (cfg_mask0 | cfg_mask1);
	cfg_val = (cfg_val0 | cfg_val1);

	vcpu->arch.hcr_el2 = 0;
	id_reg_features_trap_activate(vcpu, &id_reg_data);
	KUNIT_EXPECT_EQ(test, vcpu->arch.hcr_el2 & cfg_mask, cfg_val);

	/* Disable features for config0, 1, and 2 */
	__vcpu_sys_reg(vcpu, IDREG_SYS_IDX(id)) = 0xf0;
	cfg_mask = (cfg_mask0 | cfg_mask1 | cfg_mask2);
	cfg_val = (cfg_val0 | cfg_val1 | cfg_val2);

	vcpu->arch.hcr_el2 = 0;
	id_reg_features_trap_activate(vcpu, &id_reg_data);
	KUNIT_EXPECT_EQ(test, vcpu->arch.hcr_el2 & cfg_mask, cfg_val);

	/* Test with id_reg_info == NULL */
	vcpu->arch.hcr_el2 = 0;
	id_reg_features_trap_activate(vcpu, NULL);
	KUNIT_EXPECT_EQ(test, vcpu->arch.hcr_el2, 0);

	/* Test with id_reg_data.trap_features = NULL */
	id_reg_data.trap_features = NULL;
	__vcpu_sys_reg(vcpu, IDREG_SYS_IDX(id)) = 0xf0;

	vcpu->arch.hcr_el2 = 0;
	id_reg_features_trap_activate(vcpu, &id_reg_data);
	KUNIT_EXPECT_EQ(test, vcpu->arch.hcr_el2, 0);

	test_kvm_vcpu_fini(test, vcpu);
}

static void vcpu_need_trap_ptrauth_test(struct kunit *test)
{
	struct kvm_vcpu *vcpu;
	u32 id = SYS_ID_AA64ISAR1_EL1;

	vcpu = test_kvm_vcpu_init(test);
	KUNIT_EXPECT_TRUE(test, vcpu);
	if (!vcpu)
		return;

	if (system_has_full_ptr_auth()) {
		__vcpu_sys_reg(vcpu, IDREG_SYS_IDX(id)) = 0x0;
		KUNIT_EXPECT_TRUE(test, vcpu_need_trap_ptrauth(vcpu));

		/* GPI = 1, API = 1 */
		__vcpu_sys_reg(vcpu, IDREG_SYS_IDX(id)) = 0x10000100;
		KUNIT_EXPECT_FALSE(test, vcpu_need_trap_ptrauth(vcpu));

		/* GPI = 1, APA = 1 */
		__vcpu_sys_reg(vcpu, IDREG_SYS_IDX(id)) = 0x10000010;
		KUNIT_EXPECT_FALSE(test, vcpu_need_trap_ptrauth(vcpu));

		/* GPA = 1, API = 1 */
		__vcpu_sys_reg(vcpu, IDREG_SYS_IDX(id)) = 0x01000100;
		KUNIT_EXPECT_FALSE(test, vcpu_need_trap_ptrauth(vcpu));

		/* GPA = 1, APA = 1 */
		__vcpu_sys_reg(vcpu, IDREG_SYS_IDX(id)) = 0x01000010;
		KUNIT_EXPECT_FALSE(test, vcpu_need_trap_ptrauth(vcpu));
	} else {
		KUNIT_EXPECT_FALSE(test, vcpu_need_trap_ptrauth(vcpu));
	}

	test_kvm_vcpu_fini(test, vcpu);
}

static struct kunit_case kvm_sys_regs_test_cases[] = {
	KUNIT_CASE_PARAM(arm64_check_feature_one_test, feature_one_gen_params),
	KUNIT_CASE_PARAM(arm64_check_features_test, features_gen_params),
	KUNIT_CASE_PARAM(vcpu_id_reg_feature_frac_check_test, frac_gen_params),
	KUNIT_CASE_PARAM(validate_id_aa64mmfr0_tgran2_test, tgran4_2_gen_params),
	KUNIT_CASE_PARAM(validate_id_aa64mmfr0_tgran2_test, tgran64_2_gen_params),
	KUNIT_CASE_PARAM(validate_id_aa64mmfr0_tgran2_test, tgran16_2_gen_params),
	KUNIT_CASE(validate_id_aa64pfr0_el1_test),
	KUNIT_CASE(validate_id_aa64pfr1_el1_test),
	KUNIT_CASE(validate_id_aa64isar0_el1_test),
	KUNIT_CASE(validate_id_aa64isar1_el1_test),
	KUNIT_CASE_PARAM(validate_id_aa64mmfr0_el1_test, tgran4_2_gen_params),
	KUNIT_CASE(validate_id_aa64dfr0_el1_test),
	KUNIT_CASE(validate_id_dfr0_el1_test),
	KUNIT_CASE(validate_mvfr1_el1_test),
	KUNIT_CASE(vcpu_need_trap_ptrauth_test),
	KUNIT_CASE(feature_trap_activate_test),
	KUNIT_CASE(id_reg_features_trap_activate_test),
	{}
};

static struct kunit_suite kvm_sys_regs_test_suite = {
	.name = "kvm-sys-regs-test-suite",
	.test_cases = kvm_sys_regs_test_cases,
};

kunit_test_suites(&kvm_sys_regs_test_suite);
MODULE_LICENSE("GPL");
