/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_FIRMWARE_H
#define _LINUX_FIRMWARE_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/gfp.h>

#define FW_ACTION_NOUEVENT 0
#define FW_ACTION_UEVENT 1

struct firmware {
	size_t size;
	const u8 *data;

	/* firmware loader private fields */
	void *priv;
};

/* Update progress codes */
#define FW_UPLOAD_PROG_IDLE		0
#define FW_UPLOAD_PROG_RECEIVING	1
#define FW_UPLOAD_PROG_PREPARING	2
#define FW_UPLOAD_PROG_TRANSFERRING	3
#define FW_UPLOAD_PROG_PROGRAMMING	4
#define FW_UPLOAD_PROG_MAX		5

/* Update error progress codes */
#define FW_UPLOAD_ERR_HW_ERROR		1
#define FW_UPLOAD_ERR_TIMEOUT		2
#define FW_UPLOAD_ERR_CANCELED		3
#define FW_UPLOAD_ERR_BUSY		4
#define FW_UPLOAD_ERR_INVALID_SIZE	5
#define FW_UPLOAD_ERR_RW_ERROR		6
#define FW_UPLOAD_ERR_WEAROUT		7
#define FW_UPLOAD_ERR_MAX		8

struct fw_upload {
	void *dd_handle; /* reference to parent driver */
	void *priv;	 /* firmware loader private fields */
};

/**
 * struct fw_upload_ops - device specific operations to support firmware upload
 * @prepare:		  Required: Prepare secure update
 * @write:		  Required: The write() op receives the remaining
 *			  size to be written and must return the actual
 *			  size written or a negative error code. The write()
 *			  op will be called repeatedly until all data is
 *			  written.
 * @poll_complete:	  Required: Check for the completion of the
 *			  HW authentication/programming process.
 * @cancel:		  Required: Request cancellation of update. This op
 *			  is called from the context of a different kernel
 *			  thread, so race conditions need to be considered.
 * @cleanup:		  Optional: Complements the prepare()
 *			  function and is called at the completion
 *			  of the update, on success or failure, if the
 *			  prepare function succeeded.
 */
struct fw_upload_ops {
	u32 (*prepare)(struct fw_upload *fw_upload, const u8 *data, u32 size);
	s32 (*write)(struct fw_upload *fw_upload, const u8 *data,
		     u32 offset, u32 size);
	u32 (*poll_complete)(struct fw_upload *fw_upload);
	void (*cancel)(struct fw_upload *fw_upload);
	void (*cleanup)(struct fw_upload *fw_upload);
};

struct module;
struct device;

/*
 * Built-in firmware functionality is only available if FW_LOADER=y, but not
 * FW_LOADER=m
 */
#ifdef CONFIG_FW_LOADER
bool firmware_request_builtin(struct firmware *fw, const char *name);
#else
static inline bool firmware_request_builtin(struct firmware *fw,
					    const char *name)
{
	return false;
}
#endif

#if defined(CONFIG_FW_LOADER) || (defined(CONFIG_FW_LOADER_MODULE) && defined(MODULE))
int request_firmware(const struct firmware **fw, const char *name,
		     struct device *device);
int firmware_request_nowarn(const struct firmware **fw, const char *name,
			    struct device *device);
int firmware_request_platform(const struct firmware **fw, const char *name,
			      struct device *device);
int request_firmware_nowait(
	struct module *module, bool uevent,
	const char *name, struct device *device, gfp_t gfp, void *context,
	void (*cont)(const struct firmware *fw, void *context));
int request_firmware_direct(const struct firmware **fw, const char *name,
			    struct device *device);
int request_firmware_into_buf(const struct firmware **firmware_p,
	const char *name, struct device *device, void *buf, size_t size);
int request_partial_firmware_into_buf(const struct firmware **firmware_p,
				      const char *name, struct device *device,
				      void *buf, size_t size, size_t offset);

void release_firmware(const struct firmware *fw);
#else
static inline int request_firmware(const struct firmware **fw,
				   const char *name,
				   struct device *device)
{
	return -EINVAL;
}

static inline int firmware_request_nowarn(const struct firmware **fw,
					  const char *name,
					  struct device *device)
{
	return -EINVAL;
}

static inline int firmware_request_platform(const struct firmware **fw,
					    const char *name,
					    struct device *device)
{
	return -EINVAL;
}

static inline int request_firmware_nowait(
	struct module *module, bool uevent,
	const char *name, struct device *device, gfp_t gfp, void *context,
	void (*cont)(const struct firmware *fw, void *context))
{
	return -EINVAL;
}

static inline void release_firmware(const struct firmware *fw)
{
}

static inline int request_firmware_direct(const struct firmware **fw,
					  const char *name,
					  struct device *device)
{
	return -EINVAL;
}

static inline int request_firmware_into_buf(const struct firmware **firmware_p,
	const char *name, struct device *device, void *buf, size_t size)
{
	return -EINVAL;
}

static inline int request_partial_firmware_into_buf
					(const struct firmware **firmware_p,
					 const char *name,
					 struct device *device,
					 void *buf, size_t size, size_t offset)
{
	return -EINVAL;
}

#endif

#ifdef CONFIG_FW_UPLOAD

struct fw_upload *
fw_upload_register(struct device *parent, const char *name,
		   const struct fw_upload_ops *ops, void *dd_handle);
void fw_upload_unregister(struct fw_upload *fw_upload);

#else

static inline struct fw_upload *
fw_upload_register(struct device *parent, const char *name,
		   const struct fw_upload_ops *ops, void *dd_handle)
{
		return ERR_PTR(-EINVAL);
}

static inline void fw_upload_unregister(struct fw_upload *fw_upload)
{
}

#endif

int firmware_request_cache(struct device *device, const char *name);

#endif
