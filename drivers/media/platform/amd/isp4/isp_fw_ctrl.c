/*
 * Copyright 2024-2025 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/errno.h>
#include <linux/uaccess.h>
#include <linux/random.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/time.h>
#include "isp_core.h"
#include "isp_fw_ctrl.h"
#include "isp_module_intf.h"
#include "isp_param.h"
#include "isp_debug.h"
#include "isp_common.h"
#include "isp_hw_reg.h"
#include "isp_events.h"
#include "isp_common.h"
#include "isp_hwa.h"
#include "isp_mc_addr_mgr.h"
#include "isp_utils.h"

#define WORK_ITEM_INTERVAL  5 /* ms */

static struct isp_fw_resp_thread_para isp_resp_para[MAX_REAL_FW_RESP_STREAM_NUM];

static struct isp_mapped_buf_info *isp_preview_done(struct isp_context *isp,
						    enum camera_port_id cid,
						    struct meta_info_t *meta,
						    struct frame_done_cb_para *pcb)
{
	struct isp_mapped_buf_info *prev = NULL;
	struct device *dev;

	if (!isp) {
		pr_err("invalid isp context");
		return NULL;
	}

	dev = &isp->amd_cam->pdev->dev;

	if (cid >= CAMERA_PORT_MAX || !meta || !pcb) {
		dev_err(dev, "-><- %s,fail bad param", __func__);
		return prev;
	}

	pcb->preview.status = BUF_DONE_STATUS_ABSENT;
	if (meta->preview.enabled &&
	    (meta->preview.status == BUFFER_STATUS_SKIPPED ||
	     meta->preview.status == BUFFER_STATUS_DONE ||
	     meta->preview.status == BUFFER_STATUS_DIRTY)) {
		struct isp_stream_info *str_info;

		str_info = &isp->sensor_info[cid].str_info[STREAM_ID_PREVIEW];
		prev = (struct isp_mapped_buf_info *)isp_list_get_first(&str_info->buf_in_fw);

		if (!prev) {
			dev_err(dev, "%s,fail null prev", __func__);
		} else if (!prev->sys_img_buf_hdl) {
			dev_err(dev, "%s,fail null prev orig", __func__);
		} else {
			pcb->preview.buf = *prev->sys_img_buf_hdl;

			pcb->preview.status = BUF_DONE_STATUS_SUCCESS;
			{
				u64 mc_exp = prev->y_map_info.mc_addr;
				u64 mc_real = isp_join_addr64
					      (meta->preview.buffer.buf_base_a_lo,
					       meta->preview.buffer.buf_base_a_hi);

				if (mc_exp != mc_real) {
					dev_err(dev, "disorder:0x%llx expt 0x%llx recv",
						mc_exp, mc_real);
				}
			}
		}
	} else if (meta->preview.enabled) {
		dev_err(dev, "%s,fail bad preview status %u(%s)", __func__,
			meta->preview.status,
			isp_dbg_get_buf_done_str(meta->preview.status));
	}

	if (prev)
		isp_unmap_sys_2_mc(isp, prev);

	return prev;
}

static struct isp_mapped_buf_info *isp_video_done(struct isp_context *isp,
						  enum camera_port_id cid,
						  struct meta_info_t *meta,
						  struct frame_done_cb_para *pcb)
{
	struct isp_mapped_buf_info *video = NULL;
	struct device *dev;

	if (!isp) {
		pr_err("invalid isp context");
		return NULL;
	}

	dev = &isp->amd_cam->pdev->dev;

	if (cid >= CAMERA_PORT_MAX || !meta || !pcb) {
		dev_err(dev, "-><- %s,fail bad param", __func__);
		return video;
	}

	pcb->video.status = BUF_DONE_STATUS_ABSENT;
	if (meta->video.enabled &&
	    (meta->video.status == BUFFER_STATUS_SKIPPED ||
	     meta->video.status == BUFFER_STATUS_DONE ||
	     meta->video.status == BUFFER_STATUS_DIRTY)) {
		video = (struct isp_mapped_buf_info *)isp_list_get_first
			(&isp->sensor_info[cid]
			 .str_info[STREAM_ID_VIDEO]
			 .buf_in_fw);

		if (!video) {
			dev_err(dev, "%s,fail null video", __func__);
		} else if (!video->sys_img_buf_hdl) {
			dev_err(dev, "%s,fail null video orig", __func__);
		} else {
			pcb->video.buf = *video->sys_img_buf_hdl;
			pcb->video.status = BUF_DONE_STATUS_SUCCESS;
		}
	} else if (meta->video.enabled) {
		dev_err(dev, "%s,fail bad video status %u(%s)", __func__,
			meta->video.status,
			isp_dbg_get_buf_done_str(meta->video.status));
	}
	if (video)
		isp_unmap_sys_2_mc(isp, video);

	return video;
}

static struct isp_mapped_buf_info *isp_zsl_done(struct isp_context *isp,
						enum camera_port_id cid,
						struct meta_info_t *meta,
						struct frame_done_cb_para *pcb)
{
	struct isp_mapped_buf_info *zsl = NULL;
	struct isp_sensor_info *sif;
	enum buffer_source_t orig_src = BUFFER_SOURCE_INVALID;
	struct device *dev;

	if (!isp) {
		pr_err("invalid isp context");
		return NULL;
	}

	dev = &isp->amd_cam->pdev->dev;

	if (cid >= CAMERA_PORT_MAX || !meta || !pcb) {
		dev_err(dev, "-><- %s,fail bad param", __func__);
		return zsl;
	}

	sif = &isp->sensor_info[cid];
	pcb->zsl.status = BUF_DONE_STATUS_ABSENT;
	orig_src = meta->still.source;

	if (meta->still.enabled &&
	    (meta->still.status == BUFFER_STATUS_SKIPPED ||
	     meta->still.status == BUFFER_STATUS_DONE ||
	     meta->still.status == BUFFER_STATUS_DIRTY)) {
		char *src = "zsl";

		zsl = (struct isp_mapped_buf_info *)isp_list_get_first
		      (&sif->str_info[STREAM_ID_ZSL].buf_in_fw);

		if (!zsl) {
			dev_err(dev, "%s,fail null %s", __func__, src);
		} else if (!zsl->sys_img_buf_hdl) {
			dev_err(dev, "%s,fail null %s orig", __func__, src);
		} else {
			pcb->zsl.buf = *zsl->sys_img_buf_hdl;

			pcb->zsl.status = BUF_DONE_STATUS_SUCCESS;
		}
	} else if (meta->still.enabled) {
		dev_err(dev, "%s,fail bad still status %u(%s)", __func__,
			meta->still.status,
			isp_dbg_get_buf_done_str(meta->still.status));
	}

	if (zsl)
		isp_unmap_sys_2_mc(isp, zsl);

	if (meta)
		meta->still.source = orig_src;
	return zsl;
}

static void *isp_metainfo_get_sys_from_mc(struct isp_context *isp,
					  enum fw_cmd_resp_stream_id fw_stream_id,
					  u64 mc)
{
	struct device *dev;
	u32 i;

	if (!isp) {
		pr_err("invalid isp context");
		return NULL;
	}

	dev = &isp->amd_cam->pdev->dev;

	if (!mc || fw_stream_id >= FW_CMD_RESP_STREAM_ID_MAX) {
		dev_err(dev, "-><- %s, fail bad param", __func__);
		return NULL;
	};

	for (i = 0; i < STREAM_META_BUF_COUNT; i++) {
		if (isp->fw_cmd_resp_strs_info[fw_stream_id].meta_info_buf[i]) {
			if (mc == isp->fw_cmd_resp_strs_info[fw_stream_id]
			    .meta_info_buf[i]->gpu_mc_addr)
				return isp->fw_cmd_resp_strs_info[fw_stream_id]
				       .meta_info_buf[i]->sys_addr;
		}
	}
	return NULL;
};

static enum camera_port_id isp_get_cid_from_stream_id(struct isp_context *isp,
						      enum fw_cmd_resp_stream_id fw_stream_id)
{
	struct device *dev = &isp->amd_cam->pdev->dev;
	enum camera_port_id searched_cid;

	searched_cid = isp->fw_cmd_resp_strs_info[fw_stream_id].cid_owner;

	dev_dbg(dev, "%s get cid:%d for fw_stream_id:%d", __func__, searched_cid, fw_stream_id);

	return searched_cid;
}

static void resend_meta_in_framedone(struct isp_context *isp,
				     enum camera_port_id cid,
				     enum fw_cmd_resp_stream_id fw_stream_id,
				     u64 meta_info_mc, u64 meta_data_mc)
{
	struct cmd_send_buffer buf_type;
	struct device *dev;

	if (!isp) {
		pr_err("invalid isp context");
		return;
	}

	dev = &isp->amd_cam->pdev->dev;

	if (cid >= CAMERA_PORT_MAX) {
		dev_err(dev, "-><- %s,fail bad param", __func__);
		return;
	}

	if (isp->sensor_info[cid].status != START_STATUS_STARTED &&
	    isp->sensor_info[cid].status != START_STATUS_STARTING) {
		if (meta_info_mc)
			isp_fw_ret_indirect_cmd_pl(isp, &isp->fw_indirect_cmd_pl_buf_mgr,
						   meta_info_mc);
		dev_warn(dev, "not working status %i, meta_info 0x%llx, metaData 0x%llx",
			 isp->sensor_info[cid].status, meta_info_mc, meta_data_mc);
		return;
	}

	if (meta_info_mc) {
		memset(&buf_type, 0, sizeof(buf_type));
		buf_type.buffer_type = BUFFER_TYPE_META_INFO;
		buf_type.buffer.buf_tags = 0;
		buf_type.buffer.vmid_space.bit.vmid = 0;
		buf_type.buffer.vmid_space.bit.space = ADDR_SPACE_TYPE_GPU_VA;
		isp_split_addr64(meta_info_mc, &buf_type.buffer.buf_base_a_lo,
				 &buf_type.buffer.buf_base_a_hi);
		buf_type.buffer.buf_size_a = META_INFO_BUF_SIZE;
		if (isp_send_fw_cmd(isp, CMD_ID_SEND_BUFFER, fw_stream_id,
				    FW_CMD_PARA_TYPE_DIRECT, &buf_type,
				    sizeof(buf_type))) {
			dev_err(dev, "%s(%u) fail send meta_info 0x%llx",
				__func__, cid, meta_info_mc);
			isp_fw_ret_indirect_cmd_pl(isp, &isp->fw_indirect_cmd_pl_buf_mgr,
						   meta_info_mc);
		} else {
			dev_dbg(dev, "%s(%u), resend meta_info 0x%llx",
				__func__, cid, meta_info_mc);
		}
	}

	if (meta_data_mc) {
		memset(&buf_type, 0, sizeof(buf_type));
		buf_type.buffer_type = BUFFER_TYPE_META_DATA;
		buf_type.buffer.buf_tags = 0;
		buf_type.buffer.vmid_space.bit.vmid = 0;
		buf_type.buffer.vmid_space.bit.space = ADDR_SPACE_TYPE_GPU_VA;
		isp_split_addr64(meta_data_mc, &buf_type.buffer.buf_base_a_lo,
				 &buf_type.buffer.buf_base_a_hi);
		buf_type.buffer.buf_size_a = META_DATA_BUF_SIZE;
		if (isp_send_fw_cmd(isp, CMD_ID_SEND_BUFFER, fw_stream_id,
				    FW_CMD_PARA_TYPE_DIRECT, &buf_type,
				    sizeof(buf_type))) {
			dev_err(dev, "%s(%u) fail send metaData 0x%llx",
				__func__, cid, meta_data_mc);
			isp_fw_ret_indirect_cmd_pl(isp, &isp->fw_indirect_cmd_pl_buf_mgr, meta_data_mc);
		} else {
			dev_dbg(dev, "%s(%u), resend metaData 0x%llx",
				__func__, cid, meta_data_mc);
		}
	}
}

static u32 compute_check_sum(u8 *buf, u32 buf_size)
{
	u32 i;
	u32 checksum = 0;
	u32 *buffer;
	u8 *surplus_ptr;

	buffer = (u32 *)buf;
	for (i = 0; i < buf_size / sizeof(u32); i++)
		checksum += buffer[i];

	surplus_ptr = (u8 *)&buffer[i];
	/* add surplus data crc checksum */
	for (i = 0; i < buf_size % sizeof(u32); i++)
		checksum += surplus_ptr[i];

	return checksum;
}

static u32 get_nxt_cmd_seq_num(struct isp_context *isp)
{
	u32 seq_num;

	if (!isp)
		return 1;

	seq_num = isp->host2fw_seq_num++;
	return seq_num;
}

static bool is_fw_cmd_supported(u32 cmd)
{
	switch (cmd) {
	case CMD_ID_GET_FW_VERSION:
	case CMD_ID_SET_LOG_LEVEL:
	case CMD_ID_SET_LOG_MODULE:
	case CMD_ID_SET_LOG_MODULE_LEVEL:
	case CMD_ID_SEND_BUFFER:
	case CMD_ID_SET_OUT_CHAN_PROP:
	case CMD_ID_SET_STREAM_CONFIG:
	case CMD_ID_START_STREAM:
	case CMD_ID_STOP_STREAM:
	case CMD_ID_ENABLE_OUT_CHAN:
	case CMD_ID_SET_OUT_CHAN_FRAME_RATE_RATIO:
	case CMD_ID_SET_3A_ROI:
	case CMD_ID_ENABLE_PREFETCH:
		return true;
	default:
		return false;
	}
}

void isp_boot_disable_ccpu(struct isp_context *isp)
{
	struct device *dev = &isp->amd_cam->pdev->dev;
	u32 reg_val;

	reg_val = isp_hwa_rreg(isp, ISP_CCPU_CNTL);
	dev_dbg(dev, "rd ISP_CCPU_CNTL 0x%x", reg_val);
	reg_val |= ISP_CCPU_CNTL__CCPU_HOST_SOFT_RST_MASK;
	dev_dbg(dev, "wr ISP_CCPU_CNTL 0x%x", reg_val);
	isp_hwa_wreg(isp, ISP_CCPU_CNTL, reg_val);
	usleep_range(100, 150);
	reg_val = isp_hwa_rreg(isp, ISP_SOFT_RESET);
	dev_dbg(dev, "rd ISP_SOFT_RESET 0x%x", reg_val);
	reg_val |= ISP_SOFT_RESET__CCPU_SOFT_RESET_MASK;
	dev_dbg(dev, "wr ISP_SOFT_RESET 0x%x", reg_val);
	/* disable CCPU */
	isp_hwa_wreg(isp, ISP_SOFT_RESET, reg_val);
}

void isp_boot_enable_ccpu(struct isp_context *isp)
{
	struct device *dev = &isp->amd_cam->pdev->dev;
	u32 reg_val;

	reg_val = isp_hwa_rreg(isp, ISP_SOFT_RESET);
	dev_dbg(dev, "rd ISP_SOFT_RESET 0x%x", reg_val);
	reg_val &= (~ISP_SOFT_RESET__CCPU_SOFT_RESET_MASK);
	dev_dbg(dev, "rd ISP_SOFT_RESET 0x%x", reg_val);
	isp_hwa_wreg(isp, ISP_SOFT_RESET, reg_val); /* bus reset */
	usleep_range(100, 150);
	reg_val = isp_hwa_rreg(isp, ISP_CCPU_CNTL);
	dev_dbg(dev, "rd ISP_CCPU_CNTL 0x%x", reg_val);
	reg_val &= (~ISP_CCPU_CNTL__CCPU_HOST_SOFT_RST_MASK);
	dev_dbg(dev, "rd ISP_CCPU_CNTL 0x%x", reg_val);
	isp_hwa_wreg(isp, ISP_CCPU_CNTL, reg_val);
}

int isp_boot_fw_init(struct isp_context *isp)
{
	struct device *dev = &isp->amd_cam->pdev->dev;
	u64 log_addr;
	u32 log_len = ISP_LOGRB_SIZE;

	if (!isp->fw_running_buf) {
		isp->fw_running_buf =
			isp_gpu_mem_alloc(isp, log_len);

		if (isp->fw_running_buf) {
			dev_dbg(dev, "size %u, allocate gpu mem suc", log_len);
		} else {
			dev_err(dev, "size %u, fail to allocate gpu mem", log_len);
			return -ENOMEM;
		}
	}

	log_addr = isp->fw_running_buf->gpu_mc_addr;
	isp->fw_log_buf = (u8 *)isp->fw_running_buf->sys_addr;
	isp->fw_log_buf_len = log_len;

	isp_hwa_wreg(isp, ISP_LOG_RB_BASE_HI0, ((log_addr >> 32) & 0xffffffff));
	isp_hwa_wreg(isp, ISP_LOG_RB_BASE_LO0, (log_addr & 0xffffffff));
	isp_hwa_wreg(isp, ISP_LOG_RB_SIZE0, log_len);

	dev_dbg(dev, "ISP_LOG_RB_BASE_HI=0x%08x",
		isp_hwa_rreg(isp, ISP_LOG_RB_BASE_HI0));
	dev_dbg(dev, "ISP_LOG_RB_BASE_LO=0x%08x",
		isp_hwa_rreg(isp, ISP_LOG_RB_BASE_LO0));
	dev_dbg(dev, "ISP_LOG_RB_SIZE=0x%08x",
		isp_hwa_rreg(isp, ISP_LOG_RB_SIZE0));

	isp_hwa_wreg(isp, ISP_LOG_RB_WPTR0, 0x0);
	isp_hwa_wreg(isp, ISP_LOG_RB_RPTR0, 0x0);

	return 0;
}

int isp_boot_cmd_resp_rb_init(struct isp_context *isp)
{
	struct device *dev = &isp->amd_cam->pdev->dev;
	u32 i;
	u32 total_size;

	if (!isp->fw_cmd_resp_buf) {
		total_size = RB_PMBMAP_MEM_SIZE;

		isp->fw_cmd_resp_buf =
			isp_gpu_mem_alloc(isp, total_size);
		if (isp->fw_cmd_resp_buf) {
			dev_dbg(dev, "size %u, allocate gpu mem suc", total_size);
		} else {
			dev_err(dev, "size %u, fail to allocate gpu mem", total_size);
			return -ENOMEM;
		}
	}
	for (i = 0; i < ISP_FW_CMD_BUF_COUNT; i++)
		isp_fw_buf_get_cmd_base(isp, i, &isp->fw_cmd_buf_sys[i],
					&isp->fw_cmd_buf_mc[i],
					&isp->fw_cmd_buf_size[i]);
	for (i = 0; i < ISP_FW_RESP_BUF_COUNT; i++)
		isp_fw_buf_get_resp_base(isp, i, &isp->fw_resp_buf_sys[i],
					 &isp->fw_resp_buf_mc[i],
					 &isp->fw_resp_buf_size[i]);

	for (i = 0; i < ISP_FW_CMD_BUF_COUNT; i++)
		isp_init_fw_ring_buf(isp, i, 1);
	for (i = 0; i < ISP_FW_RESP_BUF_COUNT; i++)
		isp_init_fw_ring_buf(isp, i, 0);

	return 0;
}

int isp_boot_wait_fw_ready(struct isp_context *isp, u32 isp_status_addr)
{
	struct device *dev = &isp->amd_cam->pdev->dev;
	u32 reg_val;
	u32 timeout = 0;
	u32 fw_ready_timeout;
	u32 interval_ms = 1;
	u32 timeout_ms = 100;

	fw_ready_timeout = timeout_ms / interval_ms;

	/* wait for FW initialize done! */
	while (timeout < fw_ready_timeout) {
		reg_val = isp_hwa_rreg(isp, isp_status_addr);
		dev_dbg(dev, "ISP_STATUS(0x%x):0x%x", isp_status_addr, reg_val);

		if (reg_val & ISP_STATUS__CCPU_REPORT_MASK) {
			dev_dbg(dev, "CCPU bootup succeeds!");
			return 0;
		}

		msleep(interval_ms);
		timeout++;
	}

	dev_err(dev, "CCPU bootup fails!");

	return -ETIME;
}

int isp_boot_isp_fw_boot(struct isp_context *isp)
{
	struct device *dev = &isp->amd_cam->pdev->dev;
	int ret;

	if (ISP_GET_STATUS(isp) != ISP_STATUS_PWR_ON) {
		dev_err(dev, "invalid isp power status %d", ISP_GET_STATUS(isp));
		return -EINVAL;
	}

	isp_hwa_wreg(isp, ISP_POWER_STATUS, 0x7);
	isp_boot_disable_ccpu(isp);

	ret = isp_boot_fw_init(isp);
	if (ret) {
		dev_err(dev, "0:isp_boot_fw_init failed:%d", ret);
		return ret;
	};

	ret = isp_boot_cmd_resp_rb_init(isp);
	if (ret) {
		dev_err(dev, "1:isp_boot_cmd_resp_rb_init failed:%d", ret);
		return ret;
	}

	/* clear register */
	isp_hwa_wreg(isp, ISP_STATUS, 0x0);

	isp_boot_enable_ccpu(isp);

	if (isp_boot_wait_fw_ready(isp, ISP_STATUS)) {
		dev_err(dev, "ccpu fail by bootup timeout");
		isp_boot_disable_ccpu(isp);
		return -EINVAL;
	}

	/* enable interrupt */
	isp_hwa_wreg(isp, ISP_SYS_INT0_EN, FW_RESP_RB_IRQ_EN_MASK);
	dev_dbg(dev, "ISP_SYS_INT0_EN=0x%x", isp_hwa_rreg(isp, ISP_SYS_INT0_EN));

	ISP_SET_STATUS(isp, ISP_STATUS_FW_RUNNING);
	dev_info(dev, "ISP FW boot suc!");
	return 0;
}

int isp_get_f2h_resp(struct isp_context *isp,
		     enum fw_cmd_resp_stream_id stream,
		     struct resp_t *response)
{
	struct device *dev = &isp->amd_cam->pdev->dev;
	u32 rd_ptr;
	u32 wr_ptr;
	u32 rd_ptr_dbg;
	u32 wr_ptr_dbg;
	u64 mem_addr;
	u32 rreg;
	u32 wreg;
	u32 checksum;
	u32 len;
	void **mem_sys;

	isp_get_resp_buf_regs(isp, stream, &rreg, &wreg, NULL, NULL, NULL);
	isp_fw_buf_get_resp_base(isp, stream,
				 (u64 *)&mem_sys, &mem_addr, &len);

	rd_ptr = isp_hwa_rreg(isp, rreg);
	wr_ptr = isp_hwa_rreg(isp, wreg);
	rd_ptr_dbg = rd_ptr;
	wr_ptr_dbg = wr_ptr;

	if (rd_ptr > len) {
		dev_err(dev, "%s: fail %s(%u),rd_ptr %u(should<=%u),wr_ptr %u",
			__func__, isp_dbg_get_stream_str(stream),
			stream, rd_ptr, len, wr_ptr);
		return -EINVAL;
	}

	if (wr_ptr > len) {
		dev_err(dev, "%s: fail %s(%u),wr_ptr %u(should<=%u), rd_ptr %u",
			__func__, isp_dbg_get_stream_str(stream),
			stream, wr_ptr, len, rd_ptr);
		return -EINVAL;
	}

	if (rd_ptr < wr_ptr) {
		if ((wr_ptr - rd_ptr) >= (sizeof(struct resp_t))) {
			memcpy((u8 *)response, (u8 *)mem_sys + rd_ptr,
			       sizeof(struct resp_t));

			rd_ptr += sizeof(struct resp_t);
			if (rd_ptr < len) {
				isp_hwa_wreg(isp, rreg, rd_ptr);
			} else {
				dev_err(dev, "%s(%u),rd %u(should<=%u),wr %u",
					isp_dbg_get_stream_str(stream),
					stream, rd_ptr, len, wr_ptr);
				return -EINVAL;
			}
			goto out;
		} else {
			dev_err(dev, "sth wrong with wptr and rptr");
			return -EINVAL;
		}
	} else if (rd_ptr > wr_ptr) {
		u32 size;
		u8 *dst;
		u64 src_addr;

		dst = (u8 *)response;

		src_addr = mem_addr + rd_ptr;
		size = len - rd_ptr;
		if (size > sizeof(struct resp_t)) {
			mem_addr += rd_ptr;
			memcpy((u8 *)response,
			       (u8 *)(mem_sys) + rd_ptr,
			       sizeof(struct resp_t));
			rd_ptr += sizeof(struct resp_t);
			if (rd_ptr < len) {
				isp_hwa_wreg(isp, rreg, rd_ptr);
			} else {
				dev_err(dev, "%s(%u),rd %u(should<=%u),wr %u",
					isp_dbg_get_stream_str(stream),
					stream, rd_ptr, len, wr_ptr);
				return -EINVAL;
			}
			goto out;
		} else {
			if ((size + wr_ptr) < (sizeof(struct resp_t))) {
				dev_err(dev, "sth wrong with wptr and rptr1");
				return -EINVAL;
			}

			memcpy(dst, (u8 *)(mem_sys) + rd_ptr, size);

			dst += size;
			src_addr = mem_addr;
			size = sizeof(struct resp_t) - size;
			if (size)
				memcpy(dst, (u8 *)(mem_sys), size);
			rd_ptr = size;
			if (rd_ptr < len) {
				isp_hwa_wreg(isp, rreg, rd_ptr);
			} else {
				dev_err(dev, "%s(%u),rd %u(should<=%u),wr %u",
					isp_dbg_get_stream_str(stream),
					stream, rd_ptr, len, wr_ptr);
				return -EINVAL;
			}
			goto out;
		}
	} else {/* if (rd_ptr == wr_ptr) */
		return -ETIME;
	}

out:
	checksum = compute_check_sum((u8 *)response,
				     (sizeof(struct resp_t) - 4));

	if (checksum != response->resp_check_sum) {
		dev_err(dev, "resp checksum[0x%x],should 0x%x,rdptr %u,wrptr %u",
			checksum, response->resp_check_sum,
			rd_ptr_dbg, wr_ptr_dbg);

		dev_err(dev, "%s(%u), seqNo %u, resp_id %s(0x%x)",
			isp_dbg_get_stream_str(stream), stream,
			response->resp_seq_num,
			isp_dbg_get_resp_str(response->resp_id),
			response->resp_id);

		return -EINVAL;
	}

	return 0;
}

int isp_send_fw_cmd_ex(struct isp_context *isp,
		       enum camera_port_id cam_id,
		       u32 cmd_id,
		       enum fw_cmd_resp_stream_id stream,
		       enum fw_cmd_para_type directcmd,
		       void *package,
		       u32 package_size,
		       struct isp_event *evt,
		       u32 *seq,
		       void *resp_pl,
		       u32 *resp_pl_len)
{
	struct device *dev = &isp->amd_cam->pdev->dev;
	u64 package_base = 0;
	u64 pack_sys = 0;
	u32 pack_len;
	int ret = -EINVAL;
	u32 seq_num;
	struct isp_cmd_element command_element = { 0 };
	struct isp_cmd_element *cmd_ele = NULL;
	u32 sleep_count;
	struct cmd_t cmd;
	struct cmd_param_package_t *pkg;
	struct isp_gpu_mem_info *gpu_mem = NULL;

	if (directcmd && package_size > sizeof(cmd.cmd_param)) {
		dev_err(dev, "fail pkgsize(%u)>%lu cmd:0x%x,stream %d",
			package_size, sizeof(cmd.cmd_param), cmd_id, stream);
		return ret;
	}

	if (package_size && !package) {
		dev_err(dev, "-><- %s, fail null pkg cmd:0x%x,stream %d",
			__func__, cmd_id, stream);
		return ret;
	}
	/* if commands need to be ignored for debug for fw not support list them here */
	if (!is_fw_cmd_supported(cmd_id)) {
		dev_warn(dev, "cmd:%s(0x%08x) not supported,ret directly",
			 isp_dbg_get_cmd_str(cmd_id), cmd_id);
		if (evt)
			isp_event_signal(0, evt);

		return 0;
	}

	/* Semaphore check */
	if (!isp_semaphore_acquire(isp)) {
		dev_err(dev, "fail acquire isp semaphore cmd:0x%x,stream %d",
			cmd_id, stream);
		return -ETIME;
	}

	mutex_lock(&isp->command_mutex);
	sleep_count = 0;
	while (1) {
		if (no_fw_cmd_ringbuf_slot(isp, stream)) {
			u32 rreg;
			u32 wreg;
			u32 len;
			u32 rd_ptr, wr_ptr;

			if (sleep_count < MAX_SLEEP_COUNT) {
				msleep(MAX_SLEEP_TIME);
				dev_dbg(dev, "sleep for no cmd ringbuf slot");
				sleep_count++;
				continue;
			}
			isp_get_cmd_buf_regs(isp, stream, &rreg, &wreg,
					     NULL, NULL, NULL);
			isp_fw_buf_get_cmd_base(isp, stream,
						NULL, NULL, &len);

			rd_ptr = isp_hwa_rreg(isp, rreg);
			wr_ptr = isp_hwa_rreg(isp, wreg);
			dev_err(dev, "fail no cmdslot cid:%d,stream %s(%d)",
				cmd_id,
				isp_dbg_get_stream_str(stream),
				stream);
			dev_err(dev, "rreg %u,wreg %u,len %u",
				rd_ptr, wr_ptr, len);

			ret = -ETIME;
			goto busy_out;
		}
		break;
	}

	memset(&cmd, 0, sizeof(cmd));
	cmd.cmd_id = cmd_id;
	switch (stream) {
	case FW_CMD_RESP_STREAM_ID_1:
		cmd.cmd_stream_id = STREAM_ID_1;
		break;
	case FW_CMD_RESP_STREAM_ID_2:
		cmd.cmd_stream_id = STREAM_ID_2;
		break;
	case FW_CMD_RESP_STREAM_ID_3:
		cmd.cmd_stream_id = STREAM_ID_3;
		break;
	default:
		cmd.cmd_stream_id = (u16)STREAM_ID_INVALID;
		break;
	}

	if (directcmd) {
		if (package && package_size)
			memcpy(cmd.cmd_param, package, package_size);
	} else if (package_size <= isp_get_cmd_pl_size()) {
		ret = isp_fw_get_nxt_indirect_cmd_pl(isp, &isp->fw_indirect_cmd_pl_buf_mgr,
						     &pack_sys, &package_base, &pack_len);

		if (ret) {
			dev_err(dev, "-><- %s,no enough pkg buf(0x%08x)",
				__func__, cmd_id);
			goto failure_out;
		}
		memcpy((void *)pack_sys, package, package_size);

		pkg = (struct cmd_param_package_t *)cmd.cmd_param;
		isp_split_addr64(package_base, &pkg->package_addr_lo,
				 &pkg->package_addr_hi);
		pkg->package_size = package_size;
		pkg->package_check_sum = compute_check_sum((u8 *)package, package_size);
	} else {
		dev_err(dev, "fail too big indCmdPlSize %u,max %u,camId %d",
			package_size, isp_get_cmd_pl_size(), cmd_id);
		ret = -EFAULT;
		goto failure_out;
	}

	seq_num = get_nxt_cmd_seq_num(isp);
	cmd.cmd_seq_num = seq_num;
	cmd.cmd_check_sum = compute_check_sum((u8 *)&cmd, sizeof(cmd) - 1);

	if (seq)
		*seq = seq_num;
	command_element.seq_num = seq_num;
	command_element.cmd_id = cmd_id;
	command_element.mc_addr = package_base;
	command_element.evt = evt;
	command_element.gpu_pkg = gpu_mem;
	command_element.resp_payload = resp_pl;
	command_element.resp_payload_len = resp_pl_len;
	command_element.stream = stream;
	command_element.i2c_reg_addr = (u16)I2C_REGADDR_NULL;
	command_element.cam_id = cam_id;

	cmd_ele = isp_append_cmd_2_cmdq(isp, &command_element);
	if (IS_ERR_OR_NULL(cmd_ele)) {
		dev_err(dev, "-><- %s, fail for isp_append_cmd_2_cmdq", __func__);
		ret = PTR_ERR(cmd_ele);
		goto failure_out;
	}

	/* same cmd log format as FW team's,
	 * so it'll be easy to compare and debug if there is sth wrong.
	 */
	dev_dbg(dev, "cmd_id = 0x%08x, name = %s",
		cmd_id, isp_dbg_get_cmd_str(cmd_id));
	dev_dbg(dev, "cmd_stream_id = %u", cmd.cmd_stream_id);
	dev_dbg(dev, "cmd_param[0]: 0x%08x, 0x%08x, 0x%08x, 0x%08x",
		cmd.cmd_param[0], cmd.cmd_param[1],
		cmd.cmd_param[2], cmd.cmd_param[3]);
	dev_dbg(dev, "cmd_param[4]: 0x%08x, 0x%08x, 0x%08x, 0x%08x",
		cmd.cmd_param[4], cmd.cmd_param[5],
		cmd.cmd_param[6], cmd.cmd_param[7]);
	dev_dbg(dev, "cmd_param[8]: 0x%08x, 0x%08x, 0x%08x, 0x%08x",
		cmd.cmd_param[8], cmd.cmd_param[9],
		cmd.cmd_param[10], cmd.cmd_param[11]);

	if (cmd_id == CMD_ID_SEND_BUFFER) {
		struct cmd_send_buffer *p = (struct cmd_send_buffer *)package;
		u32 total = p->buffer.buf_size_a + p->buffer.buf_size_b +
			    p->buffer.buf_size_c;
		u64 y = isp_join_addr64
			(p->buffer.buf_base_a_lo, p->buffer.buf_base_a_hi);
		u64 u = isp_join_addr64
			(p->buffer.buf_base_b_lo, p->buffer.buf_base_b_hi);
		u64 v = isp_join_addr64
			(p->buffer.buf_base_c_lo, p->buffer.buf_base_c_hi);

		dev_dbg(dev, "%s(0x%08x:%s) %s,sn:%u,%s,0x%llx,0x%llx,0x%llx,%u",
			isp_dbg_get_cmd_str(cmd_id), cmd_id,
			isp_dbg_get_stream_str(stream),
			directcmd ? "direct" : "indirect", seq_num,
			isp_dbg_get_buf_type(p->buffer_type),
			y, u, v, total);

	} else {
		dev_dbg(dev, "%s(0x%08x:%s)%s,sn:%u",
			isp_dbg_get_cmd_str(cmd_id), cmd_id,
			isp_dbg_get_stream_str(stream),
			directcmd ? "direct" : "indirect", seq_num);
	}

	isp_get_cur_time_tick(&cmd_ele->send_time);
	ret = insert_isp_fw_cmd(isp, stream, &cmd);
	if (ret) {
		dev_err(dev, "%s: fail for insert_isp_fw_cmd camId %s(0x%08x)",
			__func__, isp_dbg_get_cmd_str(cmd_id), cmd_id);
		isp_rm_cmd_from_cmdq(isp, cmd_ele->seq_num,
				     cmd_ele->cmd_id, false);
		goto failure_out;
	}

	mutex_unlock(&isp->command_mutex);
	isp_semaphore_release(isp);

	return ret;

failure_out:
	if (package_base)
		isp_fw_ret_indirect_cmd_pl(isp, &isp->fw_indirect_cmd_pl_buf_mgr,
					   package_base);
	kfree(cmd_ele);

busy_out:
	mutex_unlock(&isp->command_mutex);
	isp_semaphore_release(isp);

	return ret;
}

int isp_send_fw_cmd(struct isp_context *isp,
		    u32 cmd_id,
		    enum fw_cmd_resp_stream_id stream,
		    enum fw_cmd_para_type directcmd,
		    void *package,
		    u32 package_size)
{
	struct device *dev = &isp->amd_cam->pdev->dev;

	if (stream >= FW_CMD_RESP_STREAM_ID_MAX) {
		dev_err(dev, "%s: invalid fw strId:%d", __func__, stream);
		return -EINVAL;
	}

	return isp_send_fw_cmd_ex(isp, CAMERA_PORT_MAX,
				  cmd_id, stream, directcmd, package,
				  package_size, NULL, NULL, NULL, NULL);
}

int isp_send_fw_cmd_sync(struct isp_context *isp,
			 u32 cmd_id,
			 enum fw_cmd_resp_stream_id stream,
			 enum fw_cmd_para_type directcmd,
			 void *package,
			 u32 package_size,
			 u32 timeout /* in ms */,
			 void *resp_pl,
			 u32 *resp_pl_len)
{
	struct device *dev = &isp->amd_cam->pdev->dev;
	int ret;
	struct isp_event evt;
	u32 seq;

	if (stream >= FW_CMD_RESP_STREAM_ID_MAX) {
		dev_err(dev, "%s: invalid fw strId:%d", __func__, stream);
		return -EINVAL;
	}

	isp_event_init(&evt, 1, 0);

	ret = isp_send_fw_cmd_ex(isp, CAMERA_PORT_MAX,
				 cmd_id, stream, directcmd, package,
				 package_size, &evt, &seq,
				 resp_pl, resp_pl_len);

	if (ret) {
		dev_err(dev, "%s: fail(%d) send cmd", __func__, ret);
		return ret;
	}

	dev_dbg(dev, "before wait cmd:0x%x,evt:%d", cmd_id, evt.event);
	ret = isp_event_wait(&evt, timeout);
	dev_dbg(dev, "after wait cmd:0x%x,evt:%d", cmd_id, evt.event);

	if (ret)
		dev_err(dev, "%s: fail(%d) timeout", __func__, ret);

	if (ret == -ETIME) {
		struct isp_cmd_element *ele;

		ele = isp_rm_cmd_from_cmdq(isp, seq, cmd_id, false);
		if (ele) {
			if (ele->mc_addr)
				isp_fw_ret_indirect_cmd_pl(isp, &isp->fw_indirect_cmd_pl_buf_mgr,
							   ele->mc_addr);

			kfree(ele);
		}
	}

	return ret;
}

bool no_fw_cmd_ringbuf_slot(struct isp_context *isp,
			    enum fw_cmd_resp_stream_id cmd_buf_idx)
{
	u32 rreg;
	u32 wreg;
	u32 rd_ptr, wr_ptr;
	u32 new_wr_ptr;
	u32 len;

	isp_get_cmd_buf_regs(isp, cmd_buf_idx, &rreg, &wreg, NULL, NULL, NULL);
	isp_fw_buf_get_cmd_base(isp, cmd_buf_idx,
				NULL, NULL, &len);

	rd_ptr = isp_hwa_rreg(isp, rreg);
	wr_ptr = isp_hwa_rreg(isp, wreg);

	new_wr_ptr = wr_ptr + sizeof(struct cmd_t);

	if (wr_ptr >= rd_ptr) {
		if (new_wr_ptr < len) {
			return 0;
		} else if (new_wr_ptr == len) {
			if (rd_ptr == 0)
				return true;
			else
				return false;
		} else {
			new_wr_ptr -= len;

			if (new_wr_ptr < rd_ptr)
				return false;
			else
				return true;
		}
	} else {
		if (new_wr_ptr < rd_ptr)
			return false;
		else
			return true;
	}
}

int insert_isp_fw_cmd(struct isp_context *isp,
		      enum fw_cmd_resp_stream_id stream,
		      struct cmd_t *cmd)
{
	struct device *dev = &isp->amd_cam->pdev->dev;
	u64 mem_sys;
	u64 mem_addr;
	u32 rreg;
	u32 wreg;
	u32 wr_ptr;
	u32 rd_ptr;
	u32 len;

	if (!isp || !cmd) {
		dev_err(dev, "%s: fail invalid ctx or cmd", __func__);
		return -EINVAL;
	}

	if (stream > FW_CMD_RESP_STREAM_ID_3) {
		dev_err(dev, "%s: fail bad stream id[%d]",
			__func__, stream);
		return -EINVAL;
	}

	switch (cmd->cmd_id) {
	case CMD_ID_GET_FW_VERSION:
	case CMD_ID_SET_LOG_LEVEL:
		stream = FW_CMD_RESP_STREAM_ID_GLOBAL;
		break;
	default:
		break;
	}

	isp_get_cmd_buf_regs(isp, stream, &rreg, &wreg, NULL, NULL, NULL);
	isp_fw_buf_get_cmd_base(isp, stream,
				&mem_sys, &mem_addr, &len);

	if (no_fw_cmd_ringbuf_slot(isp, stream)) {
		dev_err(dev, "%s: fail no cmdslot %s(%d)", __func__,
			isp_dbg_get_stream_str(stream), stream);
		return -EINVAL;
	}

	wr_ptr = isp_hwa_rreg(isp, wreg);
	rd_ptr = isp_hwa_rreg(isp, rreg);

	if (rd_ptr > len) {
		dev_err(dev, "%s: fail %s(%u),rd_ptr %u(should<=%u),wr_ptr %u",
			__func__, isp_dbg_get_stream_str(stream),
			stream, rd_ptr, len, wr_ptr);
		return -EINVAL;
	}

	if (wr_ptr > len) {
		dev_err(dev, "%s: fail %s(%u),wr_ptr %u(should<=%u), rd_ptr %u",
			__func__, isp_dbg_get_stream_str(stream),
			stream, wr_ptr, len, rd_ptr);
		return -EINVAL;
	}

	if (wr_ptr < rd_ptr) {
		mem_addr += wr_ptr;

		memcpy((u8 *)(mem_sys + wr_ptr),
		       (u8 *)cmd, sizeof(struct cmd_t));
	} else {
		if ((len - wr_ptr) >= (sizeof(struct cmd_t))) {
			mem_addr += wr_ptr;

			memcpy((u8 *)(mem_sys + wr_ptr),
			       (u8 *)cmd, sizeof(struct cmd_t));
		} else {
			u32 size;
			u8 *src;

			src = (u8 *)cmd;
			size = len - wr_ptr;

			memcpy((u8 *)(mem_sys + wr_ptr), src, size);

			src += size;
			size = sizeof(struct cmd_t) - size;
			memcpy((u8 *)(mem_sys), src, size);
		}
	}

	wr_ptr += sizeof(struct cmd_t);
	if (wr_ptr >= len)
		wr_ptr -= len;

	isp_hwa_wreg(isp, wreg, wr_ptr);

	return 0;
}

struct isp_cmd_element *isp_append_cmd_2_cmdq(struct isp_context *isp,
					      struct isp_cmd_element *command)
{
	struct device *dev = &isp->amd_cam->pdev->dev;
	struct isp_cmd_element *tail_element = NULL;
	struct isp_cmd_element *copy_command = NULL;

	if (!command) {
		dev_err(dev, "%s: NULL cmd pointer", __func__);
		return NULL;
	}

	copy_command = kmalloc(sizeof(*copy_command), GFP_KERNEL);
	if (!copy_command) {
		dev_err(dev, "%s: memory allocate fail", __func__);
		return NULL;
	}

	memcpy(copy_command, command, sizeof(struct isp_cmd_element));
	copy_command->next = NULL;
	mutex_lock(&isp->cmd_q_mtx);
	if (!isp->cmd_q) {
		isp->cmd_q = copy_command;
		goto quit;
	}

	tail_element = isp->cmd_q;

	/* find the tail element */
	while (tail_element->next)
		tail_element = tail_element->next;

	/* insert current element after the tail element */
	tail_element->next = copy_command;
quit:
	mutex_unlock(&isp->cmd_q_mtx);
	return copy_command;
}

struct isp_cmd_element *isp_rm_cmd_from_cmdq(struct isp_context *isp,
					     u32 seq_num,
					     u32 cmd_id, int signal_evt)
{
	struct device *dev = &isp->amd_cam->pdev->dev;
	struct isp_cmd_element *curr_element;
	struct isp_cmd_element *prev_element;

	mutex_lock(&isp->cmd_q_mtx);

	curr_element = isp->cmd_q;
	if (!curr_element) {
		dev_err(dev, "%s: fail empty q", __func__);
		goto quit;
	}

	/* process the first element */
	if (curr_element->seq_num == seq_num &&
	    curr_element->cmd_id == cmd_id) {
		isp->cmd_q = curr_element->next;
		curr_element->next = NULL;
		goto quit;
	}

	prev_element = curr_element;
	curr_element = curr_element->next;

	while (curr_element) {
		if (curr_element->seq_num == seq_num &&
		    curr_element->cmd_id == cmd_id) {
			prev_element->next = curr_element->next;
			curr_element->next = NULL;
			goto quit;
		}

		prev_element = curr_element;
		curr_element = curr_element->next;
	}

	dev_err(dev, "%s: cmd(0x%x,seq:%u) not found",
		__func__, cmd_id, seq_num);
quit:
	if (curr_element && curr_element->evt && signal_evt) {
		dev_dbg(dev, "%s: signal event %d",
			__func__, curr_element->evt->event);
		isp_event_signal(0, curr_element->evt);
	}
	mutex_unlock(&isp->cmd_q_mtx);
	return curr_element;
}

void isp_get_cmd_buf_regs(struct isp_context *isp,
			  enum fw_cmd_resp_stream_id idx,
			  u32 *rreg, u32 *wreg,
			  u32 *baselo_reg, u32 *basehi_reg,
			  u32 *size_reg)
{
	struct device *dev = &isp->amd_cam->pdev->dev;

	switch (idx) {
	case FW_CMD_RESP_STREAM_ID_1:
		if (rreg)
			*rreg = ISP_RB_RPTR1;
		if (wreg)
			*wreg = ISP_RB_WPTR1;
		if (baselo_reg)
			*baselo_reg = ISP_RB_BASE_LO1;
		if (basehi_reg)
			*basehi_reg = ISP_RB_BASE_HI1;
		if (size_reg)
			*size_reg = ISP_RB_SIZE1;
		break;
	case FW_CMD_RESP_STREAM_ID_2:
		if (rreg)
			*rreg = ISP_RB_RPTR2;
		if (wreg)
			*wreg = ISP_RB_WPTR2;
		if (baselo_reg)
			*baselo_reg = ISP_RB_BASE_LO2;
		if (basehi_reg)
			*basehi_reg = ISP_RB_BASE_HI2;
		if (size_reg)
			*size_reg = ISP_RB_SIZE2;
		break;
	case FW_CMD_RESP_STREAM_ID_3:
		if (rreg)
			*rreg = ISP_RB_RPTR3;
		if (wreg)
			*wreg = ISP_RB_WPTR3;
		if (baselo_reg)
			*baselo_reg = ISP_RB_BASE_LO3;
		if (basehi_reg)
			*basehi_reg = ISP_RB_BASE_HI3;
		if (size_reg)
			*size_reg = ISP_RB_SIZE3;
		break;
	case FW_CMD_RESP_STREAM_ID_GLOBAL:
		if (rreg)
			*rreg = ISP_RB_RPTR4;
		if (wreg)
			*wreg = ISP_RB_WPTR4;
		if (baselo_reg)
			*baselo_reg = ISP_RB_BASE_LO4;
		if (basehi_reg)
			*basehi_reg = ISP_RB_BASE_HI4;
		if (size_reg)
			*size_reg = ISP_RB_SIZE4;
		break;
	default:
		dev_err(dev, "fail id[%d]", idx);
		break;
	}
}

void isp_get_resp_buf_regs(struct isp_context *isp,
			   enum fw_cmd_resp_stream_id idx,
			   u32 *rreg, u32 *wreg,
			   u32 *baselo_reg, u32 *basehi_reg,
			   u32 *size_reg)
{
	struct device *dev = &isp->amd_cam->pdev->dev;

	switch (idx) {
	case FW_CMD_RESP_STREAM_ID_1:
		if (rreg)
			*rreg = ISP_RB_RPTR9;
		if (wreg)
			*wreg = ISP_RB_WPTR9;
		if (baselo_reg)
			*baselo_reg = ISP_RB_BASE_LO9;
		if (basehi_reg)
			*basehi_reg = ISP_RB_BASE_HI9;
		if (size_reg)
			*size_reg = ISP_RB_SIZE9;
		break;
	case FW_CMD_RESP_STREAM_ID_2:
		if (rreg)
			*rreg = ISP_RB_RPTR10;
		if (wreg)
			*wreg = ISP_RB_WPTR10;
		if (baselo_reg)
			*baselo_reg = ISP_RB_BASE_LO10;
		if (basehi_reg)
			*basehi_reg = ISP_RB_BASE_HI10;
		if (size_reg)
			*size_reg = ISP_RB_SIZE10;
		break;
	case FW_CMD_RESP_STREAM_ID_3:
		if (rreg)
			*rreg = ISP_RB_RPTR11;
		if (wreg)
			*wreg = ISP_RB_WPTR11;
		if (baselo_reg)
			*baselo_reg = ISP_RB_BASE_LO11;
		if (basehi_reg)
			*basehi_reg = ISP_RB_BASE_HI11;
		if (size_reg)
			*size_reg = ISP_RB_SIZE11;
		break;
	case FW_CMD_RESP_STREAM_ID_GLOBAL:
		if (rreg)
			*rreg = ISP_RB_RPTR12;
		if (wreg)
			*wreg = ISP_RB_WPTR12;
		if (baselo_reg)
			*baselo_reg = ISP_RB_BASE_LO12;
		if (basehi_reg)
			*basehi_reg = ISP_RB_BASE_HI12;
		if (size_reg)
			*size_reg = ISP_RB_SIZE12;
		break;
	default:
		if (rreg)
			*rreg = 0;
		if (wreg)
			*wreg = 0;
		if (baselo_reg)
			*baselo_reg = 0;
		if (basehi_reg)
			*basehi_reg = 0;
		if (size_reg)
			*size_reg = 0;
		dev_err(dev, "fail idx (%u)", idx);
		break;
	}
}

void isp_init_fw_ring_buf(struct isp_context *isp,
			  enum fw_cmd_resp_stream_id idx, u32 cmd)
{
	struct device *dev = &isp->amd_cam->pdev->dev;
	u32 rreg;
	u32 wreg;
	u32 baselo_reg;
	u32 basehi_reg;
	u32 size_reg;
	u64 mc;
	u32 lo;
	u32 hi;
	u32 len;

	if (cmd) {
		/* command buffer */
		if (!isp || idx > FW_CMD_RESP_STREAM_ID_3) {
			dev_err(dev, "(%u:cmd) fail,bad para", idx);
			return;
		}

		isp_get_cmd_buf_regs(isp, idx, &rreg, &wreg,
				     &baselo_reg, &basehi_reg, &size_reg);
		isp_fw_buf_get_cmd_base(isp, idx, NULL, &mc, &len);
	} else {
		/* response buffer */
		if (!isp || idx > FW_CMD_RESP_STREAM_ID_3) {
			dev_err(dev, "(%u:resp) fail,bad para", idx);
			return;
		}

		isp_get_resp_buf_regs(isp, idx, &rreg, &wreg,
				      &baselo_reg, &basehi_reg, &size_reg);
		isp_fw_buf_get_resp_base(isp, idx, NULL, &mc, &len);
	}

	dev_dbg(dev, "init %s ringbuf %u, mc 0x%llx(%u)",
		cmd ? "cmd" : "resp", idx, mc, len);

	isp_split_addr64(mc, &lo, &hi);

	isp_hwa_wreg(isp, rreg, 0);
	isp_hwa_wreg(isp, wreg, 0);
	isp_hwa_wreg(isp, baselo_reg, lo);
	isp_hwa_wreg(isp, basehi_reg, hi);
	isp_hwa_wreg(isp, size_reg, len);

	dev_dbg(dev, "rreg(0x%x)=0x%x", rreg, isp_hwa_rreg(isp, rreg));
	dev_dbg(dev, "wreg(0x%x)=0x%x", wreg, isp_hwa_rreg(isp, wreg));
	dev_dbg(dev, "baselo_reg(0x%x)=0x%x",
		baselo_reg, isp_hwa_rreg(isp, baselo_reg));
	dev_dbg(dev, "basehi_reg(0x%x)=0x%x",
		basehi_reg, isp_hwa_rreg(isp, basehi_reg));
	dev_dbg(dev, "size_reg(0x%x)=0x%x", size_reg, isp_hwa_rreg(isp, size_reg));
}

static enum fw_cmd_resp_stream_id isp_get_stream_id_from_cid(struct isp_context *isp,
							     enum camera_port_id cid)
{
	struct device *dev = &isp->amd_cam->pdev->dev;

	if (isp->sensor_info[cid].stream_id != FW_CMD_RESP_STREAM_ID_MAX)
		return isp->sensor_info[cid].stream_id;

	switch (cid) {
	case CAMERA_PORT_0:
		isp->sensor_info[cid].stream_id = FW_CMD_RESP_STREAM_ID_1;
		break;
	case CAMERA_PORT_1:
		isp->sensor_info[cid].stream_id = FW_CMD_RESP_STREAM_ID_2;
		break;
	case CAMERA_PORT_2:
		isp->sensor_info[cid].stream_id = FW_CMD_RESP_STREAM_ID_3;
		break;
	default:
		dev_err(dev, "Invalid cid[%d].", cid);
		return FW_CMD_RESP_STREAM_ID_MAX;
	};
	return isp->sensor_info[cid].stream_id;
}

int fw_if_send_img_buf(struct isp_context *isp,
		       struct isp_mapped_buf_info *buffer,
		       enum camera_port_id cam_id, enum stream_id stream_id)
{
	struct device *dev;
	struct cmd_send_buffer cmd;
	enum fw_cmd_resp_stream_id stream;
	int result;

	if (!is_para_legal(isp, cam_id))
		return -EINVAL;

	dev = &isp->amd_cam->pdev->dev;

	if (!buffer || stream_id > STREAM_ID_ZSL) {
		dev_err(dev, "%s invalid parameter", __func__);
		return -EINVAL;
	};

	memset(&cmd, 0, sizeof(cmd));
	switch (stream_id) {
	case STREAM_ID_PREVIEW:
		cmd.buffer_type = BUFFER_TYPE_PREVIEW;
		break;
	case STREAM_ID_VIDEO:
		cmd.buffer_type = BUFFER_TYPE_VIDEO;
		break;
	case STREAM_ID_ZSL:
		cmd.buffer_type = BUFFER_TYPE_STILL;
		break;
	default:
		dev_err(dev, "fail bad sid %d", stream_id);
		return -EINVAL;
	};
	stream = isp_get_stream_id_from_cid(isp, cam_id);
	cmd.buffer.vmid_space.bit.vmid = 0;
	cmd.buffer.vmid_space.bit.space = ADDR_SPACE_TYPE_GPU_VA;
	isp_split_addr64(buffer->y_map_info.mc_addr,
			 &cmd.buffer.buf_base_a_lo, &cmd.buffer.buf_base_a_hi);
	cmd.buffer.buf_size_a = buffer->y_map_info.len;

	isp_split_addr64(buffer->u_map_info.mc_addr,
			 &cmd.buffer.buf_base_b_lo, &cmd.buffer.buf_base_b_hi);
	cmd.buffer.buf_size_b = buffer->u_map_info.len;

	isp_split_addr64(buffer->v_map_info.mc_addr,
			 &cmd.buffer.buf_base_c_lo, &cmd.buffer.buf_base_c_hi);
	cmd.buffer.buf_size_c = buffer->v_map_info.len;

	result = isp_send_fw_cmd(isp, CMD_ID_SEND_BUFFER,
				 stream,
				 FW_CMD_PARA_TYPE_DIRECT, &cmd, sizeof(cmd));

	if (result)
		return result;

	dev_dbg(dev, "suc, cid %u,sid %u, addr:%llx, %llx, %llx",
		cam_id, stream_id,
		buffer->y_map_info.mc_addr,
		buffer->u_map_info.mc_addr,
		buffer->v_map_info.mc_addr);

	return 0;
}

void isp_fw_resp_cmd_done_extra(struct isp_context *isp,
				enum camera_port_id cid, struct resp_cmd_done *para,
				struct isp_cmd_element *ele)
{
	struct device *dev;
	u32 major;
	u32 minor;
	u32 rev;
	u32 ver;
	u8 *payload;

	if (!isp) {
		pr_err("invalid isp context");
		return;
	}

	dev = &isp->amd_cam->pdev->dev;

	if (!para || !ele) {
		dev_err(dev, "-><- %s null pointer", __func__);
		return;
	}

	payload = para->payload;
	if (!payload) {
		dev_err(dev, "-><- struct resp_cmd_done payload null pointer");
		return;
	}

	switch (para->cmd_id) {
	case CMD_ID_GET_FW_VERSION:
		ver = *((u32 *)payload);
		major = (ver & FW_VERSION_MAJOR_MASK) >> FW_VERSION_MAJOR_SHIFT;
		minor = (ver & FW_VERSION_MINOR_MASK) >> FW_VERSION_MINOR_SHIFT;
		rev = (ver & FW_VERSION_BUILD_MASK) >> FW_VERSION_BUILD_SHIFT;
		isp->isp_fw_ver = ver;
		dev_dbg(dev, "fw version,maj:min:rev:sub %u:%u:%u", major, minor, rev);
		if (major != FW_VERSION_MAJOR) {
			dev_err(dev, "fw major mismatch, expect %u", FW_VERSION_MAJOR);
		} else if (minor != FW_VERSION_MINOR ||
			   rev != FW_VERSION_BUILD) {
			dev_warn(dev, "fw minor mismatch, expect %u:%u",
				 FW_VERSION_MINOR, FW_VERSION_BUILD);
		}
		break;
	case CMD_ID_START_STREAM:
		break;
	case CMD_ID_SET_3A_ROI:
		dev_dbg(dev, "%s cmd_done (0x%x) for cid:%d", __func__,
			para->cmd_id, cid);
		struct cmd_done_cb_para cmd_cd_param;

		memset(&cmd_cd_param, 0, sizeof(struct cmd_done_cb_para));
		cmd_cd_param.cam_id = cid;
		cmd_cd_param.cmd_id = para->cmd_id;
		cmd_cd_param.cmd_status = para->cmd_status;
		cmd_cd_param.cmd_seqnum = para->cmd_seq_num;
		cmd_cd_param.cmd_payload = *((u32 *)payload);
		isp->evt_cb[cid](isp->evt_cb_context[cid], CB_EVT_ID_CMD_DONE,
				 &cmd_cd_param);
		break;
	default:
		break;
	}
}

void isp_fw_resp_cmd_skip_extra(struct isp_context *isp,
				enum camera_port_id cid, struct resp_cmd_done *para,
				struct isp_cmd_element *ele)
{
	u8 *payload;
	struct device *dev;

	if (!isp) {
		pr_err("invalid isp context");
		return;
	}

	dev = &isp->amd_cam->pdev->dev;

	if (!para || !ele) {
		dev_err(dev, "%s, null pointer", __func__);
		return;
	}

	payload = para->payload;
	if (!payload) {
		dev_err(dev, "%s, struct resp_cmd_done payload null pointer",
			__func__);
		return;
	}
}

void isp_fw_resp_cmd_done(struct isp_context *isp,
			  enum fw_cmd_resp_stream_id fw_stream_id,
			  struct resp_cmd_done *para)
{
	struct isp_cmd_element *ele;
	enum camera_port_id cid;
	struct device *dev = &isp->amd_cam->pdev->dev;

	cid = isp_get_cid_from_stream_id(isp, fw_stream_id);
	ele = isp_rm_cmd_from_cmdq(isp, para->cmd_seq_num, para->cmd_id, false);

	if (!ele) {
		dev_err(dev, "-><- stream %d,cmd %s(0x%08x)(%d),seq %u,no orig", fw_stream_id,
			isp_dbg_get_cmd_str(para->cmd_id),
			para->cmd_id, para->cmd_status, para->cmd_seq_num);
	} else {
		if (ele->resp_payload && ele->resp_payload_len) {
			*ele->resp_payload_len = min(*ele->resp_payload_len, 36);
			memcpy(ele->resp_payload, para->payload,
			       *ele->resp_payload_len);
		}

		dev_dbg(dev, "-><- cid %u, stream %d,cmd %s(0x%08x)(%d),seq %u",
			cid, fw_stream_id,
			isp_dbg_get_cmd_str(para->cmd_id),
			para->cmd_id, para->cmd_status, para->cmd_seq_num);

		if (para->cmd_status == 0) {
			isp_fw_resp_cmd_done_extra(isp, cid, para, ele);
		} else if (para->cmd_status == 2) { /* process the skipped cmd */
			isp_fw_resp_cmd_skip_extra(isp, cid, para, ele);
		}
		if (ele->evt) {
			dev_dbg(dev, "signal event %d", ele->evt->event);
			isp_event_signal(para->cmd_status, ele->evt);
		}
		if (cid >= CAMERA_PORT_MAX) {
			if (fw_stream_id != FW_CMD_RESP_STREAM_ID_GLOBAL)
				dev_err(dev, "fail cid %d, sid %u", cid,
					fw_stream_id);
		}

		if (ele->mc_addr)
			isp_fw_ret_indirect_cmd_pl(isp, &isp->fw_indirect_cmd_pl_buf_mgr,
						   ele->mc_addr);

		kfree(ele);
	}
}

void isp_fw_resp_frame_done(struct isp_context *isp,
			    enum fw_cmd_resp_stream_id fw_stream_id,
			    struct resp_param_package_t *para)
{
	struct meta_info_t *meta;
	u64 mc = 0;
	u64 meta_data_mc = 0;
	enum camera_port_id cid;
	struct isp_mapped_buf_info *prev = NULL;
	struct isp_mapped_buf_info *video = NULL;
	struct isp_mapped_buf_info *zsl = NULL;
	struct frame_done_cb_para *pcb = NULL;
	struct isp_sensor_info *sif;
	struct device *dev = &isp->amd_cam->pdev->dev;

	cid = isp_get_cid_from_stream_id(isp, fw_stream_id);
	if (cid >= CAMERA_PORT_MAX || cid < CAMERA_PORT_0) {
		dev_err(dev, "<- %s,fail,bad cid,streamid %d", __func__,
			fw_stream_id);
		return;
	}

	sif = &isp->sensor_info[cid];
	mc = isp_join_addr64(para->package_addr_lo, para->package_addr_hi);
	meta = (struct meta_info_t *)isp_metainfo_get_sys_from_mc(isp, fw_stream_id, mc);
	if (mc == 0 || !meta) {
		dev_err(dev, "<- %s,fail,bad mc,streamid %d", __func__, fw_stream_id);
		return;
	}
	sif->poc = meta->poc;
	pcb = kzalloc(sizeof(*pcb), GFP_KERNEL);
	if (IS_ERR_OR_NULL(pcb)) {
		dev_err(dev, "<- %s,cid %u,streamid %d, alloc pcb fail",
			__func__, cid, fw_stream_id);
		return;
	}

	pcb->poc = meta->poc;
	pcb->cam_id = cid;

	memcpy(&pcb->meta_info, meta, sizeof(struct meta_info_t));

	dev_dbg(dev, "%s,ts:%llu,cameraId:%d,streamId:%d,poc:%u,preview_en:%u,%s(%i)",
		__func__, ktime_get_ns(), cid, fw_stream_id, meta->poc,
		meta->preview.enabled,
		isp_dbg_get_buf_done_str(meta->preview.status),
		meta->preview.status);

	/* WA here to avoid miss valid RAW buffer, */
	/* currently FW didn't set "source". */
	meta->raw_mipi.source = BUFFER_SOURCE_STREAM;
	meta->byrp_tap_out.source = BUFFER_SOURCE_STREAM;

	prev = isp_preview_done(isp, cid, meta, pcb);
	video = isp_video_done(isp, cid, meta, pcb);
	zsl = isp_zsl_done(isp, cid, meta, pcb);

	if (pcb->preview.status != BUF_DONE_STATUS_ABSENT)
		isp_dbg_show_bufmeta_info("prev", cid, &meta->preview,
					  &pcb->preview.buf);

	if (pcb->video.status != BUF_DONE_STATUS_ABSENT)
		isp_dbg_show_bufmeta_info("video", cid, &meta->video,
					  &pcb->video.buf);

	if (pcb->zsl.status != BUF_DONE_STATUS_ABSENT)
		isp_dbg_show_bufmeta_info("zsl", cid, &meta->still,
					  &pcb->zsl.buf);

	if (meta->metadata.status == BUFFER_STATUS_DONE) {
		meta_data_mc =
			isp_join_addr64(meta->metadata.buffer.buf_base_a_lo,
					meta->metadata.buffer.buf_base_a_hi);
	};

	if (isp->evt_cb[cid]) {
		if (pcb->preview.status != BUF_DONE_STATUS_ABSENT ||
		    pcb->video.status != BUF_DONE_STATUS_ABSENT ||
		    pcb->zsl.status != BUF_DONE_STATUS_ABSENT) {
			isp->evt_cb[cid](isp->evt_cb_context[cid],
					 CB_EVT_ID_FRAME_DONE, pcb);
		}
	} else {
		dev_err(dev, "in %s,fail empty cb for cid %u", __func__, cid);
	}

	if (prev) {
		kfree(prev->sys_img_buf_hdl);
	}
	kfree(prev);

	if (video) {
		kfree(video->sys_img_buf_hdl);
	}
	kfree(video);

	if (zsl) {
		kfree(zsl->sys_img_buf_hdl);
	}
	kfree(zsl);

	if (isp->sensor_info[cid].status == START_STATUS_STARTED)
		resend_meta_in_framedone(isp, cid, fw_stream_id, mc, meta_data_mc);

	kfree(pcb);

	dev_dbg(dev, "stream_id:%d, status:%d", fw_stream_id,
		isp->sensor_info[cid].status);
}

bool isp_semaphore_acquire_one_try(struct isp_context *isp)
{
	u8 i = 0;
	bool ret = true;

	if (!isp) {
		pr_err("%s: fail for null isp", __func__);
		return false;
	}

	mutex_lock(&isp->isp_semaphore_mutex);
	do {
		isp_hwa_wreg(isp, ISP_SEMAPHORE_0, ISP_SEMAPHORE_ID_X86);
		if (isp_hwa_rreg(isp, ISP_SEMAPHORE_0) == ISP_SEMAPHORE_ID_X86)
			break;

		i++;
	} while (i < ISP_SEMAPHORE_ATTEMPTS);

	if (i >= ISP_SEMAPHORE_ATTEMPTS) {
		ret = false;
	} else {
		ret = true;
		isp->isp_semaphore_acq_cnt++;
	}

	mutex_unlock(&isp->isp_semaphore_mutex);
	return ret;
}

bool isp_semaphore_acquire(struct isp_context *isp)
{
	u8 i = 0;
	struct device *dev;

	if (!isp) {
		pr_err("%s: fail for null isp", __func__);
		return false;
	}

	dev = &isp->amd_cam->pdev->dev;

	do {
		if (isp_semaphore_acquire_one_try(isp))
			break;
		i++;
		msleep(ISP_SEMAPHORE_DELAY);
	} while (i < ISP_SEMAPHORE_ATTEMPTS);

	if (i >= ISP_SEMAPHORE_ATTEMPTS) {
		dev_err(dev, "%s: acquire isp_semaphore timeout[%dms]!!!, value 0x%x",
			__func__, ISP_SEMAPHORE_ATTEMPTS * ISP_SEMAPHORE_DELAY,
			isp_hwa_rreg(isp, ISP_SEMAPHORE_0));
		return false;
	} else {
		return true;
	}
}

void isp_semaphore_release(struct isp_context *isp)
{
	struct device *dev;

	if (!isp) {
		pr_err("%s: fail for null isp", __func__);
		return;
	}

	dev = &isp->amd_cam->pdev->dev;

	mutex_lock(&isp->isp_semaphore_mutex);
	isp->isp_semaphore_acq_cnt--;

	if (isp->isp_semaphore_acq_cnt == 0) {
		if (isp_hwa_rreg(isp, ISP_SEMAPHORE_0) == ISP_SEMAPHORE_ID_X86) {
			isp_hwa_wreg(isp, ISP_SEMAPHORE_0, 0);
		} else {
			dev_err(dev, "cnt dec to %u, ISP_SEMAPHORE 0x%x should be 0x%x",
				isp->isp_semaphore_acq_cnt,
				isp_hwa_rreg(isp, ISP_SEMAPHORE_0),
				ISP_SEMAPHORE_ID_X86);
		}
	}

	mutex_unlock(&isp->isp_semaphore_mutex);
}

void isp_fw_resp_func(struct isp_context *isp,
		      enum fw_cmd_resp_stream_id fw_stream_id)
{
	struct resp_t resp;
	struct device *dev = &isp->amd_cam->pdev->dev;

	if (ISP_GET_STATUS(isp) < ISP_STATUS_FW_RUNNING)
		return;

	isp_fw_log_print(isp);

	while (true) {
		s32 ret;
		/* Semaphore check */
		if (!isp_semaphore_acquire(isp)) {
			dev_err(dev, "fail acquire isp semaphore stream_id %u", fw_stream_id);
			break;
		}

		ret = isp_get_f2h_resp(isp, fw_stream_id, &resp);

		isp_semaphore_release(isp);
		if (ret)
			break;

		switch (resp.resp_id) {
		case RESP_ID_CMD_DONE:
			isp_fw_resp_cmd_done(isp, fw_stream_id,
					     (struct resp_cmd_done *)resp.resp_param);
			break;
		case RESP_ID_NOTI_FRAME_DONE:
			isp_fw_resp_frame_done(isp, fw_stream_id,
					       (struct resp_param_package_t *)resp.resp_param);
			break;
		default:
			dev_err(dev, "-><- fail respid %s(0x%x)",
				isp_dbg_get_resp_str(resp.resp_id),
				resp.resp_id);
			break;
		}
	}
}

s32 isp_fw_resp_thread_wrapper(void *context)
{
	struct isp_fw_resp_thread_para *para = context;
	struct isp_context *isp;
	struct device *dev;
	struct thread_handler *thread_ctx;
	enum fw_cmd_resp_stream_id fw_stream_id;
	u64 timeout;

	if (!para) {
		pr_err("%s invalid para", __func__);
		goto quit;
	}

	isp = para->isp;
	dev = &isp->amd_cam->pdev->dev;

	switch (para->idx) {
	case 0:
		fw_stream_id = FW_CMD_RESP_STREAM_ID_GLOBAL;
		break;
	case 1:
		fw_stream_id = FW_CMD_RESP_STREAM_ID_1;
		break;
	case 2:
		fw_stream_id = FW_CMD_RESP_STREAM_ID_2;
		break;
	case 3:
		fw_stream_id = FW_CMD_RESP_STREAM_ID_3;
		break;
	default:
		dev_err(dev, "-><- invalid idx[%d]", para->idx);
		goto quit;
	}

	thread_ctx = &isp->fw_resp_thread[para->idx];

	thread_ctx->wakeup_evt.event = 0;
	mutex_init(&thread_ctx->mutex);
	init_waitqueue_head(&thread_ctx->waitq);
	timeout = msecs_to_jiffies(WORK_ITEM_INTERVAL);

	dev_dbg(dev, "[%u] started", para->idx);

	while (true) {
		wait_event_interruptible_timeout(thread_ctx->waitq,
						 thread_ctx->wakeup_evt.event,
						 timeout);
		thread_ctx->wakeup_evt.event = 0;

		if (kthread_should_stop()) {
			dev_dbg(dev, "[%u] quit", para->idx);
			break;
		}

		mutex_lock(&thread_ctx->mutex);
		isp_fw_resp_func(isp, fw_stream_id);
		mutex_unlock(&thread_ctx->mutex);
	}

	mutex_destroy(&thread_ctx->mutex);

quit:
	return 0;
}

int isp_start_resp_proc_threads(struct isp_context *isp)
{
	struct device *dev = &isp->amd_cam->pdev->dev;
	int i;

	for (i = 0; i < MAX_REAL_FW_RESP_STREAM_NUM; i++) {
		struct thread_handler *thread_ctx = &isp->fw_resp_thread[i];

		isp_resp_para[i].idx = i;
		isp_resp_para[i].isp = isp;

		isp_event_init(&thread_ctx->wakeup_evt, 1, 0);

		thread_ctx->thread = kthread_run(isp_fw_resp_thread_wrapper,
						 &isp_resp_para[i],
						 "amd_isp4_thread");
		if (IS_ERR(thread_ctx->thread)) {
			dev_err(dev, "create thread [%d] fail", i);
			goto fail;
		}
	}
	return 0;
fail:
	isp_stop_resp_proc_threads(isp);
	return -EINVAL;
}

int isp_stop_resp_proc_threads(struct isp_context *isp)
{
	int i;

	for (i = 0; i < MAX_REAL_FW_RESP_STREAM_NUM; i++) {
		struct thread_handler *thread_ctx = &isp->fw_resp_thread[i];

		if (thread_ctx->thread) {
			kthread_stop(thread_ctx->thread);
			thread_ctx->thread = NULL;
		}
	}

	return 0;
}

void wake_up_resp_thread(struct isp_context *isp, u32 index)
{
	if (isp && index < MAX_REAL_FW_RESP_STREAM_NUM) {
		struct thread_handler *thread_ctx = &isp->fw_resp_thread[index];

		thread_ctx->wakeup_evt.event = 1;
		wake_up_interruptible(&thread_ctx->waitq);
	}
}
