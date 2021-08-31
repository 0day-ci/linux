// SPDX-License-Identifier: GPL-2.0-only

#include "arm-smmu-v3.h"

struct arm_smmu_device *arm_smmu_v3_impl_init(struct arm_smmu_device *smmu)
{
	/*
	 * Nvidia implementation supports ACPI only, so calling its init()
	 * unconditionally to walk through ACPI tables to probe the device.
	 * It will keep the smmu pointer intact, if it fails.
	 */
	smmu = nvidia_smmu_v3_impl_init(smmu);

	return smmu;
}
