/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_ACPI_H__
#define __INTEL_ACPI_H__

struct pci_dev;
struct drm_i915_private;

#ifdef CONFIG_ACPI
void intel_register_dsm_handler(void);
void intel_unregister_dsm_handler(void);
void intel_bxt_dsm_detect(struct pci_dev *pdev);
void intel_acpi_device_id_update(struct drm_i915_private *i915);
#else
static inline void intel_register_dsm_handler(void) { return; }
static inline void intel_unregister_dsm_handler(void) { return; }
static inline void intel_bxt_dsm_detect(struct pci_dev *pdev) { return; }
static inline
void intel_acpi_device_id_update(struct drm_i915_private *i915) { return; }
#endif /* CONFIG_ACPI */

#endif /* __INTEL_ACPI_H__ */
