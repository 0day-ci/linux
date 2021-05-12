/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2019-2021 Intel Corporation */

#ifndef _NNPDRV_HOSTRES_H
#define _NNPDRV_HOSTRES_H

#include <linux/dma-mapping.h>
#include "device.h"

/**
 * nnp_hostres_alloc() - allocate memory and create host resource
 * @size: Size of the host resource to be created
 * @dir:  Resource direction (read or write or both)
 *
 * This function allocates memory pages and provides host resource handle.
 * The memory is mapped to kernel virtual address.
 * The resource can be Input(read by device), Output(write by device) and both.
 *
 * The return handle can be used as argument to one of the other nnpi_hostres*
 * functions for:
 *    - mapping/unmapping the resource for NNP-I device.
 *    - pointer to the allocated memory can be retrieved by nnp_hostres_vptr()
 *
 * The handle should be released when no longer needed by a call to
 * nnp_hostres_put.
 *
 * Return: pointer to created resource or error value
 */
struct host_resource *nnp_hostres_alloc(size_t size, enum dma_data_direction dir);

/**
 * nnp_hostres_from_usermem() - Creates host resource from user-space memory
 * @user_ptr: user virtual memory to pin
 * @size: size of user buffer to pin
 * @dir: Resource direction (read or write or both)
 *
 * This function pins the provided user memory and create a host resource
 * handle managing this memory.
 * The provided handle can be used the same as the handle created by
 * nnp_hostres_alloc.
 * The resource can be Input(read by device), Output(write by device) and both.
 *
 * The handle should be released when no longer needed by a call to
 * nnp_hostres_put.
 *
 * Return: pointer to created resource or error value
 */
struct host_resource *nnp_hostres_from_usermem(void __user *user_ptr, size_t size,
					       enum dma_data_direction dir);

/**
 * nnp_hostres_map_device() - Maps the host resource to NNP-I device
 * @res: handle to host resource
 * @nnpdev: handle to nnp device struct
 * @use_one_entry: when true will produce ipc dma chain page table descriptor
 *                 of the mapping in a single concurrent dma block.
 *                 otherwise a chain of multiple blocks might be generated.
 * @page_list: returns the dma address of the ipc dma chain page table
 *             descriptor.
 * @total_chunks: returns the total number of elements in the mapping's
 *                sg_table. Can be NULL if this info is not required.
 *
 * This function maps the host resource to be accessible from device
 * and returns the dma page list of DMA addresses packed in format
 * suitable to be used in IPC protocol to be sent to the card.
 *
 * The resource can be mapped to multiple devices.
 *
 * Return: pointer to the mapping object or error on failure.
 */
struct nnpdev_mapping *nnp_hostres_map_device(struct host_resource *res,
					      struct nnp_device *nnpdev,
					      bool use_one_entry,
					      dma_addr_t *page_list,
					      u32 *total_chunks);

/**
 * nnp_hostres_unmap_device() - Unmaps the host resource from NNP-I device
 * @mapping: mapping pointer, returned from nnp_hostres_map_device
 *
 * This function unmaps previously mapped host resource from device.
 */
void nnp_hostres_unmap_device(struct nnpdev_mapping *mapping);

/**
 * nnp_hostres_user_lock() - Lock the host resource to access from userspace
 * @res: handle to host resource
 *
 * This function should be called before user-space application is accessing
 * the host resource content (either for read or write). The function
 * invalidates  or flashes the cpu caches when necessary.
 * The function does *not* impose any synchronization between application and
 * device accesses to the resource memory. Such synchronization is handled
 * in user-space.
 *
 * Return: error on failure.
 */
int nnp_hostres_user_lock(struct host_resource *res);

/**
 * nnp_hostres_user_unlock() - Unlocks the host resource from userspace access
 * @res: handle to host resource
 *
 * This function should be called after user-space application is finished
 * accessing the host resource content (either for read or write). The function
 * invalidates  or flashes the cpu caches when necessary.
 *
 * Return: error on failure.
 */
int nnp_hostres_user_unlock(struct host_resource *res);

/**
 * nnp_hostres_get() - Increases refcount of the hostres
 * @res: handle to host resource
 *
 * This function increases refcount of the host resource.
 */
void nnp_hostres_get(struct host_resource *res);

/**
 * nnp_hostres_put() - Decreases refcount of the hostres
 * @res: handle to host resource
 *
 * This function decreases refcount of the host resource and destroys it
 * when it reaches 0.
 */
void nnp_hostres_put(struct host_resource *res);

/**
 * nnp_hostres_is_input() - Returns if the host resource is input resource
 * @res: handle to host resource
 *
 * This function returns true if the host resource can be read by device.
 * The "input" terminology is used here since such resources are usually
 * used as inputs to device inference network.
 *
 * Return: true if the reasource is readable.
 */
bool nnp_hostres_is_input(struct host_resource *res);

/**
 * nnp_hostres_is_output() - Returns if the host resource is output resource
 * @res: handle to host resource
 *
 * This function returns true if the host resource can be modified by device.
 * The term "output" is used here since usually such resources are used for
 * outputs of device inference network.
 *
 * Return: true if the reasource is writable.
 */
bool nnp_hostres_is_output(struct host_resource *res);

size_t nnp_hostres_size(struct host_resource *res);

/**
 * nnp_hostres_vptr() - returns the virtual pointer to the resource buffer
 * @res: handle to host resource
 *
 * Return: pointer to resource data or NULL if was not allocated by
 * nnp_hostres_alloc()
 */
void *nnp_hostres_vptr(struct host_resource *res);

int nnp_hostres_init_sysfs(struct device *dev);
void nnp_hostres_fini_sysfs(struct device *dev);

#endif
