// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 NVIDIA Corporation
 *
 * Author: Dipen Patel <dipenp@nvidia.com>
 */

#ifndef __LINUX_HTE_H
#define __LINUX_HTE_H

#include <linux/errno.h>

struct hte_chip;
struct hte_device;
struct of_phandle_args;
struct device_node;

/**
 * enum hte_dir - HTE edge timestamp direction.
 *
 * @HTE_RISING_EDGE_TS: Timestamps is for rising edge.
 * @HTE_FALLING_EDGE_TS: Timestamps is for falling edge.
 * @HTE_DIR_NOSUPP: Direction is not supported.
 */
enum hte_dir {
	HTE_RISING_EDGE_TS,
	HTE_FALLING_EDGE_TS,
	HTE_DIR_NOSUPP,
};

/**
 * struct hte_ts_data - HTE timestamp data.
 * The provider uses and fills timestamp related details during push_timestamp
 * API call. The consumer uses during retrieve_timestamp API call.
 *
 * @tsc: Timestamp value.
 * @seq: Sequence counter of the timestamps.
 * @dir: Direction of the event at the time of timestamp.
 */
struct hte_ts_data {
	u64 tsc;
	u64 seq;
	enum hte_dir dir;
};

/**
 * struct hte_clk_info - Clock source info that HTE provider uses to timestamp
 * The provider uses hardware clock as a source to timestamp real time. This
 * structure presents the clock information to consumers during
 * hte_get_clk_src_info call.
 *
 * @hz: Supported clock rate in HZ, for example 1KHz clock = 1000.
 * @type: Supported clock type. CLOCK_* types.
 */
struct hte_clk_info {
	u64 hz;
	clockid_t type;
};

/**
 * enum hte_return- HTE subsystem return values used during callback.
 *
 * @HTE_CB_HANDLED: The consumer handled the data successfully.
 * @HTE_RUN_THREADED_CB: The consumer needs further processing, in that case HTE
 * subsystem will invoke kernel thread and call secondary callback provided by
 * the consumer during devm_of_hte_request_ts and hte_req_ts_by_dt_node call.
 * @HTE_CB_TS_DROPPED: The client returns when it can not store ts data.
 * @HTE_CB_ERROR: The client returns error if anything goes wrong.
 */
enum hte_return {
	HTE_CB_HANDLED,
	HTE_RUN_THREADED_CB,
	HTE_CB_TS_DROPPED,
	HTE_CB_ERROR,
};
typedef enum hte_return hte_return_t;

/**
 * typedef hte_ts_cb_t - Callback provided during devm_of_hte_request_ts and
 * hte_req_ts_by_dt_node APIs call.
 *
 * The callback is used to push timestamp data to client.
 * @ts: HW timestamp data.
 * @data: Client supplied data.
 */
typedef hte_return_t (*hte_ts_cb_t)(struct hte_ts_data *ts, void *data);

/**
 * typedef hte_ts_threaded_cb_t - Threaded callback provided during
 * devm_of_hte_request_ts and hte_req_ts_by_dt_node APIs call.
 *
 * @data: Client supplied data.
 *
 * It will be called when client return HTE_RUN_THREADED_CB from hte_ts_cb_t.
 * The callback will be called from thread context.
 *
 */
typedef hte_return_t (*hte_ts_threaded_cb_t)(void *data);

/**
 * struct hte_ts_desc - HTE timestamp descriptor, this structure will be
 * communication token between consumers to subsystem and subsystem to
 * providers.
 *
 * @con_id: This is the same id sent in request APIs.
 * @name: Descriptive name of the entity that is being monitored for the
 * realtime timestamping. The consumer can set any name it likes. If null
 * HTE core will construct name as ts_con_id. It will be the consumer's
 * job to free any allocation if name is set by the consumer.
 * @hte_data: Subsystem's private data relate to requested con_id.
 */
struct hte_ts_desc {
	u32 con_id;
	char *name;
	void *hte_data;
};

/**
 * struct hte_ops - HTE operations set by providers.
 *
 * @request: Hook for requesting a HTE timestamp. Returns 0 on success,
 * non-zero for failures.
 * @release: Hook for releasing a HTE timestamp. Returns 0 on success,
 * non-zero for failures.
 * @enable: Hook to enable the specified timestamp. Returns 0 on success,
 * non-zero for failures.
 * @disable: Hook to disable specified timestamp. Returns 0 on success,
 * non-zero for failures.
 * @get_clk_src_info: Hook to get the clock information the provider uses
 * to timestamp. Returns 0 for success and negative error code for failure. On
 * success HTE subsystem fills up provided struct hte_clk_info.
 *
 * xlated_id parameter is used to communicate between HTE subsystem and the
 * providers. It is the same id returned during xlate API call and translated
 * by the provider. This may be helpful as both subsystem and provider locate
 * the requested entity in constant time, where entity could be anything from
 * lines, signals, events, buses etc.. that providers support.
 */
struct hte_ops {
	int (*request)(struct hte_chip *chip, u32 xlated_id);
	int (*release)(struct hte_chip *chip, u32 xlated_id);
	int (*enable)(struct hte_chip *chip, u32 xlated_id);
	int (*disable)(struct hte_chip *chip, u32 xlated_id);
	int (*get_clk_src_info)(struct hte_chip *chip,
				struct hte_clk_info *ci);
};

/**
 * struct hte_chip - Abstract HTE chip structure.
 * @name: functional name of the HTE IP block.
 * @dev: device providing the HTE.
 * @ops: callbacks for this HTE.
 * @nlines: number of lines/signals supported by this chip.
 * @xlate: Callback which translates consumer supplied logical ids to
 * physical ids, return from 0 for the success and negative for the
 * failures. It stores (0 to @nlines) in xlated_id parameter for the success.
 * @of_hte_n_cells: Number of cells used to form the HTE specifier.
 * @gdev: HTE subsystem abstract device, internal to the HTE subsystem.
 * @data: chip specific private data.
 */
struct hte_chip {
	const char *name;
	struct device *dev;
	const struct hte_ops *ops;
	u32 nlines;
	int (*xlate)(struct hte_chip *gc,
		     const struct of_phandle_args *args,
		     struct hte_ts_desc *desc, u32 *xlated_id);
	u8 of_hte_n_cells;

	struct hte_device *gdev;
	void *data;
};

#if IS_ENABLED(CONFIG_HTE)
/* HTE APIs for the providers */
int devm_hte_register_chip(struct hte_chip *chip);
int hte_push_ts_ns(const struct hte_chip *chip, u32 xlated_id,
		   struct hte_ts_data *data);

/* HTE APIs for the consumers */

int hte_release_ts(struct hte_ts_desc *desc);
int devm_of_hte_request_ts(struct device *dev, struct hte_ts_desc *desc,
			   hte_ts_cb_t cb, hte_ts_threaded_cb_t tcb,
			   void *data);
int hte_req_ts_by_hte_name(struct device *dev, const char *propname,
			   struct hte_ts_desc *desc, hte_ts_cb_t cb,
			   hte_ts_threaded_cb_t tcb, void *data);
int hte_enable_ts(struct hte_ts_desc *desc);
int hte_disable_ts(struct hte_ts_desc *desc);
int hte_get_clk_src_info(const struct hte_ts_desc *desc,
			 struct hte_clk_info *ci);

#else /* !CONFIG_HTE */
static inline int devm_hte_register_chip(struct hte_chip *chip)
{
	return -ENOTSUPP;
}

static inline int hte_push_ts_ns(const struct hte_chip *chip,
				 u32 xlated_id,
				 const struct hte_ts_data *data)
{
	return -ENOTSUPP;
}

static inline int hte_release_ts(struct hte_ts_desc *desc)
{
	return -ENOTSUPP;
}

static inline int devm_of_hte_request_ts(struct device *dev,
					 struct hte_ts_desc *desc,
					 hte_ts_cb_t cb,
					 hte_ts_threaded_cb_t threaded_cb,
					 void *data)
{
	return -ENOTSUPP;
}

static inline int hte_req_ts_by_hte_name(struct device *dev,
					 const char *propname,
					 struct hte_ts_desc *desc,
					 hte_ts_cb_t cb,
					 hte_ts_threaded_cb_t tcb, void *data)
{
	return -ENOTSUPP;
}

static inline int hte_enable_ts(struct hte_ts_desc *desc)
{
	return -ENOTSUPP;
}

static inline int hte_disable_ts(struct hte_ts_desc *desc)
{
	return -ENOTSUPP;
}

static inline int hte_get_clk_src_info(const struct hte_ts_desc *desc,
				       struct hte_clk_info *ci)
{
	return -ENOTSUPP;
}
#endif /* !CONFIG_HTE */

#endif
