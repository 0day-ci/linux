// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 NVIDIA Corporation
 *
 * Author: Dipen Patel <dipenp@nvidia.com>
 */

#ifndef __LINUX_HTE_H
#define __LINUX_HTE_H

struct hte_chip;
struct hte_device;
struct of_phandle_args;

/**
 * Used by providers to indicate the direction of the timestamp.
 */
#define HTE_EVENT_RISING_EDGE          0x1
#define HTE_EVENT_FALLING_EDGE         0x2

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
	int dir;
};

/**
 * struct hte_clk_info - Clock source info that HTE provider uses.
 * The provider uses hardware clock as a source to timestamp real time. This
 * structure presents the clock information to consumers. 
 *
 * @hz: Clock rate in HZ, for example 1KHz clock = 1000.
 * @type: Clock type. CLOCK_* types.
 */
struct hte_clk_info {
	u64 hz;
	clockid_t type;
};

/**
 * HTE subsystem notifications for the consumers.
 *
 * @HTE_TS_AVAIL: Timestamps available notification.
 * @HTE_TS_DROPPED: Timestamps dropped notification.
 */
enum hte_notify {
	HTE_TS_AVAIL = 1,
	HTE_TS_DROPPED,
	HTE_NUM_NOTIFIER,
};

/**
 * struct hte_ts_desc - HTE timestamp descriptor, this structure will be
 * communication token between consumers to subsystem and subsystem to
 * providers.
 *
 * @con_id: This is the same id sent in request APIs.
 * @name: Descriptive name of the entity that is being monitored for the
 * realtime timestamping.
 * @data_subsys: Subsystem's private data relate to requested con_id.
 */
struct hte_ts_desc {
	u32 con_id;
	char *name;
	void *data_subsys;
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
 * @get_clk_src_info: Optional hook to get the clock information provider uses
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

	/* only used internally by the HTE framework */
	struct hte_device *gdev;
	void *data;
};

#if IS_ENABLED(CONFIG_HTE)
/* HTE APIs for the providers */
int hte_register_chip(struct hte_chip *chip);
int hte_unregister_chip(struct hte_chip *chip);
int hte_push_ts_ns_atomic(const struct hte_chip *chip, u32 xlated_id,
			  struct hte_ts_data *data, size_t n);

/* HTE APIs for the consumers */

int hte_release_ts(struct hte_ts_desc *desc);
struct hte_ts_desc *of_hte_request_ts(struct device *dev, const char *label,
				      void (*cb)(enum hte_notify n));

struct hte_ts_desc *devm_of_hte_request_ts(struct device *dev,
					   const char *label,
					   void (*cb)(enum hte_notify n));
struct hte_ts_desc *hte_req_ts_by_dt_node(struct device_node *of_node,
					  unsigned int id,
					  void (*cb)(enum hte_notify n));
int devm_hte_release_ts(struct device *dev, struct hte_ts_desc *desc);
int hte_retrieve_ts_ns(const struct hte_ts_desc *desc, struct hte_ts_data *el,
		       size_t n);
int hte_retrieve_ts_ns_wait(const struct hte_ts_desc *desc,
			    struct hte_ts_data *el, size_t n);
int hte_set_buf_len(const struct hte_ts_desc *desc, size_t len);
size_t hte_get_buf_len(const struct hte_ts_desc *desc);
int hte_set_buf_watermark(const struct hte_ts_desc *desc, size_t val);
size_t hte_get_buf_watermark(const struct hte_ts_desc *desc);
size_t hte_available_ts(const struct hte_ts_desc *desc);
int hte_enable_ts(struct hte_ts_desc *desc);
int hte_disable_ts(struct hte_ts_desc *desc);
int hte_get_clk_src_info(const struct hte_ts_desc *desc,
			 struct hte_clk_info *ci);

#else /* !CONFIG_HTE */
static inline int hte_register_chip(struct hte_chip *chip)
{
	return -ENOTSUPP;
}

static inline int hte_unregister_chip(struct hte_chip *chip)
{
	return -ENOTSUPP;
}

static inline int hte_push_ts_ns_atomic(const struct hte_chip *chip,
					u32 xlated_id,
					const struct hte_ts_data *data,
					size_t n)
{
	return -ENOTSUPP;
}

static inline int hte_release_ts(struct hte_ts_desc *desc)
{
	return -ENOTSUPP;
}

static inline
struct hte_ts_desc *of_hte_request_ts(struct device *dev, const char *label,
				      void (*cb)(enum hte_notify ac))
{
	return ERR_PTR(-ENOTSUPP);
}

static inline
struct hte_ts_desc *devm_of_hte_request_ts(struct device *dev,
					   const char *label,
					   void (*cb)(enum hte_notify ac))
{
	return ERR_PTR(-ENOTSUPP);
}

static inline int devm_hte_release_ts(struct device *dev,
				      struct hte_ts_desc *desc)
{
	return -ENOTSUPP;
}

static inline int hte_retrieve_ts_ns(const struct hte_ts_desc *desc,
				     struct hte_ts_data *el, size_t n)
{
	return -ENOTSUPP;
}

static inline int hte_retrieve_ts_ns_wait(const struct hte_ts_desc *desc,
					  struct hte_ts_data *el, size_t n)
{
	return -ENOTSUPP;
}

static inline int hte_set_buf_len(const struct hte_ts_desc *desc,
				  size_t len)
{
	return -ENOTSUPP;
}

static inline size_t hte_get_buf_len(const struct hte_ts_desc *desc)
{
	return 0;
}

static inline int hte_set_buf_watermark(const struct hte_ts_desc *desc,
					size_t val)
{
	return -ENOTSUPP;
}

static inline size_t hte_get_buf_watermark(const struct hte_ts_desc *desc)
{
	return 0;
}

static inline size_t hte_available_ts(const struct hte_ts_desc *desc)
{
	return 0;
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

static inline
struct hte_ts_desc *hte_req_ts_by_dt_node(struct device_node *of_node,
					  unsigned int id,
					  void (*cb)(enum hte_notify n))
{
	return ERR_PTR(-ENOTSUPP);
}
#endif /* !CONFIG_HTE */

#endif
