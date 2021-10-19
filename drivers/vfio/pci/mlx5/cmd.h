/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef MLX5_VFIO_CMD_H
#define MLX5_VFIO_CMD_H

#include <linux/kernel.h>
#include <linux/mlx5/driver.h>

struct migration_data {
	struct sg_append_table table;

	struct scatterlist *last_offset_sg;
	unsigned int sg_last_entry;
	unsigned long last_offset;
};

/* state data of vhca to be used as part of migration flow */
struct mlx5_vhca_state_data {
	u64 state_size;
	u64 num_pages;
	u32 win_start_offset;
	struct migration_data mig_data;
};

int mlx5vf_cmd_suspend_vhca(struct pci_dev *pdev, u16 vhca_id, u16 op_mod);
int mlx5vf_cmd_resume_vhca(struct pci_dev *pdev, u16 vhca_id, u16 op_mod);
int mlx5vf_cmd_query_vhca_migration_state(struct pci_dev *pdev, u16 vhca_id,
					  uint32_t *state_size);
int mlx5vf_cmd_get_vhca_id(struct pci_dev *pdev, u16 function_id, u16 *vhca_id);
int mlx5vf_cmd_save_vhca_state(struct pci_dev *pdev, u16 vhca_id,
			       u64 state_size,
			       struct mlx5_vhca_state_data *state);
void mlx5vf_reset_vhca_state(struct mlx5_vhca_state_data *state);
int mlx5vf_cmd_load_vhca_state(struct pci_dev *pdev, u16 vhca_id,
			       struct mlx5_vhca_state_data *state);
int mlx5vf_add_migration_pages(struct mlx5_vhca_state_data *state,
			       unsigned int npages);
struct page *mlx5vf_get_migration_page(struct migration_data *data,
				       unsigned long offset);
#endif /* MLX5_VFIO_CMD_H */
