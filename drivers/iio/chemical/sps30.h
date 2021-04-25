/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SPS30_H
#define _SPS30_H

struct sps30_state;
struct sps30_ops {
	int (*start_meas)(struct sps30_state *state);
	int (*stop_meas)(struct sps30_state *state);
	int (*read_meas)(struct sps30_state *state, int *meas, int num);
	int (*reset)(struct sps30_state *state);
	int (*clean_fan)(struct sps30_state *state);
	int (*read_cleaning_period)(struct sps30_state *state, int *period);
	int (*write_cleaning_period)(struct sps30_state *state, int period);
	int (*show_info)(struct sps30_state *state);
};

struct sps30_state {
	/* serialize access to the device */
	struct mutex lock;
	struct device *dev;
	int state;
	/*
	 * priv pointer is solely for serdev driver private data. We keep it
	 * here because driver_data inside dev has been already used for iio and
	 * struct serdev_device doesn't have one.
	 */
	void *priv;
	const struct sps30_ops *ops;
};

int sps30_probe(struct device *dev, const char *name, void *priv, const struct sps30_ops *ops);

static inline int sps30_start_meas(struct sps30_state *state)
{
	return state->ops->start_meas(state);
}

static inline int sps30_stop_meas(struct sps30_state *state)
{
	return state->ops->stop_meas(state);
}

static inline int sps30_read_meas(struct sps30_state *state, void *meas, int num)
{
	return state->ops->read_meas(state, meas, num);
}

static inline int sps30_clean_fan(struct sps30_state *state)
{
	return state->ops->clean_fan(state);
}

static inline int sps30_write_cleaning_period(struct sps30_state *state, int period)
{
	return state->ops->write_cleaning_period(state, period);
}

static inline int sps30_read_cleaning_period(struct sps30_state *state, int *period)
{
	return state->ops->read_cleaning_period(state, period);
}

static inline int sps30_show_info(struct sps30_state *state)
{
	return state->ops->show_info(state);
}

static inline int sps30_reset(struct sps30_state *state)
{
	return state->ops->reset(state);
}

#endif
