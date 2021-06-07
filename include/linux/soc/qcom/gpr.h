/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __QCOM_GPR_H_
#define __QCOM_GPR_H_

#include <linux/spinlock.h>
#include <linux/device.h>
#include <dt-bindings/soc/qcom,gpr.h>

#define APM_MODULE_INSTANCE_ID		GPR_APM_MODULE_IID
#define PRM_MODULE_INSTANCE_ID		GPR_PRM_MODULE_IID
#define AMDB_MODULE_INSTANCE_ID		GPR_AMDB_MODULE_IID
#define VCPM_MODULE_INSTANCE_ID		GPR_VCPM_MODULE_IID

struct gpr_hdr {
	uint32_t version:4;
	uint32_t hdr_size:4;
	uint32_t pkt_size:24;
	uint32_t dest_domain:8;
	uint32_t src_domain:8;
	uint32_t reserved:16;
	uint32_t src_port;
	uint32_t dest_port;
	uint32_t token;
	uint32_t opcode;
} __packed;

struct gpr_pkt {
	struct gpr_hdr hdr;
	uint32_t payload[0];
};

struct gpr_resp_pkt {
	struct gpr_hdr hdr;
	void *payload;
	int payload_size;
};

#define GPR_HDR_SIZE sizeof(struct gpr_hdr)
#define GPR_PKT_VER	0x0
#define GPR_PKT_HEADER_WORD_SIZE	((sizeof(struct gpr_pkt) + 3) >> 2)
#define GPR_PKT_HEADER_BYTE_SIZE	(GPR_PKT_HEADER_WORD_SIZE << 2)
#define GPR_DOMAIN_ID_MODEM	1
#define GPR_DOMAIN_ID_ADSP	2
#define GPR_DOMAIN_ID_APPS	3

#define GPR_BASIC_RSP_RESULT 0x02001005
struct gpr_ibasic_rsp_result_t {
	uint32_t opcode;
	uint32_t status;
};

#define GPR_BASIC_EVT_ACCEPTED 0x02001006
struct gpr_ibasic_rsp_accepted_t {
	uint32_t opcode;
};

extern struct bus_type gprbus;
typedef int (*gpr_port_cb) (struct gpr_resp_pkt *d, void *priv, int op);

struct gpr_port {
	struct device *dev;
	gpr_port_cb callback;
	struct gpr *gpr;
	spinlock_t lock;
	int id;
	void *priv;
};

#define GPR_NAME_SIZE	128
struct gpr_device {
	struct device	dev;
	uint16_t	port_id;
	uint16_t	domain_id;
	uint32_t	version;
	char name[GPR_NAME_SIZE];
	struct gpr_port port;
};

#define to_gpr_device(d) container_of(d, struct gpr_device, dev)

struct gpr_driver {
	int	(*probe)(struct gpr_device *sl);
	int	(*remove)(struct gpr_device *sl);
	int (*callback)(struct gpr_resp_pkt *d, void *data, int op);
	struct device_driver		driver;
};

#define to_gpr_driver(d) container_of(d, struct gpr_driver, driver)

/*
 * use a macro to avoid include chaining to get THIS_MODULE
 */
#define gpr_driver_register(drv) __gpr_driver_register(drv, THIS_MODULE)

int __gpr_driver_register(struct gpr_driver *drv, struct module *owner);
void gpr_driver_unregister(struct gpr_driver *drv);

/**
 * module_gpr_driver() - Helper macro for registering a aprbus driver
 * @__aprbus_driver: aprbus_driver struct
 *
 * Helper macro for aprbus drivers which do not do anything special in
 * module init/exit. This eliminates a lot of boilerplate. Each module
 * may only use this macro once, and calling it replaces module_init()
 * and module_exit()
 */
#define module_gpr_driver(__gpr_driver) \
	module_driver(__gpr_driver, gpr_driver_register, \
			gpr_driver_unregister)

struct gpr_port *gpr_alloc_port(struct gpr_device *gdev, struct device *dev,
				gpr_port_cb cb, void *priv);
void gpr_free_port(struct gpr_port *port);

struct gpr_pkt *gpr_alloc_pkt(struct gpr_port *port, int payload_size,
			      uint32_t opcode, uint32_t token,
			      uint32_t dest_port);
void gpr_free_pkt(struct gpr_port *port, struct gpr_pkt *pkt);

int gpr_send_port_pkt(struct gpr_port *port, struct gpr_pkt *pkt);
static inline int gpr_send_pkt(struct gpr_device *gdev, struct gpr_pkt *pkt)
{
	return gpr_send_port_pkt(&gdev->port, pkt);
}

#endif /* __QCOM_GPR_H_ */
