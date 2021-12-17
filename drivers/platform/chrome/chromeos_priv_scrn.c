// SPDX-License-Identifier: GPL-2.0

/*
 *  chromeos_priv_scrn.c - ChromeOS Privacy Screen support
 *
 * Copyright (C) 2022 The Chromium OS Authors
 *
 */

#include <linux/acpi.h>
#include <drm/drm_privacy_screen_driver.h>

/*
 * The DSM (Define Specific Method) constants below are the agreed API with
 * the firmware team, on how to control privacy screen using ACPI methods.
 */
#define PRIV_SCRN_DSM_REVID		1	/* DSM version */
#define PRIV_SCRN_DSM_FN_GET_STATUS	1	/* Get privacy screen status */
#define PRIV_SCRN_DSM_FN_ENABLE		2	/* Enable privacy screen */
#define PRIV_SCRN_DSM_FN_DISABLE	3	/* Disable privacy screen */

static const guid_t chromeos_priv_scrn_dsm_guid =
		    GUID_INIT(0xc7033113, 0x8720, 0x4ceb,
			      0x90, 0x90, 0x9d, 0x52, 0xb3, 0xe5, 0x2d, 0x73);

static void
chromeos_priv_scrn_get_hw_state(struct drm_privacy_screen *drm_priv_scrn)
{
	union acpi_object *obj;
	acpi_handle handle;
	struct device *priv_scrn = drm_priv_scrn->dev.parent;

	if (!priv_scrn)
		return;

	handle = acpi_device_handle(to_acpi_device(priv_scrn));
	obj = acpi_evaluate_dsm(handle, &chromeos_priv_scrn_dsm_guid,
				PRIV_SCRN_DSM_REVID,
				PRIV_SCRN_DSM_FN_GET_STATUS, NULL);
	if (!obj) {
		dev_err(priv_scrn, "_DSM failed to get privacy-screen state\n");
		return;
	}

	if (obj->type != ACPI_TYPE_INTEGER)
		dev_err(priv_scrn, "Bad _DSM to get privacy-screen state\n");
	else if (obj->integer.value == 1)
		drm_priv_scrn->hw_state = drm_priv_scrn->sw_state =
			PRIVACY_SCREEN_ENABLED;
	else
		drm_priv_scrn->hw_state = drm_priv_scrn->sw_state =
			PRIVACY_SCREEN_DISABLED;

	ACPI_FREE(obj);
}

static int
chromeos_priv_scrn_set_sw_state(struct drm_privacy_screen *drm_priv_scrn,
				enum drm_privacy_screen_status state)
{
	union acpi_object *obj = NULL;
	acpi_handle handle;
	struct device *priv_scrn = drm_priv_scrn->dev.parent;

	if (!priv_scrn)
		return -ENODEV;

	handle = acpi_device_handle(to_acpi_device(priv_scrn));

	if (state == PRIVACY_SCREEN_DISABLED) {
		obj = acpi_evaluate_dsm(handle,	&chromeos_priv_scrn_dsm_guid,
					PRIV_SCRN_DSM_REVID,
					PRIV_SCRN_DSM_FN_DISABLE, NULL);
	} else if (state == PRIVACY_SCREEN_ENABLED) {
		obj = acpi_evaluate_dsm(handle,	&chromeos_priv_scrn_dsm_guid,
					PRIV_SCRN_DSM_REVID,
					PRIV_SCRN_DSM_FN_ENABLE, NULL);
	} else {
		dev_err(priv_scrn, "Bad attempt to set privacy-screen status\n");
		return -EINVAL;
	}

	if (!obj) {
		dev_err(priv_scrn, "_DSM failed to set privacy-screen state\n");
		return -EIO;
	}

	drm_priv_scrn->hw_state = drm_priv_scrn->sw_state = state;
	ACPI_FREE(obj);
	return 0;
}

static const struct drm_privacy_screen_ops chromeos_priv_scrn_ops = {
	.get_hw_state = chromeos_priv_scrn_get_hw_state,
	.set_sw_state = chromeos_priv_scrn_set_sw_state,
};

static int chromeos_priv_scrn_add(struct acpi_device *adev)
{
	struct drm_privacy_screen *drm_priv_scrn =
		drm_privacy_screen_register(&adev->dev, &chromeos_priv_scrn_ops);

	if (IS_ERR(drm_priv_scrn)) {
		dev_err(&adev->dev, "Error registering privacy-screen\n");
		return PTR_ERR(drm_priv_scrn);
	}

	dev_info(&adev->dev, "registered privacy-screen '%s'\n",
		 dev_name(&drm_priv_scrn->dev));

	return 0;
}

static const struct acpi_device_id chromeos_priv_scrn_device_ids[] = {
	{"GOOG0010", 0}, /* Google's electronic privacy screen for eDP-1 */
	{}
};
MODULE_DEVICE_TABLE(acpi, chromeos_priv_scrn_device_ids);

static struct acpi_driver chromeos_priv_scrn_driver = {
	.name = "chromeos_priv_scrn_drvr",
	.class = "ChromeOS",
	.ids = chromeos_priv_scrn_device_ids,
	.ops = {
		.add = chromeos_priv_scrn_add,
	},
};

module_acpi_driver(chromeos_priv_scrn_driver);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ChromeOS ACPI Privacy Screen driver");
MODULE_AUTHOR("Rajat Jain <rajatja@google.com>");
