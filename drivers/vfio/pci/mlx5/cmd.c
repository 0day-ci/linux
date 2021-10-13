// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */

#include "cmd.h"

int mlx5vf_cmd_suspend_vhca(struct pci_dev *pdev, u16 vhca_id, u16 op_mod)
{
	struct mlx5_core_dev *mdev = mlx5_vf_get_core_dev(pdev);
	u32 out[MLX5_ST_SZ_DW(suspend_vhca_out)] = {};
	u32 in[MLX5_ST_SZ_DW(suspend_vhca_in)] = {};
	int ret;

	if (!mdev)
		return -ENOTCONN;

	MLX5_SET(suspend_vhca_in, in, opcode, MLX5_CMD_OP_SUSPEND_VHCA);
	MLX5_SET(suspend_vhca_in, in, vhca_id, vhca_id);
	MLX5_SET(suspend_vhca_in, in, op_mod, op_mod);

	ret = mlx5_cmd_exec_inout(mdev, suspend_vhca, in, out);
	mlx5_vf_put_core_dev(mdev);
	return ret;
}

int mlx5vf_cmd_resume_vhca(struct pci_dev *pdev, u16 vhca_id, u16 op_mod)
{
	struct mlx5_core_dev *mdev = mlx5_vf_get_core_dev(pdev);
	u32 out[MLX5_ST_SZ_DW(resume_vhca_out)] = {};
	u32 in[MLX5_ST_SZ_DW(resume_vhca_in)] = {};
	int ret;

	if (!mdev)
		return -ENOTCONN;

	MLX5_SET(resume_vhca_in, in, opcode, MLX5_CMD_OP_RESUME_VHCA);
	MLX5_SET(resume_vhca_in, in, vhca_id, vhca_id);
	MLX5_SET(resume_vhca_in, in, op_mod, op_mod);

	ret = mlx5_cmd_exec_inout(mdev, resume_vhca, in, out);
	mlx5_vf_put_core_dev(mdev);
	return ret;
}

int mlx5vf_cmd_query_vhca_migration_state(struct pci_dev *pdev, u16 vhca_id,
					  u32 *state_size)
{
	struct mlx5_core_dev *mdev = mlx5_vf_get_core_dev(pdev);
	u32 out[MLX5_ST_SZ_DW(query_vhca_migration_state_out)] = {};
	u32 in[MLX5_ST_SZ_DW(query_vhca_migration_state_in)] = {};
	int ret;

	if (!mdev)
		return -ENOTCONN;

	MLX5_SET(query_vhca_migration_state_in, in, opcode,
		 MLX5_CMD_OP_QUERY_VHCA_MIGRATION_STATE);
	MLX5_SET(query_vhca_migration_state_in, in, vhca_id, vhca_id);
	MLX5_SET(query_vhca_migration_state_in, in, op_mod, 0);

	ret = mlx5_cmd_exec_inout(mdev, query_vhca_migration_state, in, out);
	if (ret)
		goto end;

	*state_size = MLX5_GET(query_vhca_migration_state_out, out,
			       required_umem_size);

end:
	mlx5_vf_put_core_dev(mdev);
	return ret;
}

int mlx5vf_cmd_get_vhca_id(struct pci_dev *pdev, u16 function_id, u16 *vhca_id)
{
	struct mlx5_core_dev *mdev = mlx5_vf_get_core_dev(pdev);
	u32 in[MLX5_ST_SZ_DW(query_hca_cap_in)] = {};
	int out_size;
	void *out;
	int ret;

	if (!mdev)
		return -ENOTCONN;

	out_size = MLX5_ST_SZ_BYTES(query_hca_cap_out);
	out = kzalloc(out_size, GFP_KERNEL);
	if (!out) {
		ret = -ENOMEM;
		goto end;
	}

	MLX5_SET(query_hca_cap_in, in, opcode, MLX5_CMD_OP_QUERY_HCA_CAP);
	MLX5_SET(query_hca_cap_in, in, other_function, 1);
	MLX5_SET(query_hca_cap_in, in, function_id, function_id);
	MLX5_SET(query_hca_cap_in, in, op_mod,
		 MLX5_SET_HCA_CAP_OP_MOD_GENERAL_DEVICE << 1 |
		 HCA_CAP_OPMOD_GET_CUR);

	ret = mlx5_cmd_exec_inout(mdev, query_hca_cap, in, out);
	if (ret)
		goto err_exec;

	*vhca_id = MLX5_GET(query_hca_cap_out, out,
			    capability.cmd_hca_cap.vhca_id);

err_exec:
	kfree(out);
end:
	mlx5_vf_put_core_dev(mdev);
	return ret;
}

static int _create_state_mkey(struct mlx5_core_dev *mdev, u32 pdn,
			      struct mlx5_vhca_state_data *state, u32 *mkey)
{
	struct sg_dma_page_iter dma_iter;
	int err = 0, inlen;
	__be64 *mtt;
	void *mkc;
	u32 *in;

	inlen = MLX5_ST_SZ_BYTES(create_mkey_in) +
			sizeof(*mtt) * round_up(state->num_pages, 2);

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(create_mkey_in, in, translations_octword_actual_size,
		 DIV_ROUND_UP(state->num_pages, 2));
	mtt = (__be64 *)MLX5_ADDR_OF(create_mkey_in, in, klm_pas_mtt);

	for_each_sgtable_dma_page(&state->mig_data.table.sgt, &dma_iter, 0)
		*mtt++ = cpu_to_be64(sg_page_iter_dma_address(&dma_iter));

	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);
	MLX5_SET(mkc, mkc, access_mode_1_0, MLX5_MKC_ACCESS_MODE_MTT);
	MLX5_SET(mkc, mkc, lr, 1);
	MLX5_SET(mkc, mkc, lw, 1);
	MLX5_SET(mkc, mkc, pd, pdn);
	MLX5_SET(mkc, mkc, bsf_octword_size, 0);
	MLX5_SET(mkc, mkc, qpn, 0xffffff);
	MLX5_SET(mkc, mkc, log_page_size, PAGE_SHIFT);
	MLX5_SET(mkc, mkc, translations_octword_size,
		 DIV_ROUND_UP(state->num_pages, 2));
	MLX5_SET64(mkc, mkc, len, state->num_pages * PAGE_SIZE);
	err = mlx5_core_create_mkey(mdev, mkey, in, inlen);

	kvfree(in);

	return err;
}

struct page *mlx5vf_get_migration_page(struct migration_data *data,
				       unsigned long offset)
{
	unsigned long cur_offset = 0;
	struct scatterlist *sg;
	unsigned int i;

	if (offset < data->last_offset || !data->last_offset_sg) {
		data->last_offset = 0;
		data->last_offset_sg = data->table.sgt.sgl;
		data->sg_last_entry = 0;
	}

	cur_offset = data->last_offset;

	for_each_sg(data->last_offset_sg, sg,
			data->table.sgt.orig_nents - data->sg_last_entry, i) {
		if (offset < sg->length + cur_offset) {
			data->last_offset_sg = sg;
			data->sg_last_entry += i;
			data->last_offset = cur_offset;
			return nth_page(sg_page(sg),
					(offset - cur_offset) / PAGE_SIZE);
		}
		cur_offset += sg->length;
	}
	return NULL;
}

void mlx5vf_reset_vhca_state(struct mlx5_vhca_state_data *state)
{
	struct migration_data *data = &state->mig_data;
	struct sg_page_iter sg_iter;

	if (!data->table.prv)
		goto end;

	/* Undo alloc_pages_bulk_array() */
	for_each_sgtable_page(&data->table.sgt, &sg_iter, 0)
		__free_page(sg_page_iter_page(&sg_iter));
	sg_free_append_table(&data->table);
end:
	memset(state, 0, sizeof(*state));
}

int mlx5vf_add_migration_pages(struct mlx5_vhca_state_data *state,
			       unsigned int npages)
{
	unsigned int to_alloc = npages;
	struct page **page_list;
	unsigned long filled;
	unsigned int to_fill;
	int ret = 0;

	to_fill = min_t(unsigned int, npages, PAGE_SIZE / sizeof(*page_list));
	page_list = kvzalloc(to_fill * sizeof(*page_list), GFP_KERNEL);
	if (!page_list)
		return -ENOMEM;

	do {
		filled = alloc_pages_bulk_array(GFP_KERNEL, to_fill,
						page_list);
		if (!filled) {
			ret = -ENOMEM;
			goto err;
		}
		to_alloc -= filled;
		ret = sg_alloc_append_table_from_pages(
			&state->mig_data.table, page_list, filled, 0,
			filled << PAGE_SHIFT, UINT_MAX, SG_MAX_SINGLE_ALLOC,
			GFP_KERNEL);

		if (ret)
			goto err;
		/* clean input for another bulk allocation */
		memset(page_list, 0, filled * sizeof(*page_list));
		to_fill = min_t(unsigned int, to_alloc,
				PAGE_SIZE / sizeof(*page_list));
	} while (to_alloc > 0);

	kvfree(page_list);
	state->num_pages += npages;

	return 0;

err:
	kvfree(page_list);
	return ret;
}

int mlx5vf_cmd_save_vhca_state(struct pci_dev *pdev, u16 vhca_id,
			       u64 state_size,
			       struct mlx5_vhca_state_data *state)
{
	struct mlx5_core_dev *mdev = mlx5_vf_get_core_dev(pdev);
	u32 out[MLX5_ST_SZ_DW(save_vhca_state_out)] = {};
	u32 in[MLX5_ST_SZ_DW(save_vhca_state_in)] = {};
	u32 pdn, mkey;
	int err;

	if (!mdev)
		return -ENOTCONN;

	err = mlx5_core_alloc_pd(mdev, &pdn);
	if (err)
		goto end;

	err = mlx5vf_add_migration_pages(state,
				DIV_ROUND_UP_ULL(state_size, PAGE_SIZE));
	if (err < 0)
		goto err_alloc_pages;

	err = dma_map_sgtable(mdev->device, &state->mig_data.table.sgt,
			      DMA_FROM_DEVICE, 0);
	if (err)
		goto err_reg_dma;

	err = _create_state_mkey(mdev, pdn, state, &mkey);
	if (err)
		goto err_create_mkey;

	MLX5_SET(save_vhca_state_in, in, opcode,
		 MLX5_CMD_OP_SAVE_VHCA_STATE);
	MLX5_SET(save_vhca_state_in, in, op_mod, 0);
	MLX5_SET(save_vhca_state_in, in, vhca_id, vhca_id);
	MLX5_SET(save_vhca_state_in, in, mkey, mkey);
	MLX5_SET(save_vhca_state_in, in, size, state_size);

	err = mlx5_cmd_exec_inout(mdev, save_vhca_state, in, out);
	if (err)
		goto err_exec;

	state->state_size = state_size;

	mlx5_core_destroy_mkey(mdev, mkey);
	mlx5_core_dealloc_pd(mdev, pdn);
	dma_unmap_sgtable(mdev->device, &state->mig_data.table.sgt,
			  DMA_FROM_DEVICE, 0);
	mlx5_vf_put_core_dev(mdev);

	return 0;

err_exec:
	mlx5_core_destroy_mkey(mdev, mkey);
err_create_mkey:
	dma_unmap_sgtable(mdev->device, &state->mig_data.table.sgt,
			  DMA_FROM_DEVICE, 0);
err_reg_dma:
	mlx5vf_reset_vhca_state(state);
err_alloc_pages:
	mlx5_core_dealloc_pd(mdev, pdn);
end:
	mlx5_vf_put_core_dev(mdev);
	return err;
}

int mlx5vf_cmd_load_vhca_state(struct pci_dev *pdev, u16 vhca_id,
			       struct mlx5_vhca_state_data *state)
{
	struct mlx5_core_dev *mdev = mlx5_vf_get_core_dev(pdev);
	u32 out[MLX5_ST_SZ_DW(save_vhca_state_out)] = {};
	u32 in[MLX5_ST_SZ_DW(save_vhca_state_in)] = {};
	u32 pdn, mkey;
	int err;

	if (!mdev)
		return -ENOTCONN;

	err = mlx5_core_alloc_pd(mdev, &pdn);
	if (err)
		goto end;

	err = dma_map_sgtable(mdev->device, &state->mig_data.table.sgt,
			      DMA_TO_DEVICE, 0);
	if (err)
		goto err_reg;

	err = _create_state_mkey(mdev, pdn, state, &mkey);
	if (err)
		goto err_mkey;

	MLX5_SET(load_vhca_state_in, in, opcode,
		 MLX5_CMD_OP_LOAD_VHCA_STATE);
	MLX5_SET(load_vhca_state_in, in, op_mod, 0);
	MLX5_SET(load_vhca_state_in, in, vhca_id, vhca_id);
	MLX5_SET(load_vhca_state_in, in, mkey, mkey);
	MLX5_SET(load_vhca_state_in, in, size, state->state_size);

	err = mlx5_cmd_exec_inout(mdev, load_vhca_state, in, out);

	mlx5_core_destroy_mkey(mdev, mkey);
err_mkey:
	dma_unmap_sgtable(mdev->device, &state->mig_data.table.sgt,
			  DMA_TO_DEVICE, 0);
err_reg:
	mlx5_core_dealloc_pd(mdev, pdn);
end:
	mlx5_vf_put_core_dev(mdev);
	return err;
}
