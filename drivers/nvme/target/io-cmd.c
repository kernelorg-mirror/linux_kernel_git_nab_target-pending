/*
 * NVMe I/O command implementation.
 * Copyright (c) 2015-2016 HGST, a Western Digital Company.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/blkdev.h>
#include <linux/module.h>
#include <target/target_core_base.h>
#include <target/target_core_backend.h>
#include "nvmet.h"

static void nvmet_complete_ios(struct target_iostate *ios, u16 status)
{
	struct nvmet_req *req = container_of(ios, struct nvmet_req, t_iostate);

	nvmet_req_complete(req, status ? NVME_SC_INTERNAL | NVME_SC_DNR : 0);
}

static inline u32 nvmet_rw_len(struct nvmet_req *req)
{
	return ((u32)le16_to_cpu(req->cmd->rw.length) + 1) <<
			req->ns->blksize_shift;
}

static void nvmet_execute_rw(struct nvmet_req *req)
{
	struct target_iostate *ios = &req->t_iostate;
	struct target_iomem *iomem = &req->t_iomem;
	struct se_device *dev = rcu_dereference_raw(req->ns->dev);
	struct sbc_ops *sbc_ops = dev->transport->sbc_ops;
	sector_t sector;
	enum dma_data_direction data_direction;
	sense_reason_t rc;
	bool fua_write = false, prot_enabled = false;

	if (!sbc_ops || !sbc_ops->execute_rw) {
		nvmet_req_complete(req, NVME_SC_INTERNAL | NVME_SC_DNR);
		return;
	}

	if (!req->sg_cnt) {
		nvmet_req_complete(req, 0);
		return;
	}

	if (req->cmd->rw.opcode == nvme_cmd_write) {
		if (req->cmd->rw.control & cpu_to_le16(NVME_RW_FUA))
			fua_write = true;

		data_direction = DMA_TO_DEVICE;
	} else {
		data_direction = DMA_FROM_DEVICE;
	}

	sector = le64_to_cpu(req->cmd->rw.slba);
	sector <<= (req->ns->blksize_shift - 9);

	ios->t_task_lba = sector;
	ios->data_length = nvmet_rw_len(req);
	ios->data_direction = data_direction;
	iomem->t_data_sg = req->sg;
	iomem->t_data_nents = req->sg_cnt;
	iomem->t_prot_sg = req->prot_sg;
	iomem->t_prot_nents = req->prot_sg_cnt;

	// XXX: Make common between sbc_check_prot and nvme-target
	switch (dev->dev_attrib.pi_prot_type) {
	case TARGET_DIF_TYPE3_PROT:
		ios->reftag_seed = 0xffffffff;
		prot_enabled = true;
		break;
	case TARGET_DIF_TYPE1_PROT:
		ios->reftag_seed = ios->t_task_lba;
		prot_enabled = true;
		break;
	default:
		break;
	}

	if (prot_enabled) {
		ios->prot_type = dev->dev_attrib.pi_prot_type;
		ios->prot_length = dev->prot_length *
				       (le16_to_cpu(req->cmd->rw.length) + 1);
#if 0
		printk("req->cmd->rw.length: %u\n", le16_to_cpu(req->cmd->rw.length));
		printk("nvmet_rw_len: %u\n", nvmet_rw_len(req));
		printk("req->se_cmd.prot_type: %d\n", req->se_cmd.prot_type);
		printk("req->se_cmd.prot_length: %u\n", req->se_cmd.prot_length);
#endif
	}

	ios->se_dev = dev;
	ios->iomem = iomem;
	ios->t_comp_func = &nvmet_complete_ios;

	rc = sbc_ops->execute_rw(ios, iomem->t_data_sg, iomem->t_data_nents,
				 ios->data_direction, fua_write,
				 &nvmet_complete_ios);
}

static void nvmet_execute_flush(struct nvmet_req *req)
{
	struct target_iostate *ios = &req->t_iostate;
	struct se_device *dev = rcu_dereference_raw(req->ns->dev);
	struct sbc_ops *sbc_ops = dev->transport->sbc_ops;
	sense_reason_t rc;

	if (!sbc_ops || !sbc_ops->execute_sync_cache) {
		nvmet_req_complete(req, NVME_SC_INTERNAL | NVME_SC_DNR);
		return;
	}

	ios->se_dev = dev;
	ios->iomem = NULL;
	ios->t_comp_func = &nvmet_complete_ios;

	rc = sbc_ops->execute_sync_cache(ios, false);
}

#if 0
static u16 nvmet_discard_range(struct nvmet_ns *ns,
		struct nvme_dsm_range *range, int type, struct bio **bio)
{
	if (__blkdev_issue_discard(ns->bdev,
			le64_to_cpu(range->slba) << (ns->blksize_shift - 9),
			le32_to_cpu(range->nlb) << (ns->blksize_shift - 9),
			GFP_KERNEL, type, bio))
		return NVME_SC_INTERNAL | NVME_SC_DNR;

	return 0;
}
#endif

static void nvmet_execute_discard(struct nvmet_req *req)
{
#if 0
	struct nvme_dsm_range range;
	struct bio *bio = NULL;
	int type = REQ_WRITE | REQ_DISCARD, i;
	u16 status;

	for (i = 0; i <= le32_to_cpu(req->cmd->dsm.nr); i++) {
		status = nvmet_copy_from_sgl(req, i * sizeof(range), &range,
				sizeof(range));
		if (status)
			break;

		status = nvmet_discard_range(req->ns, &range, type, &bio);
		if (status)
			break;
	}

	if (bio) {
		bio->bi_private = req;
		bio->bi_end_io = nvmet_bio_done;
		if (status) {
			bio->bi_error = -EIO;
			bio_endio(bio);
		} else {
			submit_bio(type, bio);
		}
	} else {
		nvmet_req_complete(req, status);
	}
#endif
}

static void nvmet_execute_dsm(struct nvmet_req *req)
{
	switch (le32_to_cpu(req->cmd->dsm.attributes)) {
	case NVME_DSMGMT_AD:
		nvmet_execute_discard(req);
		return;
	case NVME_DSMGMT_IDR:
	case NVME_DSMGMT_IDW:
	default:
		/* Not supported yet */
		nvmet_req_complete(req, 0);
		return;
	}
}

int nvmet_parse_io_cmd(struct nvmet_req *req)
{
	struct nvme_command *cmd = req->cmd;

	if (unlikely(!(req->sq->ctrl->cc & NVME_CC_ENABLE))) {
		pr_err("nvmet: got io cmd %d while CC.EN == 0\n",
				cmd->common.opcode);
		req->ns = NULL;
		return NVME_SC_CMD_SEQ_ERROR | NVME_SC_DNR;
	}

	if (unlikely(!(req->sq->ctrl->csts & NVME_CSTS_RDY))) {
		pr_err("nvmet: got io cmd %d while CSTS.RDY == 0\n",
				cmd->common.opcode);
		req->ns = NULL;
		return NVME_SC_CMD_SEQ_ERROR | NVME_SC_DNR;
	}

	req->ns = nvmet_find_namespace(req->sq->ctrl, cmd->rw.nsid);
	if (!req->ns)
		return NVME_SC_INVALID_NS | NVME_SC_DNR;

	switch (cmd->common.opcode) {
	case nvme_cmd_read:
	case nvme_cmd_write:
		req->execute = nvmet_execute_rw;
		req->data_len = nvmet_rw_len(req);
		return 0;
	case nvme_cmd_flush:
		req->execute = nvmet_execute_flush;
		req->data_len = 0;
		return 0;
	case nvme_cmd_dsm:
		req->execute = nvmet_execute_dsm;
		req->data_len = le32_to_cpu(cmd->dsm.nr) *
			sizeof(struct nvme_dsm_range);
		return 0;
	default:
		pr_err("nvmet: unhandled cmd %d\n", cmd->common.opcode);
		return NVME_SC_INVALID_OPCODE | NVME_SC_DNR;
	}
}
