// SPDX-License-Identifier: GPL-2.0-only
/*
 * AVM WASP Remote Processor driver
 *
 * Copyright (c) 2019-2020 Andreas Böhler
 * Copyright (c) 2021-2022 Daniel Kestrel
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_mdio.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/remoteproc.h>
#include <linux/timekeeping.h>
#include <net/sock.h>
#include <asm-generic/gpio.h>

#include "remoteproc_internal.h"

#define WASP_CHUNK_SIZE			14
#define WASP_ADDR			0x07
#define WASP_TIMEOUT_COUNT		1000
#define WASP_WAIT_TIMEOUT_COUNT		20
#define WASP_CHECK_LEN_DIVBY4_MASK	0x3

#define WASP_WRITE_SLEEP_US		20000
#define WASP_WAIT_SLEEP_US_LOW		50000
#define WASP_WAIT_SLEEP_US		100000
#define WASP_POLL_SLEEP_US		200

#define WASP_RESP_RETRY			0x0102
#define WASP_RESP_OK			0x0002
#define WASP_RESP_WAIT			0x0401
#define WASP_RESP_COMPLETED		0x0000
#define WASP_RESP_READY_TO_START	0x0202
#define WASP_RESP_STARTING		0x00c9

#define WASP_CMD_SET_PARAMS		0x0c01
#define WASP_CMD_SET_CHECKSUM_3390	0x0801
#define WASP_CMD_SET_CHECKSUM_X490	0x0401
#define WASP_CMD_SET_DATA		0x0e01
#define WASP_CMD_START_FIRMWARE_3390	0x0201
#define WASP_CMD_START_FIRMWARE_X490	0x0001
#define WASP_CMD_START_FIRMWARE2_X490	0x0101

static const u32 start_addr = 0xbd003000;
static const u32 exec_addr = 0xbd003000;

static const char mac_data[WASP_CHUNK_SIZE] = {0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
		0xaa, 0x04, 0x20, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00};

enum {
	MODEL_3390,
	MODEL_X490,
	MODEL_UNKNOWN
} m_model = MODEL_UNKNOWN;

#define ETH_TYPE_ATH_ECPS_FRAME		0x88bd
#define ETH_BUF_SIZE			1056
#define ETH_SEND_LOOP_TIMEOUT_SECS	60
#define ETH_MAX_DATA_SIZE		1028
#define ETH_DATA_SIZE			1024
#define ETH_WASP_PACKET_ID		0x1200

#define CMD_FIRMWARE_DATA		0x0104
#define CMD_START_FIRMWARE		0xd400

#define RESP_DISCOVER			0x0000
#define RESP_OK				0x0100
#define RESP_STARTING			0x0200
#define RESP_ERROR			0x0300

static const u32 m_load_addr = 0x81a00000;

static char wasp_mac[] = {0x00, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa};

struct wasp_packet {
	struct ethhdr eh;
	struct __packed {
		u16	packet_start;
		u8	pad_one[5];
		u16	command;
		u16	response;
		u16	counter;
		u8	pad_two;
	} hdr;
	u8	payload[ETH_MAX_DATA_SIZE];
} __packed;

static char *firmware = "ath9k-eeprom-ahb-18100000.wmac.bin";
module_param(firmware, charp, 0444);
MODULE_PARM_DESC(firmware,
		 "Filename of the ath9k eeprom to be loaded");

static char *caldata = "ath10k/cal-pci-0000:00:00.0.bin";
module_param(caldata, charp, 0444);
MODULE_PARM_DESC(caldata,
		 "Filename of the ath10k caldata to be loaded");

static char *netboot = "netboot.fw";
module_param(netboot, charp, 0444);
MODULE_PARM_DESC(netboot,
		 "Filename of the network boot firmware for WASP");

static char *image = "wasp-image.bin";
module_param(image, charp, 0444);
MODULE_PARM_DESC(image,
		 "Filename of the linux image to be loaded to WASP");

/**
 * struct avm_wasp_rproc - avmwasp remote processor priv
 * @rproc: rproc handle
 * @pdev: pointer to platform device
 * @fw_blob: pointer to load and save any firmware
 * @linux_blob: pointer to access initramfs image
 * @mdio_bus: pointer to mii_bus of gswip device for gpio
 * @startup_gpio: store WASP startup gpio descriptor
 * @reset_gpio: store WASP reset gpio descriptor
 * @loader_port: store name of the port wasp is connected to
 * @buffer: recv buffer for feedback from WASP
 * @ifindex: interface index used for WASP communication
 */
struct avm_wasp_rproc {
	struct rproc *rproc;
	struct platform_device *pdev;
	const struct firmware *fw_blob, *linux_blob;
	struct mii_bus *mdio_bus;
	struct gpio_desc *startup_gpio, *reset_gpio;
	char *loader_port;
	char buffer[ETH_BUF_SIZE];
	int ifindex;
};

/**
 * avm_wasp_netboot_mdio_write_u32_split() - write 32bit value
 * @avmwasp: pointer to drivers private avm_wasp_rproc structure
 * @mdio_reg: register start value for two mdio registers to write to
 * @value: value to be written to the register
 *
 * As the mdio registers are 16bit, this function writes a 32bit value
 * to two subsequent mdio registers starting with the specified register
 * for the mdio address that is used for the connection to the WASP SoC
 *
 * Return: 0 on Success, -ETIMEDOUT if avm_wasp_netboot_mdio_write fails
 */
static int avm_wasp_netboot_mdio_write_u32_split(struct avm_wasp_rproc *avmwasp,
						 u32 mdio_reg, const u32 value)
{
	struct device *dev = &avmwasp->pdev->dev;
	int ret;

	ret = mdiobus_write(avmwasp->mdio_bus, WASP_ADDR, mdio_reg,
			    ((value & 0xffff0000) >> 16));
	if (ret < 0)
		goto err;

	ret = mdiobus_write(avmwasp->mdio_bus, WASP_ADDR, mdio_reg + 2,
			    (value & 0x0000ffff));
	if (ret < 0)
		goto err;

	return 0;
err:
	dev_err(dev, "mdio split write failed\n");
	return ret;
}

/**
 * avm_wasp_read_poll_timeout() - wrap read_poll_timeout_macro
 * @avmwasp: pointer to drivers private avm_wasp_rproc structure
 * @udelay: microseconds to wait after read
 * @utimeout: timeout value in microseconds
 * @checkval: value to be checked against
 *
 * This function checks checkval against the WASP mdio status register,
 * waits udelay before repeating the read and times out after utimeout.
 * Separate function because every other use of read_poll_timeout makes
 * the kernel module around half a kB larger.
 *
 * Return: 0 when checkval was read or -ETIMEDOUT if not
 */
static int avm_wasp_read_poll_timeout(struct avm_wasp_rproc *avmwasp,
				      u32 udelay, u32 utimeout, u32 checkval)
{
	u32 val;

	return read_poll_timeout(mdiobus_read, val,
				 (val == checkval), udelay, utimeout, false,
				 avmwasp->mdio_bus, WASP_ADDR, 0x0);
}

/**
 * avm_wasp_netboot_write_header() - write header to WASP
 * @avmwasp: pointer to drivers private avm_wasp_rproc structure
 * @start_addr: address where to load the firmware to on WASP
 * @len: length of the network boot firmware
 * @exec_addr: address where to start execution on WASP
 *
 * Writes the header to WASP using mdio to initiate the start of
 * transferring the network boot firmware to WASP
 *
 * Return: 0 on Success, -ETIMEDOUT if writing header failed based on return
 * code from WASP or the write methods
 */
static int avm_wasp_netboot_write_header(struct avm_wasp_rproc *avmwasp,
					 const u32 start_addr, const u32 len,
					 const u32 exec_addr)
{
	struct device *dev = &avmwasp->pdev->dev;
	int ret;

	ret = avm_wasp_netboot_mdio_write_u32_split(avmwasp, 0x2, start_addr);
	if (ret < 0)
		goto err;

	ret = avm_wasp_netboot_mdio_write_u32_split(avmwasp, 0x6, len);
	if (ret < 0)
		goto err;

	ret = avm_wasp_netboot_mdio_write_u32_split(avmwasp, 0xA, exec_addr);
	if (ret < 0)
		goto err;

	ret = mdiobus_write(avmwasp->mdio_bus, WASP_ADDR, 0x0,
			    WASP_CMD_SET_PARAMS);
	if (ret < 0)
		goto err;

	ret = avm_wasp_read_poll_timeout(avmwasp, WASP_POLL_SLEEP_US,
					 WASP_TIMEOUT_COUNT * WASP_POLL_SLEEP_US,
					 WASP_RESP_OK);
	if (ret < 0)
		goto err;

	return 0;
err:
	dev_err(dev, "mdio write for header failed\n");
	return ret;
}

/**
 * avm_wasp_netboot_write_checksum() - write checksum to WASP
 * @avmwasp: pointer to drivers private avm_wasp_rproc structure
 * @checksum: calculated checksum value to be sent to WASP
 *
 * Writes the calculated checksum for the given network boot firmware
 * to WASP using mdio as the second step
 *
 * Return: 0 on Success, -ETIMEDOUT if writing checksum failed based on
 * return code from WASP or the write methods
 */
static int avm_wasp_netboot_write_checksum(struct avm_wasp_rproc *avmwasp,
					   const u32 checksum)
{
	struct device *dev = &avmwasp->pdev->dev;
	int ret;

	ret = avm_wasp_netboot_mdio_write_u32_split(avmwasp, 0x2, checksum);
	if (ret < 0)
		goto err;

	if (m_model == MODEL_3390) {
		ret = avm_wasp_netboot_mdio_write_u32_split(avmwasp, 0x6, 0x0000);
		if (ret < 0)
			goto err;

		ret = mdiobus_write(avmwasp->mdio_bus, WASP_ADDR, 0x0,
				    WASP_CMD_SET_CHECKSUM_3390);
	} else if (m_model == MODEL_X490) {
		ret = mdiobus_write(avmwasp->mdio_bus, WASP_ADDR, 0x0,
				    WASP_CMD_SET_CHECKSUM_X490);
	}
	if (ret < 0)
		goto err;

	ret = avm_wasp_read_poll_timeout(avmwasp, WASP_POLL_SLEEP_US,
					 WASP_TIMEOUT_COUNT * WASP_POLL_SLEEP_US,
					 WASP_RESP_OK);
	if (ret < 0)
		goto err;

	return 0;
err:
	dev_err(dev, "mdio write for checksum failed\n");
	return ret;
}

/**
 * avm_wasp_netboot_write_chunk() - write chunk of data to WASP
 * @avmwasp: pointer to drivers private avm_wasp_rproc structure
 * @data: pointer to data
 * @len: length of data (should not exceed 14 bytes)
 *
 * Writes up to 14 bytes of data into the 7 16bit mdio registers
 * to WASP using mdio
 *
 * Return: 0 on Success, -EFAULT if data length is more than 14 bytes or
 * -ETIMEDOUT if writing the data failed based on return code from WASP
 * or the write methods
 */
static int avm_wasp_netboot_write_chunk(struct avm_wasp_rproc *avmwasp,
					const char *data, size_t len)
{
	struct device *dev = &avmwasp->pdev->dev;
	int i, ret;

	if (len > WASP_CHUNK_SIZE || !data)
		return -EFAULT;

	for (i = 0; i < len && ret >= 0; i += 2)
		ret = mdiobus_write(avmwasp->mdio_bus, WASP_ADDR, i + 2,
				    *((u16 *)(data + i)));
	if (ret < 0)
		goto err;

	ret = mdiobus_write(avmwasp->mdio_bus, WASP_ADDR, 0x0, WASP_CMD_SET_DATA);
	if (ret < 0)
		goto err;

	ret = avm_wasp_read_poll_timeout(avmwasp, WASP_POLL_SLEEP_US,
					 WASP_TIMEOUT_COUNT * WASP_POLL_SLEEP_US,
					 WASP_RESP_OK);
	if (ret < 0)
		goto err;

	return 0;
err:
	dev_err(dev, "mdio write for data chunk failed\n");
	return ret;
}

/**
 * avm_wasp_netboot_calc_checksum() - calculate netboot firmware checksum
 * @avmwasp: pointer to drivers private avm_wasp_rproc structure
 *
 * Calculates the checksum by using the firmware in fw_blob from the private
 * avm_wasp_rproc structure
 *
 * Return: Calculated checksum
 */
static u32 avm_wasp_netboot_calc_checksum(struct avm_wasp_rproc *avmwasp)
{
	u32 checksum = U32_MAX, count = U32_MAX, cs;
	const u8 *firmware, *firmware_end;

	firmware = avmwasp->fw_blob->data;
	firmware_end = firmware + avmwasp->fw_blob->size;

	while (firmware < firmware_end) {
		cs = be32_to_cpu(*(u32 *)firmware);
		checksum = checksum - cs;
		count++;
		firmware += 4;
	}

	checksum = checksum - count;
	return checksum;
}

/**
 * avm_wasp_netboot_load_firmware() - load netboot firmware to WASP
 * @avmwasp: pointer to drivers private avm_wasp_rproc structure
 *
 * Implements the process to send header, checksum and the firmware
 * blob in 14 byte chunks to the WASP processor using mdio
 * Includes checks between the steps and sending commands to start
 * the network boot firmware
 *
 * Return: 0 on Success, -ENODEV if WASP not ready after reset,
 * -ETIMEDOUT if there was a timeout on polling or
 * -EFAULT if other errors have occurred
 */
static int avm_wasp_netboot_load_firmware(struct avm_wasp_rproc *avmwasp)
{
	struct device *dev = &avmwasp->pdev->dev;
	struct mii_bus *mdio_bus = avmwasp->mdio_bus;
	const u8 *firmware, *firmware_end;
	int regval, regval2, ret, cont = 1;
	u32 checksum, left;

	ret = avm_wasp_read_poll_timeout(avmwasp, WASP_WAIT_SLEEP_US,
					 WASP_WAIT_TIMEOUT_COUNT *
					 WASP_WAIT_SLEEP_US, WASP_RESP_OK);
	if (ret) {
		dev_err(dev, "error WASP processor not in ready status\n");
		return -ENODEV;
	}

	firmware = avmwasp->fw_blob->data;
	firmware_end = firmware + avmwasp->fw_blob->size;

	if ((avmwasp->fw_blob->size & WASP_CHECK_LEN_DIVBY4_MASK) ||
	    avmwasp->fw_blob->size > U16_MAX) {
		dev_err(dev, "error network boot firmware size\n");
		return -EFAULT;
	}

	ret = avm_wasp_netboot_write_header(avmwasp, start_addr,
					    avmwasp->fw_blob->size,
					    exec_addr);
	if (ret < 0)
		return ret;

	checksum = avm_wasp_netboot_calc_checksum(avmwasp);
	ret = avm_wasp_netboot_write_checksum(avmwasp, checksum);

	if (ret < 0)
		return ret;

	while (firmware < firmware_end) {
		left = firmware_end - firmware;
		if (left > WASP_CHUNK_SIZE)
			left = WASP_CHUNK_SIZE;
		ret = avm_wasp_netboot_write_chunk(avmwasp, firmware, left);
		if (ret < 0)
			return ret;

		firmware += left;
	}

	usleep_range(WASP_WAIT_SLEEP_US_LOW, WASP_WAIT_SLEEP_US);

	if (m_model == MODEL_3390)
		ret = mdiobus_write(mdio_bus, WASP_ADDR, 0x0,
				    WASP_CMD_START_FIRMWARE_3390);
	else if (m_model == MODEL_X490)
		ret = mdiobus_write(mdio_bus, WASP_ADDR, 0x0,
				    WASP_CMD_START_FIRMWARE_X490);
	if (ret < 0) {
		dev_err(dev, "writing command failed\n");
		return ret;
	}

	usleep_range(WASP_WAIT_SLEEP_US_LOW, WASP_WAIT_SLEEP_US);

	ret = avm_wasp_read_poll_timeout(avmwasp, WASP_WAIT_SLEEP_US,
					 WASP_WAIT_TIMEOUT_COUNT *
					 WASP_WAIT_SLEEP_US,
					 WASP_RESP_READY_TO_START);
	if (ret) {
		dev_err(dev, "timed out waiting for WASP ready to start\n");
		return ret;
	}

	if (m_model == MODEL_3390)
		ret = mdiobus_write(mdio_bus, WASP_ADDR, 0x0,
				    WASP_CMD_START_FIRMWARE_3390);
	else if (m_model == MODEL_X490)
		ret = mdiobus_write(mdio_bus, WASP_ADDR, 0x0,
				    WASP_CMD_SET_CHECKSUM_X490);
	if (ret < 0) {
		dev_err(dev, "writing command failed\n");
		return ret;
	}

	usleep_range(WASP_WAIT_SLEEP_US_LOW, WASP_WAIT_SLEEP_US);

	if (m_model == MODEL_3390) {
		ret = avm_wasp_read_poll_timeout(avmwasp, WASP_WAIT_SLEEP_US,
						 WASP_WAIT_TIMEOUT_COUNT *
						 WASP_WAIT_SLEEP_US,
						 WASP_RESP_OK);
		if (ret) {
			dev_err(dev, "timed out waiting for WASP OK\n");
			return ret;
		}
		if (avm_wasp_netboot_write_chunk(avmwasp, mac_data,
						 ARRAY_SIZE(mac_data)) < 0) {
			dev_err(dev, "error sending MAC address!\n");
			return -EFAULT;
		}
	} else if (m_model == MODEL_X490) {
		while (cont) {
			ret = avm_wasp_read_poll_timeout(avmwasp,
							 WASP_WAIT_SLEEP_US,
							 WASP_WAIT_TIMEOUT_COUNT *
							 WASP_WAIT_SLEEP_US,
							 WASP_RESP_OK);
			if (ret) {
				dev_err(dev,
					"timed out waiting for WASP OK\n");
				return ret;
			}
			regval = mdiobus_read(mdio_bus, WASP_ADDR, 0x2);
			if (regval < 0) {
				dev_err(dev, "mdio read failed\n");
				return ret;
			}
			regval2 = mdiobus_read(mdio_bus, WASP_ADDR, 0x4);
			if (regval2 < 0) {
				dev_err(dev, "mdio read failed\n");
				return ret;
			}
			ret = mdiobus_write(mdio_bus, WASP_ADDR, 0x0,
					    WASP_CMD_SET_CHECKSUM_X490);
			if (ret < 0) {
				dev_err(dev, "writing command failed\n");
				return ret;
			}

			if (regval == 0 && regval2 != 0)
				cont = regval2;
			else
				cont--;
		}

		ret = avm_wasp_read_poll_timeout(avmwasp,
						 WASP_POLL_SLEEP_US,
						 WASP_TIMEOUT_COUNT *
						 WASP_POLL_SLEEP_US,
						 WASP_RESP_OK);
		if (ret) {
			dev_err(dev,
				"error waiting for checksum OK response\n");
			return ret;
		}

		ret = mdiobus_write(mdio_bus, WASP_ADDR, 0x2, 0x00);
		if (ret < 0) {
			dev_err(dev, "mdio write failed\n");
			return ret;
		}
		ret = mdiobus_write(mdio_bus, WASP_ADDR, 0x0,
				    WASP_CMD_START_FIRMWARE2_X490);
		if (ret < 0) {
			dev_err(dev, "writing command failed\n");
			return ret;
		}

		regval = mdiobus_read(mdio_bus, WASP_ADDR, 0x0);
		if (regval != WASP_RESP_OK) {
			dev_err(dev,
				"error starting WASP network boot: 0x%x\n",
				regval);
			return -EFAULT;
		}
	}

	return 0;
}

/**
 * avm_wasp_load_initramfs_image() - load initramfs image to WASP
 * @avmwasp: pointer to drivers private avm_wasp_rproc structure
 *
 * Uses the lan port specified from DT to load the initramfs to
 * WASP after the network boot firmware was successfully started.
 * Communication is done by using raw sockets.
 * There are several commands and status values which are checked.
 * First a discovery packet is received and then each data packet
 * is acknowledged by the WASP network boot firmware.
 * First packet needs to prepend the load address and last packet
 * needs to append the execution address.
 *
 * Return: 0 on Success, -EFAULT if errors with the WASP send protocol
 * have occurred, -EAGAIN if the wasp network interface is down or the
 * error returned from the failed operating system function or service
 */
static int avm_wasp_load_initramfs_image(struct avm_wasp_rproc *avmwasp)
{
	bool done = false;
	int ret;
	u32 num_chunks, chunk_counter;
	short interface_flags;
	const u8 *firmware, *firmware_end;
	struct device *dev = &avmwasp->pdev->dev;
	struct kvec socket_kvec;
	struct msghdr socket_msghdr;
	struct net_device *send_netdev;
	struct sockaddr send_sock_addr;
	struct sockaddr_ll send_socket_address;
	struct socket *wasp_socket;
	struct wasp_packet *packet = (struct wasp_packet *)
			(avmwasp->buffer);
	struct __kernel_old_timeval timeout;
	time64_t start_time, current_time;

	if (!avmwasp->linux_blob) {
		dev_err(dev, "error accessing initramfs image\n");
		goto err;
	}

	firmware = avmwasp->linux_blob->data;
	firmware_end = firmware + avmwasp->linux_blob->size;

	ret = sock_create_kern(&init_net, PF_PACKET, SOCK_RAW,
			       htons(ETH_TYPE_ATH_ECPS_FRAME),
			       &wasp_socket);
	if (ret < 0) {
		dev_err(dev, "error opening recv socket: %d\n", ret);
		goto err;
	}

	timeout.tv_sec = 10;
	timeout.tv_usec = 0;
	ret = sock_setsockopt(wasp_socket, SOL_SOCKET, SO_RCVTIMEO_OLD,
			      KERNEL_SOCKPTR(&timeout), sizeof(timeout));
	if (ret < 0) {
		dev_err(dev, "error SO_RCVTIMEO recv socket: %d\n", ret);
		goto err_socket;
	}

	ret = sock_setsockopt(wasp_socket, SOL_SOCKET, SO_SNDTIMEO_OLD,
			      KERNEL_SOCKPTR(&timeout), sizeof(timeout));
	if (ret < 0) {
		dev_err(dev, "error SO_SNDTIMEO send socket: %d\n", ret);
		goto err_socket;
	}

	rcu_read_lock();
	send_netdev = dev_get_by_name_rcu(sock_net(wasp_socket->sk),
					  avmwasp->loader_port);
	if (send_netdev)
		interface_flags = (short)dev_get_flags(send_netdev);
	rcu_read_unlock();

	if (IS_ERR_OR_NULL(send_netdev)) {
		dev_err(dev, "error accessing net device\n");
		ret = -ENODEV;
		goto err_socket;
	}

	if (!(interface_flags & IFF_UP && interface_flags & IFF_RUNNING)) {
		dev_err(dev, "error wasp interface %s is down\n",
			avmwasp->loader_port);
		ret = -EAGAIN;
		goto err_socket;
	}

	avmwasp->ifindex = send_netdev->ifindex;
	ret = dev_get_mac_address(&send_sock_addr, &init_net,
				  avmwasp->loader_port);
	if (ret < 0) {
		dev_err(dev, "error getting mac address: %d\n", ret);
		goto err_socket;
	}

	send_socket_address.sll_halen = ETH_ALEN;
	send_socket_address.sll_ifindex = avmwasp->ifindex;
	memset(&socket_msghdr, 0, sizeof(socket_msghdr));
	socket_msghdr.msg_name = (struct sockaddr *)&send_socket_address;
	socket_msghdr.msg_namelen = sizeof(struct sockaddr_ll);

	start_time = ktime_get_seconds();

	while (!done) {
		current_time = ktime_get_seconds();
		if ((current_time - start_time) > ETH_SEND_LOOP_TIMEOUT_SECS) {
			dev_err(dev,
				"waiting for packet from WASP timed out\n");
			ret = -ETIMEDOUT;
			goto err_socket;
		}

		socket_kvec.iov_base = avmwasp->buffer;
		socket_kvec.iov_len = ETH_BUF_SIZE;
		ret = kernel_recvmsg(wasp_socket,
				     &socket_msghdr, &socket_kvec, 1,
				     ETH_BUF_SIZE, 0);

		if (ret < 0) {
			dev_err(dev,
				"error receiving any packet or timeout: %d\n",
				ret);
			goto err_socket;
		}

		if (ret < (sizeof(struct ethhdr) + sizeof(packet->hdr))) {
			dev_err(dev,
				"packet too small, discard and continue\n");
			continue;
		}

		if (packet->eh.h_proto != ETH_TYPE_ATH_ECPS_FRAME)
			continue;

		memcpy(wasp_mac, packet->eh.h_source, sizeof(wasp_mac));

		if (packet->hdr.packet_start == ETH_WASP_PACKET_ID) {
			switch (packet->hdr.response) {
			case RESP_DISCOVER:
				chunk_counter = 1;
				num_chunks = DIV_ROUND_UP(avmwasp->linux_blob->size,
							  ETH_DATA_SIZE);
				fallthrough;
			case RESP_OK:
				memcpy(packet->eh.h_dest, wasp_mac, sizeof(packet->eh.h_dest));
				packet->eh.h_proto = ETH_TYPE_ATH_ECPS_FRAME;
				memcpy(packet->eh.h_source, send_sock_addr.sa_data,
				       sizeof(packet->eh.h_source));

				if (firmware < firmware_end) {
					size_t bytestosend, send_len;
					u32 data_offset = 0;

					if (chunk_counter == 1) {
						memcpy(packet->payload,
						       &m_load_addr,
						       sizeof(m_load_addr));
						data_offset = sizeof(m_load_addr);
					}

					if ((firmware_end - firmware) >=
					     ETH_DATA_SIZE)
						bytestosend = ETH_DATA_SIZE;
					else
						bytestosend = firmware_end -
									firmware;
					memcpy(&packet->payload[data_offset],
					       firmware, bytestosend);
					firmware = firmware + ETH_DATA_SIZE;

					packet->hdr.packet_start =
							ETH_WASP_PACKET_ID;
					if (chunk_counter == num_chunks) {
						packet->hdr.response =
							CMD_START_FIRMWARE;
						memcpy(&packet->payload
						       [data_offset + bytestosend],
						       &m_load_addr,
						       sizeof(m_load_addr));
						bytestosend += sizeof(m_load_addr);
					} else {
						packet->hdr.command =
							CMD_FIRMWARE_DATA;
					}
					packet->hdr.counter =
							(chunk_counter - 1) * 4;

					send_len = sizeof(struct ethhdr)
						+ sizeof(packet->hdr) + bytestosend +
						data_offset;

					socket_kvec.iov_len = send_len;
					socket_kvec.iov_base = avmwasp->buffer;

					ret = kernel_sendmsg(wasp_socket,
							     &socket_msghdr,
							     &socket_kvec,
							     1, send_len);
					if (ret < 0) {
						dev_err(dev,
							"error sending to WASP %d\n",
							ret);
						goto err_socket;
					}

					chunk_counter++;
				}
				break;
			case RESP_ERROR:
				dev_err(dev,
					"received an WASP error packet!\n");
				ret = -EFAULT;
				goto err_socket;
			case RESP_STARTING:
				done = true;
				ret = 0;
				continue;
				break;
			default:
				dev_err(dev, "unknown packet! Continue\n");
				continue;
				break;
			}
		}
	}

err_socket:
	wasp_socket->ops->release(wasp_socket);
err:
	return ret;
}

/**
 * avm_wasp_rproc_start() - start the remote processor
 * @rproc: pointer to the rproc structure
 *
 * Starts the remote processor by turning it on using the startup
 * gpio and initiating the reset process using the reset_gpio.
 * After that the status is checked if poweron and reset were
 * successful.
 * As the first step, the network boot firmware is tried to be loaded
 * and started.
 * As a second step, the initramfs image is tried to be loaded
 * and started.
 *
 * Return: 0 on Success, -ENODEV or return code from the called function
 * if any other error occurred in the process of starting and loading
 * the firmware files to the WASP processor
 */
static int avm_wasp_rproc_start(struct rproc *rproc)
{
	struct avm_wasp_rproc *avmwasp = rproc->priv;
	struct device *dev = &avmwasp->pdev->dev;
	u32 phandle;
	int ret;

	gpiod_set_value(avmwasp->startup_gpio, 1);
	usleep_range(WASP_WAIT_SLEEP_US_LOW, WASP_WAIT_SLEEP_US);
	gpiod_set_value(avmwasp->reset_gpio, 0);
	usleep_range(WASP_WAIT_SLEEP_US_LOW, WASP_WAIT_SLEEP_US);
	gpiod_set_value(avmwasp->reset_gpio, 1);
	usleep_range(WASP_WAIT_SLEEP_US_LOW, WASP_WAIT_SLEEP_US);

	ret = request_firmware_direct((const struct firmware **)
				      &avmwasp->fw_blob, netboot, dev);
	if (ret) {
		dev_err(dev, "could not load network boot firmware\n");
		goto err;
	}

	ret = of_property_read_u32(dev->of_node, "avm,wasp-mdio", &phandle);
	if (ret) {
		dev_err(dev, "no wasp-netboot-mdio given\n");
		goto err_release_fw;
	} else {
		struct device_node *mdio_node =
					of_find_node_by_phandle(phandle);

		if (!mdio_node) {
			dev_err(dev, "get wasp-netboot-mdio failed\n");
			ret = -ENODEV;
			goto err_release_fw;
		} else {
			avmwasp->mdio_bus = of_mdio_find_bus(mdio_node);
			of_node_put(mdio_node);
			if (!avmwasp->mdio_bus) {
				dev_err(dev, "mdio bus not found\n");
				ret = -ENODEV;
				goto err_release_fw;
			}
		}
	}

	ret = avm_wasp_netboot_load_firmware(avmwasp);
	if (ret)
		goto err_put_device;

	ret = avm_wasp_load_initramfs_image(avmwasp);

err_put_device:
	put_device(&avmwasp->mdio_bus->dev);
err_release_fw:
	release_firmware(avmwasp->fw_blob);
err:
	return ret;
}

/**
 * avm_wasp_rproc_stop() - stop the remote processor
 * @rproc: pointer to the rproc structure
 *
 * To stop the remote processor just the startup gpio is set to 0
 * and the WASP processor is powered off
 *
 * Return: 0 on Success
 */
static int avm_wasp_rproc_stop(struct rproc *rproc)
{
	struct avm_wasp_rproc *avmwasp = rproc->priv;

	gpiod_set_value(avmwasp->startup_gpio, 0);

	return 0;
}

/**
 * avm_wasp_rproc_load() - noop to avoid the ELF binary defaults
 * @rproc: pointer to the rproc structure
 * @fw: pointer to firmware struct
 *
 * If a load function is not defined in the rproc_ops, then all the settings
 * like checking the firmware binary will default to ELF checks, which fail
 * in case of the bootable and compressed initramfs image for WASP.
 * This function stores the initramfs image that is loaded by the remote
 * processor framework during boot process into the priv for access by
 * the initramfs load function avm_wasp_load_initramfs_image().
 *
 * Return: Always 0
 */
static int avm_wasp_rproc_load(struct rproc *rproc, const struct firmware *fw)
{
	struct avm_wasp_rproc *avmwasp = rproc->priv;

	avmwasp->linux_blob = fw;

	return 0;
}

static const struct rproc_ops avm_wasp_rproc_ops = {
	.start		= avm_wasp_rproc_start,
	.stop		= avm_wasp_rproc_stop,
	.load		= avm_wasp_rproc_load,
};

static int avm_wasp_rproc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct avm_wasp_rproc *avmwasp;
	struct rproc *rproc;
	struct net_device *netdev;
	int ret;
	short interface_flags;
	const u32 *match_data;
	u32 phandle;

	match_data = of_device_get_match_data(dev);
	if (IS_ERR_OR_NULL(match_data)) {
		dev_err_probe(dev, PTR_ERR(match_data),
			      "model specific data is not defined\n");
		ret = -ENODEV;
		goto err;
	}
	m_model = *match_data;

	rproc = devm_rproc_alloc(dev, "avm,wasp", &avm_wasp_rproc_ops,
				 image, sizeof(*avmwasp));
	if (!rproc) {
		ret = -ENOMEM;
		goto err;
	}

	rproc->auto_boot = true;

	avmwasp = rproc->priv;
	avmwasp->rproc = rproc;
	avmwasp->pdev = pdev;

	ret = request_firmware((const struct firmware **)&avmwasp->fw_blob,
			       firmware, dev);
	if (ret)
		dev_err(dev, "could not load ath9k firmware\n");
	release_firmware(avmwasp->fw_blob);

	if (m_model == MODEL_X490) {
		ret = request_firmware((const struct firmware **)
				       &avmwasp->fw_blob, caldata, dev);
		if (ret)
			dev_err(dev, "could not load ath10k caldata\n");
		release_firmware(avmwasp->fw_blob);
	}

	ret = of_property_read_u32(dev->of_node, "avm,wasp-port",
				   &phandle);
	if (ret) {
		dev_err(dev, "no wasp-port given\n");
		goto err;
	} else {
		struct device_node *child = of_find_node_by_phandle(phandle);

		if (!child) {
			dev_err(dev, "get wasp-port node failed\n");
			ret = -ENODEV;
			goto err;
		} else {
			ret = of_property_read_string(child, "label",
						      (const char **)
						      &avmwasp->loader_port);
			of_node_put(child);
			if (ret) {
				dev_err(dev, "get wasp-port label failed\n");
				goto err;
			}
		}
	}

	rcu_read_lock();
	netdev = dev_get_by_name_rcu(&init_net, avmwasp->loader_port);
	if (netdev)
		interface_flags = (short)dev_get_flags(netdev);
	rcu_read_unlock();

	if (IS_ERR_OR_NULL(netdev)) {
		dev_err_probe(dev, PTR_ERR(netdev),
			      "error accessing net device\n");
		ret = -ENODEV;
		goto err;
	}

	if (!(interface_flags & IFF_UP && interface_flags & IFF_RUNNING)) {
		dev_err(dev, "error wasp interface %s down\n",
			avmwasp->loader_port);
		ret = -EPROBE_DEFER;
		goto err;
	}

	avmwasp->startup_gpio = devm_gpiod_get(dev, "avm,startup",
					       GPIOD_OUT_LOW);
	if (IS_ERR(avmwasp->startup_gpio)) {
		ret = dev_err_probe(dev, PTR_ERR(avmwasp->startup_gpio),
				    "failed to get startup gpio\n");
		goto err;
	}

	avmwasp->reset_gpio = devm_gpiod_get(dev, "avm,reset", GPIOD_OUT_LOW);
	if (IS_ERR(avmwasp->reset_gpio)) {
		ret = dev_err_probe(dev, PTR_ERR(avmwasp->reset_gpio),
				    "failed to get reset gpio\n");
		goto err;
	}

	platform_set_drvdata(pdev, rproc);

	ret = devm_rproc_add(dev, rproc);
	if (ret) {
		dev_err(dev, "rproc_add failed\n");
		goto err;
	}

err:
	return ret;
}

static int avm_wasp_rproc_remove(struct platform_device *pdev)
{
	struct rproc *rproc = platform_get_drvdata(pdev);
	struct avm_wasp_rproc *avmwasp = rproc->priv;

	gpiod_set_value(avmwasp->startup_gpio, 0);

	return 0;
}

static const u32 model_3390 = MODEL_3390;
static const u32 model_x490 = MODEL_X490;

static const struct of_device_id avm_wasp_rproc_of_match[] = {
	{ .compatible = "avm,fritzbox3390-wasp", .data = &model_3390 },
	{ .compatible = "avm,fritzboxx490-wasp", .data = &model_x490 },
	{},
};
MODULE_DEVICE_TABLE(of, avm_wasp_rproc_of_match);

static struct platform_driver avm_wasp_rproc_driver = {
	.probe = avm_wasp_rproc_probe,
	.remove = avm_wasp_rproc_remove,
	.driver = {
		.name = "avm_wasp_rproc",
		.of_match_table = avm_wasp_rproc_of_match,
	},
};

module_platform_driver(avm_wasp_rproc_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("AVM WASP remote processor boot driver");
MODULE_AUTHOR("Daniel Kestrel <kestrelseventyfour@gmail.com>");
