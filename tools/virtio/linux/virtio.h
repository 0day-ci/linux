/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_VIRTIO_H
#define LINUX_VIRTIO_H
#include <linux/scatterlist.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>

#define VIRTIO_CONFIG_S_DRIVER	2

typedef struct pm_message {
	int event;
} pm_message_t;

enum probe_type {
	PROBE_DEFAULT_STRATEGY,
	PROBE_PREFER_ASYNCHRONOUS,
	PROBE_FORCE_SYNCHRONOUS,
};

struct device_driver {
	const char	*name;
	void		*bus;

	void		*owner;
	const char	*mod_name;	/* used for built-in modules */

	bool suppress_bind_attrs;	/* disables bind/unbind via sysfs */
	enum probe_type probe_type;

	const void	*of_match_table;
	const void	*acpi_match_table;

	int (*probe)(void *dev);
	void (*sync_state)(void *dev);
	int (*remove)(void *dev);
	void (*shutdown)(void *dev);
	int (*suspend)(void *dev, pm_message_t state);
	int (*resume)(void *dev);
	const void	**groups;
	const void	**dev_groups;

	const void	*pm;
	void (*coredump)(void *dev);

	struct driver_private *p;
};

struct device {
	void *parent;
	struct device_driver *driver;
};

struct virtio_config_ops;

struct virtio_device {
	struct device dev;
	u64 features;
	const struct virtio_config_ops *config;
	struct list_head vqs;
	spinlock_t vqs_list_lock;
};

struct virtqueue {
	struct list_head list;
	void (*callback)(struct virtqueue *vq);
	const char *name;
	struct virtio_device *vdev;
        unsigned int index;
        unsigned int num_free;
	void *priv;
};

struct virtio_config_ops {
	void (*enable_cbs)(struct virtio_device *vdev);
	void (*get)(struct virtio_device *vdev, unsigned int offset,
		    void *buf, unsigned int len);
	void (*set)(struct virtio_device *vdev, unsigned int offset,
		    const void *buf, unsigned int len);
	u32 (*generation)(struct virtio_device *vdev);
	u8 (*get_status)(struct virtio_device *vdev);
	void (*set_status)(struct virtio_device *vdev, u8 status);
	void (*reset)(struct virtio_device *vdev);
	int (*find_vqs)(struct virtio_device *vdev, unsigned int nvqs,
			struct virtqueue *vqs[], void *callbacks[],
			const char * const names[], const bool *ctx,
			void *desc);
	void (*del_vqs)(struct virtio_device *vdev);
	u64 (*get_features)(struct virtio_device *vdev);
	int (*finalize_features)(struct virtio_device *vdev);
	const char *(*bus_name)(struct virtio_device *vdev);
	int (*set_vq_affinity)(struct virtqueue *vq,
			       const void *cpu_mask);
	const void *(*get_vq_affinity)(struct virtio_device *vdev,
			int index);
	bool (*get_shm_region)(struct virtio_device *vdev,
			       void *region, u8 id);
};

struct virtio_driver {
	struct device_driver driver;
	const struct virtio_device_id *id_table;
	const unsigned int *feature_table;
	unsigned int feature_table_size;
	const unsigned int *feature_table_legacy;
	unsigned int feature_table_size_legacy;
	bool suppress_used_validation;
	int (*validate)(struct virtio_device *dev);
	int (*probe)(struct virtio_device *dev);
	void (*scan)(struct virtio_device *dev);
	void (*remove)(struct virtio_device *dev);
	void (*config_changed)(struct virtio_device *dev);
#ifdef CONFIG_PM
	int (*freeze)(struct virtio_device *dev);
	int (*restore)(struct virtio_device *dev);
#endif
};

/* Interfaces exported by virtio_ring. */
int virtqueue_add_sgs(struct virtqueue *vq,
		      struct scatterlist *sgs[],
		      unsigned int out_sgs,
		      unsigned int in_sgs,
		      void *data,
		      gfp_t gfp);

int virtqueue_add_outbuf(struct virtqueue *vq,
			 struct scatterlist sg[], unsigned int num,
			 void *data,
			 gfp_t gfp);

int virtqueue_add_inbuf(struct virtqueue *vq,
			struct scatterlist sg[], unsigned int num,
			void *data,
			gfp_t gfp);

bool virtqueue_kick(struct virtqueue *vq);

void *virtqueue_get_buf(struct virtqueue *vq, unsigned int *len);

void virtqueue_disable_cb(struct virtqueue *vq);

bool virtqueue_enable_cb(struct virtqueue *vq);
bool virtqueue_enable_cb_delayed(struct virtqueue *vq);

int virtqueue_use_wrap_counter(struct virtqueue *vq);

void *virtqueue_detach_unused_buf(struct virtqueue *vq);
struct virtqueue *vring_new_virtqueue(unsigned int index,
				      unsigned int num,
				      unsigned int vring_align,
				      struct virtio_device *vdev,
				      bool weak_barriers,
				      bool ctx,
				      void *pages,
				      bool (*notify)(struct virtqueue *vq),
				      void (*callback)(struct virtqueue *vq),
				      const char *name);
void vring_del_virtqueue(struct virtqueue *vq);

static inline struct virtio_driver *drv_to_virtio(struct device_driver *drv)
{
	return container_of(drv, struct virtio_driver, driver);
}

#endif
