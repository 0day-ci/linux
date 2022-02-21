// SPDX-License-Identifier: GPL-2.0-only
/*
 * AVM WASP Remote Processor driver
 *
 * Copyright (c) 2019-2020 Andreas Böhler
 * Copyright (c) 2021-2022 Daniel Kestrel
 *
 */

#include <linux/err.h>
#include <linux/interrupt.h>
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
#include "avm_wasp.h"

/**
 * struct avm_wasp_rproc - avmwasp remote processor priv
 * @rproc: rproc handle
 * @pdev: pointer to platform device
 * @eeprom_blob: pointer to load and save any firmware
 * @linux_blob: pointer to access initramfs image
 * @complete: structure for asynchronous firmware load
 * @mdio_bus: pointer to mii_bus of gswip device for gpio
 * @startup_gpio: store WASP startup gpio number
 * @reset_gpio: store WASP reset gpio number
 * @s_gpio_flg: store WASP startup gpio flags active high/low
 * @r_gpio_flg: store WASP reset gpio flags active high/low
 * @netboot_firmware: store name of the network boot firmware
 * @loader_port: store name of the port wasp is connected to
 * @sendbuf: send buffer for uploading WASP initramfs firmware
 * @recvbuf: recv buffer for feedback from WASP
 * @s_packet: structure for sending packets to WASP
 * @send_socket: pointer to socket for sending to WASP
 * @recv_socket: pointer to socket for receiving from WASP
 * @ifindex: interface index used for WASP communication
 */
struct avm_wasp_rproc {
	struct rproc *rproc;
	struct platform_device *pdev;
	const struct firmware *eeprom_blob, *linux_blob;
	struct completion complete;
	char *mdio_bus_id;
	struct mii_bus *mdio_bus;
	int startup_gpio, reset_gpio;
	enum of_gpio_flags s_gpio_flg, r_gpio_flg;
	char *netboot_firmware;
	char *loader_port;
	char sendbuf[BUF_SIZE];
	char recvbuf[BUF_SIZE];
	struct wasp_packet s_packet;
	struct socket *send_socket;
	struct socket *recv_socket;
	int ifindex;
};

/**
 * avm_wasp_firmware_request_cb() - callback handler for firmware load
 * @eeprom_blob: pointer to struct firmware
 * @ctx: context passed
 *
 * This handler is called after completing the request_firmware_nowait
 * function by passing the avm_wasp_rproc struct
 * It saves the firmware in the context and calls complete
 */
static void avm_wasp_firmware_request_cb(const struct firmware *eeprom_blob,
					 void *ctx)
{
	struct avm_wasp_rproc *avmwasp = ctx;

	if (eeprom_blob)
		avmwasp->eeprom_blob = eeprom_blob;

	complete(&avmwasp->complete);
}

/**
 * avm_wasp_firmware_request() - asynchronous load of passed firmware
 * @avmwasp: pointer to drivers private avm_wasp_rproc structure
 * @name: char pointer to filename (relative to /lib/firmware)
 *
 * Handles setup and execution of the asynchronous firmware request
 * Used to trigger the load of the ath10k caldata and ath9k eeprom
 * firmware from the tffs partition of the devices
 *
 * Return: 0 on success, -2 if file not found or error from function
 * request_firmware_nowait
 */
static int avm_wasp_firmware_request(struct avm_wasp_rproc *avmwasp,
				     const char *name)
{
	int err;

	init_completion(&avmwasp->complete);

	err = request_firmware_nowait(THIS_MODULE, 1, name,
				      &avmwasp->pdev->dev,
				      GFP_KERNEL, avmwasp,
				      avm_wasp_firmware_request_cb);
	if (err < 0) {
		dev_err(&avmwasp->pdev->dev,
			"Load request for %s failed\n", name);
		return err;
	}

	wait_for_completion(&avmwasp->complete);

	if (!avmwasp->eeprom_blob) {
		dev_err(&avmwasp->pdev->dev,
			"Unable to load %s\n", name);
		return -ENOENT;
	}

	return 0;
}

/**
 * avm_wasp_firmware_release() - clean up after firmware load
 * @avmwasp: pointer to drivers private avm_wasp_rproc structure
 *
 * Releases the firmware that is in the eeprom_blob firmware
 * pointer of the private avm_wasp_rproc structure
 */
static void avm_wasp_firmware_release(struct avm_wasp_rproc *avmwasp)
{
	release_firmware(avmwasp->eeprom_blob);
	avmwasp->eeprom_blob = NULL;
}

/**
 * avm_wasp_netboot_mdio_read() - read with gswip mdio bus
 * @avmwasp: pointer to drivers private avm_wasp_rproc structure
 * @location: register number of the m_regs_wasp register array
 *
 * Reads a value from the specified register for the mdio address
 * that is used for the connection to the WASP SoC
 * Mutex on mdio_lock is required to serialize access on bus
 *
 * Return: Value that was read from the specified register
 */
int avm_wasp_netboot_mdio_read(struct avm_wasp_rproc *avmwasp,
			       int location)
{
	int value;

	if (location > M_REGS_WASP_INDEX_MAX || location < 0)
		return 0;
	mutex_lock(&avmwasp->mdio_bus->mdio_lock);
	value = avmwasp->mdio_bus->read(avmwasp->mdio_bus,
			WASP_ADDR, m_regs_wasp[location]);
	mutex_unlock(&avmwasp->mdio_bus->mdio_lock);
	return value;
}

/**
 * avm_wasp_netboot_mdio_write() - write with gswip mdio bus
 * @avmwasp: pointer to drivers private avm_wasp_rproc structure
 * @location: register number of the m_regs_wasp register array
 * @value: value to be written to the register
 *
 * Writes a value to the specified register for the mdio address
 * that is used for the connection to the WASP SoC
 * Mutex on mdio_lock is required to serialize access on bus
 * Makes sure not to write to invalid registers as this can have
 * unpredictable results
 */
void avm_wasp_netboot_mdio_write(struct avm_wasp_rproc *avmwasp,
				 int location, int value)
{
	if (location > M_REGS_WASP_INDEX_MAX || location < 0)
		return;
	mutex_lock(&avmwasp->mdio_bus->mdio_lock);
	avmwasp->mdio_bus->write(avmwasp->mdio_bus, WASP_ADDR,
			m_regs_wasp[location], value);
	mutex_unlock(&avmwasp->mdio_bus->mdio_lock);
}

/**
 * avm_wasp_netboot_mdio_write_u32_split() - write 32bit value
 * @avmwasp: pointer to drivers private avm_wasp_rproc structure
 * @location: register number of the m_regs_wasp register array
 * @value: value to be written to the register
 *
 * As the mdio registers are 16bit, this function writes a 32bit value
 * to two subsequent registers starting with the specified register
 * for the mdio address that is used for the connection to the WASP SoC
 */
void avm_wasp_netboot_mdio_write_u32_split(struct avm_wasp_rproc *avmwasp,
					   int location, const u32 value)
{
	avm_wasp_netboot_mdio_write(avmwasp, location,
				    ((value & 0xffff0000) >> 16));
	avm_wasp_netboot_mdio_write(avmwasp, location + 1,
				    (value & 0x0000ffff));
}

/**
 * avm_wasp_netboot_write_header() - write header to WASP
 * @avmwasp: pointer to drivers private avm_wasp_rproc structure
 * @start_address: address where to load the firmware to on WASP
 * @len: length of the network boot firmware
 * @exec_address: address where to start execution on WASP
 *
 * Writes the header to WASP using mdio to initiate the start of
 * transferring the network boot firmware to WASP
 *
 * Return: 0 on Success or -14 if writing header failed based on return
 * code from WASP
 */
static int avm_wasp_netboot_write_header(struct avm_wasp_rproc *avmwasp,
					 const u32 start_addr, const u32 len,
					 const u32 exec_addr)
{
	int regval;
	int timeout = WASP_TIMEOUT_COUNT;

	avm_wasp_netboot_mdio_write_u32_split(avmwasp, 1, start_addr);
	avm_wasp_netboot_mdio_write_u32_split(avmwasp, 3, len);
	avm_wasp_netboot_mdio_write_u32_split(avmwasp, 5, exec_addr);
	avm_wasp_netboot_mdio_write(avmwasp, 0, WASP_CMD_SET_PARAMS);

	do {
		udelay(WASP_POLL_SLEEP_US);
		regval = avm_wasp_netboot_mdio_read(avmwasp, 0);
		timeout--;
	} while ((regval != WASP_RESP_OK) && (timeout > 0));

	if (regval != WASP_RESP_OK) {
		dev_err(&avmwasp->pdev->dev,
			"Error writing header to WASP! Status = %d\n", regval);
		return -EFAULT;
	}
	return 0;
}

/**
 * avm_wasp_netboot_write_checksum() - write checksum to WASP
 * @avmwasp: pointer to drivers private avm_wasp_rproc structure
 * @checksum: calculated checksum value to be sent to WASP
 *
 * Writes the calculated checksum for the given network boot firmware
 * to WASP using mdio as the second step
 *
 * Return: 0 on Success or -14 if writing checksum failed based on return
 * code from WASP
 */
static int avm_wasp_netboot_write_checksum(struct avm_wasp_rproc *avmwasp,
					   const uint32_t checksum)
{
	int regval;
	int timeout = WASP_TIMEOUT_COUNT;

	avm_wasp_netboot_mdio_write_u32_split(avmwasp, 1, checksum);
	if (m_model == MODEL_3390) {
		avm_wasp_netboot_mdio_write_u32_split(avmwasp, 3, 0x0000);
		avm_wasp_netboot_mdio_write(avmwasp, 0,
					    WASP_CMD_SET_CHECKSUM_3390);
	} else if (m_model == MODEL_X490)
		avm_wasp_netboot_mdio_write(avmwasp, 0,
					    WASP_CMD_SET_CHECKSUM_X490);

	do {
		udelay(WASP_POLL_SLEEP_US);
		regval = avm_wasp_netboot_mdio_read(avmwasp, 0);
		timeout--;
	} while ((regval != WASP_RESP_OK) && (timeout > 0));

	if (regval != WASP_RESP_OK) {
		dev_err(&avmwasp->pdev->dev,
			"Error writing checksum to WASP! Status = %d\n",
			regval);
		return -EFAULT;
	}
	return 0;
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
 * Return: 0 on Success, -14 if data length is mor than 14 bytes or
 * -2 if writing the data failed based on return code from WASP
 */
static int avm_wasp_netboot_write_chunk(struct avm_wasp_rproc *avmwasp,
					const char *data, const int len)
{
	int regval, i, j;
	int timeout = WASP_TIMEOUT_COUNT;

	if (len > WASP_CHUNK_SIZE || len < 0 || !data)
		return -EFAULT;
	for (i = 0, j = 1; i < len; i += 4, j += 2)
		avm_wasp_netboot_mdio_write_u32_split(avmwasp, j,
						      *((uint32_t *)
						      (data + i)));

	avm_wasp_netboot_mdio_write(avmwasp, 0, WASP_CMD_SET_DATA);

	do {
		udelay(WASP_POLL_SLEEP_US);
		regval = avm_wasp_netboot_mdio_read(avmwasp, 0);
		timeout--;
	} while ((regval != WASP_RESP_OK) && (timeout > 0));

	if (regval != WASP_RESP_OK && regval != WASP_RESP_WAIT &&
	    regval != WASP_RESP_COMPLETED) {
		dev_err(&avmwasp->pdev->dev,
			"Error writing chunk to WASP: m_reg_status = 0x%x!\n",
			regval);
		return -EFAULT;
	}
	return 0;
}

/**
 * avm_wasp_netboot_calc_checksum() - calculate netboot firmware checksum
 * @avmwasp: pointer to drivers private avm_wasp_rproc structure
 *
 * Calculates the checksum by using the eeprom_blob from the private
 * avm_wasp_rproc structure
 *
 * Return: Calculated checksum or -14 on Error
 */
static uint32_t avm_wasp_netboot_calc_checksum(struct avm_wasp_rproc *avmwasp)
{
	u32 checksum = 0xffffffff;
	u32 cs;
	int count = -1;
	size_t size;
	const u8 *firmware;
	const u8 *firmware_end;

	if (!avmwasp->eeprom_blob)
		return -EFAULT;
	size = avmwasp->eeprom_blob->size;
	firmware = avmwasp->eeprom_blob->data;
	firmware_end = firmware + size;

	if (!firmware || size <= 0)
		return -EFAULT;

	while (firmware < firmware_end) {
		cs = (firmware[0] << 24 | firmware[1] << 16 |
			firmware[2] << 8 | firmware[3]);
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
 * Return: 0 on Success, -2 if no firmware is present, -19 if no
 * firmware or -14 if other errors have occurred
 */
int avm_wasp_netboot_load_firmware(struct avm_wasp_rproc *avmwasp)
{
	const u8 *firmware;
	const u8 *firmware_end;
	int ret, regval, regval2, count, cont = 1;

	count = WASP_WAIT_TIMEOUT_COUNT;

	while (count > 0 && (avm_wasp_netboot_mdio_read(avmwasp, 0)
						!= WASP_RESP_OK)) {
		count -= 1;
		mdelay(WASP_WAIT_SLEEP);
	}

	if (avm_wasp_netboot_mdio_read(avmwasp, 0)
						!= WASP_RESP_OK) {
		dev_err(&avmwasp->pdev->dev,
			"Error: WASP processor not ready\n");

		return -ENODEV;
	}

	ret = request_firmware_direct((const struct firmware **)
					&avmwasp->eeprom_blob,
		avmwasp->netboot_firmware, &avmwasp->pdev->dev);
	if (ret) {
		dev_err(&avmwasp->pdev->dev,
			"Could not find network boot firmware\n");
		return -ENOENT;
	}

	firmware = avmwasp->eeprom_blob->data;
	firmware_end = firmware + avmwasp->eeprom_blob->size;

	if (!firmware || avmwasp->eeprom_blob->size <= 0)
		return -EFAULT;

	if (avm_wasp_netboot_write_header(avmwasp, start_addr,
					  avmwasp->eeprom_blob->size,
					  exec_addr) < 0)
		return -EFAULT;

	if (avm_wasp_netboot_write_checksum(avmwasp,
					    avm_wasp_netboot_calc_checksum
					    (avmwasp)) < 0)
		return -EFAULT;

	while (firmware < firmware_end) {
		if ((firmware_end - firmware) >= WASP_CHUNK_SIZE) {
			if (avm_wasp_netboot_write_chunk(avmwasp, firmware,
							 WASP_CHUNK_SIZE) < 0)
				return -EFAULT;
		} else {
			if (avm_wasp_netboot_write_chunk(avmwasp, firmware,
							 (firmware_end -
							 firmware)) < 0)
				return -EFAULT;
		}
		firmware += WASP_CHUNK_SIZE;
	}

	mdelay(WASP_WAIT_SLEEP);

	if (m_model == MODEL_3390)
		avm_wasp_netboot_mdio_write(avmwasp, 0,
					    WASP_CMD_START_FIRMWARE_3390);
	else if (m_model == MODEL_X490)
		avm_wasp_netboot_mdio_write(avmwasp, 0,
					    WASP_CMD_START_FIRMWARE_X490);

	avm_wasp_firmware_release(avmwasp);

	mdelay(WASP_WAIT_SLEEP);
	count = 0;

	while ((avm_wasp_netboot_mdio_read(avmwasp, 0)
			!= WASP_RESP_READY_TO_START) &&
			(count < WASP_WAIT_TIMEOUT_COUNT)) {
		mdelay(WASP_WAIT_SLEEP);
		count++;
	}
	if (count >= WASP_WAIT_TIMEOUT_COUNT) {
		dev_err(&avmwasp->pdev->dev,
			"Timed out waiting for WASP ready to start.\n");
		return -EFAULT;
	}

	if (m_model == MODEL_3390)
		avm_wasp_netboot_mdio_write(avmwasp, 0,
					    WASP_CMD_START_FIRMWARE_3390);
	else if (m_model == MODEL_X490)
		avm_wasp_netboot_mdio_write(avmwasp, 0,
					    WASP_CMD_SET_CHECKSUM_X490);

	mdelay(WASP_WAIT_SLEEP);

	if (m_model == MODEL_3390) {
		count = 0;
		while ((avm_wasp_netboot_mdio_read(avmwasp, 0) !=
		       WASP_RESP_OK) &&
		       (count < WASP_WAIT_TIMEOUT_COUNT)) {
			mdelay(WASP_WAIT_SLEEP);
			count++;
		}
		if (count >= WASP_WAIT_TIMEOUT_COUNT) {
			dev_err(&avmwasp->pdev->dev,
				"Timed out waiting for WASP OK.\n");
			return -EFAULT;
		}
		if (avm_wasp_netboot_write_chunk(avmwasp, mac_data,
						 WASP_CHUNK_SIZE) < 0) {
			dev_err(&avmwasp->pdev->dev,
				"Error sending MAC address!\n");
			return -EFAULT;
		}
	} else if (m_model == MODEL_X490) {
		cont = 1;
		while (cont) {
			count = 0;
			while ((avm_wasp_netboot_mdio_read(avmwasp, 0)
					!= WASP_RESP_OK) &&
					(count < WASP_WAIT_TIMEOUT_COUNT)) {
				mdelay(WASP_WAIT_SLEEP);
				count++;
			}
			if (count >= WASP_WAIT_TIMEOUT_COUNT) {
				dev_err(&avmwasp->pdev->dev,
					"Timed out waiting for WASP OK.\n");
				return -EFAULT;
			}
			regval = avm_wasp_netboot_mdio_read(avmwasp, 1);
			regval2 = avm_wasp_netboot_mdio_read(avmwasp, 2);
			avm_wasp_netboot_mdio_write(avmwasp, 0,
						    WASP_CMD_SET_CHECKSUM_X490
						    );
			if (regval == 0 && regval2 != 0)
				cont = regval2;
			else
				cont--;
		}

		count = 0;
		while ((avm_wasp_netboot_mdio_read(avmwasp, 0) !=
			WASP_RESP_OK) &&
			(count < WASP_TIMEOUT_COUNT)) {
			udelay(WASP_BOOT_SLEEP_US);
			count++;
		}
		if (count >= WASP_TIMEOUT_COUNT) {
			dev_err(&avmwasp->pdev->dev,
				"Error waiting for checksum OK response.\n");
			return -EFAULT;
		}

		avm_wasp_netboot_mdio_write(avmwasp, 1, 0x00);
		avm_wasp_netboot_mdio_write(avmwasp, 0,
					    WASP_CMD_START_FIRMWARE2_X490);

		regval = avm_wasp_netboot_mdio_read(avmwasp, 0);
		if (regval != WASP_RESP_OK) {
			dev_err(&avmwasp->pdev->dev,
				"Error starting WASP network boot: 0x%x\n",
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
 * The port of the lantiq gswip device will be started if not
 * already up and running.
 * There are several commands and status values which are checked.
 * First a discovery packet is received and then each data packet
 * is acknowledged by the WASP network boot firmware.
 * First packet needs to prepend the load address and last packet
 * needs to append the execution address.
 *
 * Return: 0 on Success, -14 if errors with the WASP send protocol
 * have occurred or the error returned from the failed operating
 * system function or service
 */
int avm_wasp_load_initramfs_image(struct avm_wasp_rproc *avmwasp)
{
	int done = 0;
	int reuse = 1;
	int num_chunks = 0;
	int chunk_counter = 1;
	int ret, packet_counter, data_offset;
	int send_len = 0;
	short interface_flags;
	ssize_t numbytes;
	ssize_t read;
	const u8 *firmware;
	const u8 *firmware_end;
	struct wasp_packet *packet = (struct wasp_packet *)
			(avmwasp->recvbuf + sizeof(struct ethhdr));
	struct ethhdr *recv_eh = (struct ethhdr *)avmwasp->recvbuf;
	struct msghdr recv_socket_hdr;
	struct kvec recv_vec;
	struct ethhdr *send_eh = (struct ethhdr *)avmwasp->sendbuf;
	struct sockaddr_ll send_socket_address;
	struct msghdr send_socket_hdr;
	struct kvec send_vec;
	struct net_device *send_netdev;
	struct sockaddr send_sock_addr;
	struct timeval {
		__kernel_old_time_t	tv_sec;
		__kernel_suseconds_t	tv_usec;
	} timeout;
	time64_t start_time, current_time;

	if (!avmwasp->linux_blob) {
		dev_err(&avmwasp->pdev->dev,
			"Error accessing initramfs image");
		goto err;
	}

	ret = sock_create_kern(&init_net, PF_PACKET, SOCK_RAW,
			       htons(ETHER_TYPE_ATH_ECPS_FRAME),
			       &avmwasp->recv_socket);
	if (ret < 0) {
		dev_err(&avmwasp->pdev->dev,
			"Error opening recv socket: %d", ret);
		goto err;
	}

	ret = sock_setsockopt(avmwasp->recv_socket, SOL_SOCKET, SO_REUSEADDR,
			      KERNEL_SOCKPTR(&reuse), sizeof(reuse));
	if (ret < 0) {
		dev_err(&avmwasp->pdev->dev,
			"Error SO_REUSEADDR recv socket: %d", ret);
		goto err_recv;
	}

	ret = sock_setsockopt(avmwasp->recv_socket, SOL_SOCKET,
			      SO_BINDTODEVICE,
			      KERNEL_SOCKPTR(avmwasp->loader_port),
			      IFNAMSIZ - 1);
	if (ret < 0) {
		dev_err(&avmwasp->pdev->dev,
			"Error SO_BINDTODEVICE recv socket: %d", ret);
		goto err_recv;
	}

	timeout.tv_sec = 10;
	timeout.tv_usec = 0;
	ret = sock_setsockopt(avmwasp->recv_socket, SOL_SOCKET,
			      SO_RCVTIMEO_OLD,
			KERNEL_SOCKPTR(&timeout), sizeof(timeout));
	if (ret < 0) {
		dev_err(&avmwasp->pdev->dev,
			"Error SO_RCVTIMEO recv socket: %d", ret);
		goto err_recv;
	}

	ret = sock_create_kern(&init_net, AF_PACKET, SOCK_RAW, IPPROTO_RAW,
			       &avmwasp->send_socket);
	if (ret < 0) {
		dev_err(&avmwasp->pdev->dev,
			"Error opening send socket: %d", ret);
		goto err_recv;
	}

	timeout.tv_sec = 10;
	timeout.tv_usec = 0;
	ret = sock_setsockopt(avmwasp->send_socket, SOL_SOCKET,
			      SO_SNDTIMEO_OLD,
			KERNEL_SOCKPTR(&timeout), sizeof(timeout));
	if (ret < 0) {
		dev_err(&avmwasp->pdev->dev,
			"Error SO_SNDTIMEO send socket: %d", ret);
		goto err_send;
	}

	rcu_read_lock();
	send_netdev = dev_get_by_name_rcu(sock_net(avmwasp->send_socket->sk),
					  avmwasp->loader_port);
	if (send_netdev)
		interface_flags = (short)dev_get_flags(send_netdev);
	rcu_read_unlock();

	if (IS_ERR_OR_NULL(send_netdev)) {
		dev_err(&avmwasp->pdev->dev, "Error accessing net device.\n");
		ret = -ENODEV;
		goto err_send;
	}

	interface_flags |= IFF_PROMISC | IFF_UP | IFF_RUNNING;
	rtnl_lock();
	ret = dev_change_flags(send_netdev, interface_flags, NULL);
	rtnl_unlock();

	if (ret) {
		dev_err(&avmwasp->pdev->dev,
			"Error changing interface flags: %d\n", ret);
		goto err_send;
	}

	avmwasp->ifindex = send_netdev->ifindex;
	ret = dev_get_mac_address(&send_sock_addr,
				  sock_net(avmwasp->send_socket->sk),
			avmwasp->loader_port);
	if (ret < 0) {
		dev_err(&avmwasp->pdev->dev,
			"Error getting mac address: %d\n", ret);
		goto err_send;
	}

	memset(avmwasp->sendbuf, 0, BUF_SIZE);

	memcpy(send_eh->h_dest, wasp_mac, sizeof(send_eh->h_dest));
	send_eh->h_proto = ETHER_TYPE_ATH_ECPS_FRAME;
	memcpy(send_eh->h_source, send_sock_addr.sa_data,
	       sizeof(send_eh->h_source));

	start_time = ktime_get_seconds();

	while (!done) {
		current_time = ktime_get_seconds();
		if ((current_time - start_time) > SEND_LOOP_TIMEOUT_SECONDS) {
			dev_err(&avmwasp->pdev->dev,
				"Waiting for packet from WASP timed out.\n");
			ret = -EFAULT;
			goto err_send;
		}

		memset(&recv_vec, 0, sizeof(recv_vec));
		memset(&recv_socket_hdr, 0, sizeof(recv_socket_hdr));
		recv_vec.iov_base = avmwasp->recvbuf;
		recv_vec.iov_len = BUF_SIZE;
		numbytes = kernel_recvmsg(avmwasp->recv_socket,
					  &recv_socket_hdr, &recv_vec, 1,
					  BUF_SIZE, 0);

		if (numbytes < 0) {
			dev_err(&avmwasp->pdev->dev,
				"Error receiving any packet or timeout: %d\n",
				numbytes);
			ret = -EFAULT;
			goto err_send;
		}

		if (numbytes < (sizeof(struct ethhdr) + WASP_HEADER_LEN)) {
			dev_err(&avmwasp->pdev->dev,
				"Packet too small, discard and continue.\n");
			continue;
		}

		if (recv_eh->h_proto != ETHER_TYPE_ATH_ECPS_FRAME)
			continue;

		memcpy(wasp_mac, recv_eh->h_source, sizeof(wasp_mac));
		memset(&avmwasp->s_packet, 0, sizeof(avmwasp->s_packet));

		if (packet->packet_start == PACKET_START) {
			switch (packet->response) {
			case RESP_DISCOVER:
				packet_counter = 0;
				firmware = avmwasp->linux_blob->data;
				firmware_end = firmware
						+ avmwasp->linux_blob->size;

				chunk_counter = 1;
				num_chunks =
					avmwasp->linux_blob->size / CHUNK_SIZE;
				if (avmwasp->linux_blob->size % CHUNK_SIZE != 0)
					num_chunks++;
			break;
			case RESP_OK:
				/* got reply send next packet */
			break;
			case RESP_ERROR:
				dev_err(&avmwasp->pdev->dev,
					"Received an WASP error packet!\n");
				ret = -EFAULT;
				goto err_send;
			break;
			case RESP_STARTING:
				done = 1;
				ret = 0;
				continue;
			break;
			default:
				dev_err(&avmwasp->pdev->dev,
					"Unknown packet! Continue.\n");
				continue;
			break;
			}

			if (packet_counter == 0) {
				memcpy(avmwasp->s_packet.payload, &m_load_addr,
				       sizeof(m_load_addr));
				data_offset = sizeof(m_load_addr);
			} else {
				data_offset = 0;
			}

			if (firmware < firmware_end) {
				if ((firmware_end - firmware) >= CHUNK_SIZE)
					read = CHUNK_SIZE;
				else
					read = firmware_end - firmware;
				memcpy(&avmwasp->s_packet.payload[data_offset],
				       firmware, read);
				firmware = firmware + CHUNK_SIZE;

				avmwasp->s_packet.packet_start = PACKET_START;
				if (chunk_counter == num_chunks) {
					avmwasp->s_packet.response =
							CMD_START_FIRMWARE;
					memcpy(&avmwasp->s_packet.payload
					       [data_offset + read],
					       &m_load_addr, sizeof(m_load_addr));
					data_offset += sizeof(m_load_addr);
				} else {
					avmwasp->s_packet.command =
							CMD_FIRMWARE_DATA;
				}
				avmwasp->s_packet.counter = packet_counter;

				memcpy(avmwasp->sendbuf + sizeof(struct ethhdr),
				       avmwasp->s_packet.data,
				       WASP_HEADER_LEN + read + data_offset);
				send_len = sizeof(struct ethhdr)
					+ WASP_HEADER_LEN + read + data_offset;
				send_socket_address.sll_halen = ETH_ALEN;
				send_socket_address.sll_ifindex =
							avmwasp->ifindex;

				memset(&send_vec, 0, sizeof(send_vec));
				send_vec.iov_len = send_len;
				send_vec.iov_base = avmwasp->sendbuf;

				memset(&send_socket_hdr, 0,
				       sizeof(send_socket_hdr));
				send_socket_hdr.msg_name = (struct sockaddr *)
							&send_socket_address;
				send_socket_hdr.msg_namelen =
					sizeof(struct sockaddr_ll);

				ret = kernel_sendmsg(avmwasp->send_socket,
						     &send_socket_hdr,
						     &send_vec,
						     1, send_len);
				if (ret < 0) {
					dev_err(&avmwasp->pdev->dev,
						"Error sending to WASP %d\n",
						ret);
					goto err_send;
				}

				packet_counter += COUNTER_INCR;
				chunk_counter++;
			}
		}
	}

err_send:
	avmwasp->send_socket->ops->release(avmwasp->send_socket);
err_recv:
	avmwasp->recv_socket->ops->release(avmwasp->recv_socket);
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
 * Return: 0 on Success, -19 or return code from the called function
 * if any other error occurred in the process of starting and loading
 * the firmware files to the WASP processor
 */
static int avm_wasp_rproc_start(struct rproc *rproc)
{
	struct avm_wasp_rproc *avmwasp = rproc->priv;
	int ret;

	gpio_set_value(avmwasp->startup_gpio,
		       (avmwasp->s_gpio_flg & OF_GPIO_ACTIVE_LOW) ?
		       0 : 1);
	mdelay(WASP_WAIT_SLEEP);
	gpio_set_value(avmwasp->reset_gpio,
		       (avmwasp->r_gpio_flg & OF_GPIO_ACTIVE_LOW) ?
		       1 : 0);
	mdelay(WASP_WAIT_SLEEP);
	gpio_set_value(avmwasp->reset_gpio,
		       (avmwasp->r_gpio_flg & OF_GPIO_ACTIVE_LOW) ?
		       0 : 1);
	mdelay(WASP_WAIT_SLEEP);

	avmwasp->mdio_bus = mdio_find_bus(avmwasp->mdio_bus_id);
	if (!avmwasp->mdio_bus) {
		dev_err(&avmwasp->pdev->dev,
			"wasp-netboot-mdio bus not found\n");
		return -ENODEV;
	}

	ret = avm_wasp_netboot_load_firmware(avmwasp);
	if (ret) {
		put_device(&avmwasp->mdio_bus->dev);
		return ret;
	}

	put_device(&avmwasp->mdio_bus->dev);

	ret = avm_wasp_load_initramfs_image(avmwasp);
	if (ret)
		return ret;

	return 0;
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

	gpio_set_value(avmwasp->startup_gpio,
		       (avmwasp->s_gpio_flg & OF_GPIO_ACTIVE_LOW) ?
		       1 : 0);

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
 * Furthermore during boot its just required to send the firmware to the WASP
 * processor, its not required to keep it in local memory, as the WASP SoC
 * has its own memory.
 *
 * Return: Always 0
 */
static int avm_wasp_rproc_load(struct rproc *rproc, const struct firmware *fw)
{
	return 0;
}

/**
 * avm_wasp_rproc_boot_addr() - store fw from framework in priv
 * @rproc: pointer to the rproc structure
 * @fw: pointer to firmware struct
 *
 * Even though firmware files can be loaded without the remote processor
 * framework, it expects at least one firmware file.
 * This function stores the initramfs image that is loaded by the remote
 * processor framework during boot process into the priv for access by
 * the initramfs load function avm_wasp_load_initramfs_image().
 *
 * Return: Address of initramfs image
 */
static u64 avm_wasp_rproc_boot_addr(struct rproc *rproc,
				    const struct firmware *fw)
{
	struct avm_wasp_rproc *avmwasp = rproc->priv;

	avmwasp->linux_blob = fw;

	return (u64)((u32)fw->data);
}

static const struct rproc_ops avm_wasp_rproc_ops = {
	.start		= avm_wasp_rproc_start,
	.stop		= avm_wasp_rproc_stop,
	.load		= avm_wasp_rproc_load,
	.get_boot_addr	= avm_wasp_rproc_boot_addr,
};

static int avm_wasp_rproc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct avm_wasp_rproc *avmwasp;
	const char *fw_name;
	struct rproc *rproc;
	struct device_node *root_node;
	int ret;
	u32 phandle;
	char *model;

	root_node = of_find_node_by_path("/");
	if (!root_node) {
		dev_err(dev, "No root node in device tree.\n");
		ret = -EFAULT;
		goto err;
	}

	ret = of_property_read_string_index(root_node, "compatible",
					    0, (const char **)&model);
	of_node_put(root_node);
	if (ret) {
		dev_err(dev, "No model in device tree.\n");
		goto err;
	}

	/* check model of host device to determine WASP SoC type */
	if (strstr(model, "3390")) {
		m_model = MODEL_3390;
	} else if (strstr(model, "490")) {
		m_model = MODEL_X490;
	} else {
		dev_err(dev, "No WASP on device.\n");
		ret = -EPERM;
		goto err;
	}

	ret = of_property_read_string(dev->of_node, "wasp-initramfs-image",
				      &fw_name);
	if (ret) {
		dev_err(dev, "No initramfs image for WASP filename given\n");
		goto err;
	}

	rproc = devm_rproc_alloc(dev, "avm,wasp", &avm_wasp_rproc_ops,
				 fw_name, sizeof(*avmwasp));
	if (!rproc) {
		ret = -ENOMEM;
		goto err;
	}

	rproc->auto_boot = true;

	avmwasp = rproc->priv;
	avmwasp->rproc = rproc;
	avmwasp->pdev = pdev;

	ret = of_property_read_string(dev->of_node, "ath9k-firmware",
				      &fw_name);
	if (ret) {
		dev_err(dev, "No ath9k firmware filename given\n");
		goto err;
	}

	ret = avm_wasp_firmware_request(avmwasp, fw_name);
	if (ret) {
		dev_err(dev, "Could not load ath9k firmware\n");
		goto err;
	}
	avm_wasp_firmware_release(avmwasp);

	if (m_model == MODEL_X490) {
		ret = of_property_read_string(dev->of_node, "ath10k-caldata",
					      &fw_name);
		if (ret) {
			dev_err(dev, "No ath10k caldata filename given\n");
			goto err;
		}

		ret = avm_wasp_firmware_request(avmwasp, fw_name);
		if (ret) {
			dev_err(dev, "Could not load ath10k caldata\n");
			goto err;
		}
		avm_wasp_firmware_release(avmwasp);
	}

	ret = of_property_read_u32(dev->of_node, "wasp-initramfs-port",
				   &phandle);
	if (ret) {
		dev_err(dev, "No wasp-initramfs-port given\n");
		goto err;
	} else {
		struct device_node *child = of_find_node_by_phandle(phandle);

		if (!child) {
			dev_err(dev, "Get wasp-initramfs-port child failed\n");
			ret = -ENODEV;
			goto err;
		} else {
			ret = of_property_read_string(child, "label",
						      (const char **)
						      &avmwasp->loader_port);
			of_node_put(child);
			if (ret) {
				dev_err(dev,
					"Get wasp-port-label failed\n");
				goto err;
			}
		}
	}

	ret = of_property_read_u32(dev->of_node, "wasp-netboot-mdio",
				   &phandle);
	if (ret) {
		dev_err(dev, "No wasp-netboot-mdio given\n");
		goto err;
	} else {
		struct device_node *mdio_node =
					of_find_node_by_phandle(phandle);

		if (!mdio_node) {
			dev_err(dev, "Get wasp-netboot-mdio failed\n");
			ret = -ENODEV;
			goto err;
		} else {
			avmwasp->mdio_bus = of_mdio_find_bus(mdio_node);
			of_node_put(mdio_node);
			if (!avmwasp->mdio_bus) {
				dev_err(dev, "mdio bus not found\n");
				ret = -ENODEV;
				goto err;
			}
			avmwasp->mdio_bus_id = avmwasp->mdio_bus->id;
			put_device(&avmwasp->mdio_bus->dev);
		}
	}

	avmwasp->startup_gpio = of_get_named_gpio_flags(dev->of_node,
							"startup-gpio",
							0,
							&avmwasp->s_gpio_flg);
	if (!gpio_is_valid(avmwasp->startup_gpio)) {
		dev_err(dev, "Request wasp-startup gpio failed\n");
		ret = -ENODEV;
		goto err;
	} else {
		ret = devm_gpio_request_one(dev, avmwasp->startup_gpio,
					    (avmwasp->s_gpio_flg &
					    OF_GPIO_ACTIVE_LOW) ?
					    GPIOF_OUT_INIT_LOW :
					    GPIOF_OUT_INIT_HIGH,
					    "wasp-startup");

		if (ret) {
			dev_err(dev, "get wasp-startup gpio failed\n");
			goto err;
		}
	}

	avmwasp->reset_gpio = of_get_named_gpio_flags(dev->of_node,
						      "reset-gpio",
						      0,
						      &avmwasp->r_gpio_flg);
	if (!gpio_is_valid(avmwasp->reset_gpio)) {
		dev_err(dev, "Request wasp-reset gpio failed\n");
		ret = -ENODEV;
		goto err_free_startup_gpio;
	} else {
		ret = devm_gpio_request_one(dev, avmwasp->reset_gpio,
					    (avmwasp->r_gpio_flg &
					    OF_GPIO_ACTIVE_LOW)	?
					    GPIOF_OUT_INIT_LOW :
					    GPIOF_OUT_INIT_HIGH,
					    "wasp-reset");

		if (ret) {
			dev_err(dev, "get wasp-reset gpio failed\n");
			goto err_free_startup_gpio;
		}
	}

	ret = of_property_read_string(dev->of_node, "wasp-netboot-firmware",
				      (const char **)
				      &avmwasp->netboot_firmware);
	if (ret) {
		dev_err(dev, "No WASP network boot firmware filename given\n");
		goto err_free_reset_gpio;
	}

	ret = request_firmware_direct((const struct firmware **)
			&avmwasp->eeprom_blob, avmwasp->netboot_firmware, dev);
	if (ret) {
		dev_err(dev, "Could not load WASP network boot firmware\n");
		goto err_free_reset_gpio;
	}

	if (avmwasp->eeprom_blob->size > 0xffff) {
		dev_err(dev, "WASP network boot firmware too big\n");
		ret = -EINVAL;
		goto err_free_reset_gpio;
	}

	avm_wasp_firmware_release(avmwasp);

	dev_set_drvdata(dev, rproc);

	ret = devm_rproc_add(dev, rproc);
	if (ret) {
		dev_err(dev, "rproc_add failed\n");
		goto err_free_reset_gpio;
	}

	return 0;

err_free_reset_gpio:
	devm_gpio_free(&avmwasp->pdev->dev, avmwasp->reset_gpio);
	gpio_set_value(avmwasp->startup_gpio,
		       (avmwasp->s_gpio_flg & OF_GPIO_ACTIVE_LOW) ?
		       1 : 0);
err_free_startup_gpio:
	devm_gpio_free(&avmwasp->pdev->dev, avmwasp->startup_gpio);
err:
	return ret;
}

static int avm_wasp_rproc_remove(struct platform_device *pdev)
{
	struct rproc *rproc = platform_get_drvdata(pdev);
	struct avm_wasp_rproc *avmwasp = rproc->priv;

	gpio_set_value(avmwasp->startup_gpio,
		       (avmwasp->s_gpio_flg & OF_GPIO_ACTIVE_LOW) ?
		       1 : 0);
	mdelay(WASP_WAIT_SLEEP);
	devm_gpio_free(&avmwasp->pdev->dev, avmwasp->startup_gpio);
	devm_gpio_free(&avmwasp->pdev->dev, avmwasp->reset_gpio);

	return 0;
}

#ifdef CONFIG_PM
static int avm_wasp_rpm_suspend(struct device *dev)
{
	return -EBUSY;
}

static int avm_wasp_rpm_resume(struct device *dev)
{
	return 0;
}
#endif

static const struct of_device_id avm_wasp_rproc_of_match[] = {
	{ .compatible = "avm,wasp", },
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
