/*
 *
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

#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include "isp_debug.h"
#include "isp_common.h"
#include "isp_core.h"
#include "isp_param.h"
#include "isp_hwa.h"
#include "isp_common.h"
#include "isp_mc_addr_mgr.h"
#include "isp_utils.h"
#include "isp_pwr.h"
#include "isp_module_intf.h"
#include "isp_fw_ctrl.h"

#define AGPIO85_ADDRESS     0x02D02654
#define OUTPUT_VALUE_BIT    22
#define STATUS_VALUE_BIT    16
#define OUTPUT_ENABLE_BIT   23

static void isp_pwr_unit_init(struct isp_pwr_unit *unit)
{
	unit->pwr_status = ISP_PWR_UNIT_STATUS_OFF;
	mutex_init(&unit->pwr_status_mutex);
	unit->on_time = 0;
	unit->on_time = MAX_ISP_TIME_TICK;
}

static void enable_agpio85(struct isp_context *isp, bool high)
{
	u32 prev_val;
	u32 val;

	prev_val = isp_hwa_indirect_rreg(isp, AGPIO85_ADDRESS);
	val = prev_val;
	if (high)
		val = val | (1 << OUTPUT_VALUE_BIT) | (1 << STATUS_VALUE_BIT);
	else
		val = val & ~((1 << OUTPUT_VALUE_BIT) | (1 << STATUS_VALUE_BIT));

	val = val | (1 << OUTPUT_ENABLE_BIT);

	if (prev_val != val)
		isp_hwa_indirect_wreg(isp, AGPIO85_ADDRESS, val);
}

static void ispm_context_uninit(struct isp_context *isp)
{
	enum camera_port_id cam_id;

	isp_clear_cmdq(isp);

	kfree(isp->fw_data);
	isp->fw_data = NULL;
	isp->fw_len = 0;

	isp_fw_indirect_cmd_pl_buf_uninit
	(&isp->fw_indirect_cmd_pl_buf_mgr);
	if (isp->indirect_cmd_payload_buf) {
		isp_gpu_mem_free(isp, isp->indirect_cmd_payload_buf);
		isp->indirect_cmd_payload_buf = NULL;
	}

	for (cam_id = CAMERA_PORT_0; cam_id < CAMERA_PORT_MAX; cam_id++) {
		struct isp_sensor_info *info;
		struct isp_stream_info *str_info;
		u32 sid;

		info = &isp->sensor_info[cam_id];

		for (sid = STREAM_ID_PREVIEW; sid <= STREAM_ID_NUM; sid++) {
			str_info = &info->str_info[sid];

			isp_list_destory(&str_info->buf_free, NULL);
			isp_list_destory(&str_info->buf_in_fw, NULL);
		}
	}

	ISP_SET_STATUS(isp, ISP_STATUS_UNINITED);
}

static void ispm_context_init(struct isp_context *isp)
{
	enum camera_port_id cam_id;
	u32 size;

	isp->fw_ctrl_3a = 1;

	isp->timestamp_fw_base = 0;
	isp->timestamp_sw_prev = 0;
	isp->timestamp_sw_base = 0;

	isp->isp_fw_ver = 0;

	isp->refclk = 24;

	isp->sensor_count = CAMERA_PORT_MAX;
	mutex_init(&isp->ops_mutex);
	mutex_init(&isp->map_unmap_mutex);
	mutex_init(&isp->cmd_q_mtx);

	mutex_init(&isp->command_mutex);
	mutex_init(&isp->response_mutex);
	mutex_init(&isp->isp_semaphore_mutex);
	isp->isp_semaphore_acq_cnt = 0;

	for (cam_id = CAMERA_PORT_0; cam_id < CAMERA_PORT_MAX; cam_id++) {
		struct isp_sensor_info *info;
		struct isp_stream_info *str_info;
		u32 sid;

		info = &isp->sensor_info[cam_id];

		info->cid = cam_id;
		info->actual_cid = cam_id;
		info->tnr_enable = false;
		info->start_str_cmd_sent = false;
		info->status = START_STATUS_NOT_START;
		info->stream_id = FW_CMD_RESP_STREAM_ID_MAX;
		info->raw_width = 0;
		info->raw_height = 0;

		for (sid = STREAM_ID_PREVIEW; sid <= STREAM_ID_NUM; sid++) {
			str_info = &info->str_info[sid];

			isp_list_init(&str_info->buf_free);
			isp_list_init(&str_info->buf_in_fw);
		}
	}

	isp_pwr_unit_init(&isp->isp_pu_isp);
	isp_pwr_unit_init(&isp->isp_pu_dphy);

	for (cam_id = CAMERA_PORT_0; cam_id < CAMERA_PORT_MAX; cam_id++)
		isp_pwr_unit_init(&isp->isp_pu_cam[cam_id]);

	for (enum fw_cmd_resp_stream_id id = FW_CMD_RESP_STREAM_ID_1;
	     id < FW_CMD_RESP_STREAM_ID_MAX; id++) {
		isp->fw_cmd_resp_strs_info[id].status =
			FW_CMD_RESP_STR_STATUS_IDLE;
		isp->fw_cmd_resp_strs_info[id].cid_owner = CAMERA_PORT_MAX;
	}

	isp->host2fw_seq_num = 1;
	ISP_SET_STATUS(isp, ISP_STATUS_UNINITED);

	size = INDIRECT_BUF_SIZE * INDIRECT_BUF_CNT;
	if (!isp->indirect_cmd_payload_buf)
		isp->indirect_cmd_payload_buf = isp_gpu_mem_alloc(isp, size);

	if (isp->indirect_cmd_payload_buf &&
	    isp->indirect_cmd_payload_buf->sys_addr)
		isp_fw_indirect_cmd_pl_buf_init(isp, &isp->fw_indirect_cmd_pl_buf_mgr,
						(u64)isp->indirect_cmd_payload_buf->sys_addr,
						isp->indirect_cmd_payload_buf->gpu_mc_addr, size);

	ISP_SET_STATUS(isp, ISP_STATUS_INITED);
}

static int isp_setup_fw_mem_pool(struct isp_context *isp,
				 enum camera_port_id cam_id,
				 enum fw_cmd_resp_stream_id fw_stream_id)
{
	struct cmd_send_buffer buf_type;
	struct device *dev;

	dev = &isp->amd_cam->pdev->dev;

	if (!isp->fw_mem_pool[cam_id]) {
		isp->fw_mem_pool[cam_id] =
			isp_gpu_mem_alloc(isp, INTERNAL_MEMORY_POOL_SIZE);
	}

	if (!isp->fw_mem_pool[cam_id] || !isp->fw_mem_pool[cam_id]->sys_addr) {
		dev_err(dev, "%s fail for allocation mem", __func__);
		return -EINVAL;
	}

	memset(&buf_type, 0, sizeof(buf_type));
	buf_type.buffer_type = BUFFER_TYPE_MEM_POOL;
	buf_type.buffer.buf_tags = 0;
	buf_type.buffer.vmid_space.bit.vmid = 0;
	buf_type.buffer.vmid_space.bit.space = ADDR_SPACE_TYPE_GPU_VA;
	isp_split_addr64(isp->fw_mem_pool[cam_id]->gpu_mc_addr,
			 &buf_type.buffer.buf_base_a_lo,
			 &buf_type.buffer.buf_base_a_hi);
	buf_type.buffer.buf_size_a = (u32)isp->fw_mem_pool[cam_id]->mem_size;

	if (isp_send_fw_cmd(isp, CMD_ID_SEND_BUFFER, fw_stream_id,
			    FW_CMD_PARA_TYPE_DIRECT,
			    &buf_type, sizeof(buf_type))) {
		dev_err(dev, "%s, send BUFFER_TYPE_MEM_POOL 0x%llx(%u) fail",
			__func__,
			isp->fw_mem_pool[cam_id]->gpu_mc_addr,
			buf_type.buffer.buf_size_a);
		return -EINVAL;
	}
	dev_dbg(dev, "%s, send BUFFER_TYPE_MEM_POOL 0x%llx(%u) suc",
		__func__, isp->fw_mem_pool[cam_id]->gpu_mc_addr,
		buf_type.buffer.buf_size_a);
	return 0;
};

static struct isp_mapped_buf_info *isp_map_sys_2_mc(struct isp_context *isp,
						    struct sys_img_buf_info *sys_img_buf,
						    u16 cam_id,
						    u16 stream_id)
{
	struct isp_mapped_buf_info *mapped_buf;

	mapped_buf = kmalloc(sizeof(*mapped_buf), GFP_KERNEL);
	if (IS_ERR_OR_NULL(mapped_buf))
		return NULL;

	memset(mapped_buf, 0, sizeof(struct isp_mapped_buf_info));

	mapped_buf->sys_img_buf_hdl = sys_img_buf;
	mapped_buf->camera_port_id = (u8)cam_id;
	mapped_buf->stream_id = (u8)stream_id;

	mapped_buf->y_map_info.len = sys_img_buf->planes[0].len;
	mapped_buf->y_map_info.mc_addr = sys_img_buf->planes[0].mc_addr;
	mapped_buf->y_map_info.sys_addr = (u64)sys_img_buf->planes[0].sys_addr;

	mapped_buf->u_map_info.len = sys_img_buf->planes[1].len;
	mapped_buf->u_map_info.mc_addr = sys_img_buf->planes[1].mc_addr;
	mapped_buf->u_map_info.sys_addr = (u64)sys_img_buf->planes[1].sys_addr;

	mapped_buf->v_map_info.len = sys_img_buf->planes[2].len;
	mapped_buf->v_map_info.mc_addr = sys_img_buf->planes[2].mc_addr;
	mapped_buf->v_map_info.sys_addr = (u64)sys_img_buf->planes[2].sys_addr;

	return mapped_buf;
}

static void isp_take_back_str_buf(struct isp_context *isp,
				  struct isp_stream_info *str,
				  enum camera_port_id cid, enum stream_id sid)
{
	struct device *dev;
	struct isp_mapped_buf_info *img_info = NULL;
	struct frame_done_cb_para *pcb = NULL;

	dev = &isp->amd_cam->pdev->dev;
	pcb = kmalloc(sizeof(*pcb), GFP_KERNEL);
	if (IS_ERR_OR_NULL(pcb))
		return;

	do {
		if (img_info) {
			isp_unmap_sys_2_mc(isp, img_info);
			kfree(img_info->sys_img_buf_hdl);
			img_info->sys_img_buf_hdl = NULL;
			kfree(img_info);
		}

		img_info = (struct isp_mapped_buf_info *)
			   isp_list_get_first(&str->buf_in_fw);
	} while (img_info);

	img_info = NULL;
	do {
		pcb->cam_id = cid;
		if (img_info) {
			isp_unmap_sys_2_mc(isp, img_info);
			kfree(img_info->sys_img_buf_hdl);
			img_info->sys_img_buf_hdl = NULL;
			kfree(img_info);
		}

		img_info = (struct isp_mapped_buf_info *)
			   isp_list_get_first(&str->buf_free);
	} while (img_info);

	kfree(pcb);
}

static enum sensor_id isp_get_fw_sensor_id(struct isp_context *isp,
					   enum camera_port_id cid)
{
	enum camera_port_id actual_id = cid;

	if (isp->sensor_info[cid].cam_type == CAMERA_TYPE_MEM)
		return SENSOR_ID_RDMA;
	if (actual_id == CAMERA_PORT_0)
		return SENSOR_ID_ON_MIPI0;
	if (actual_id == CAMERA_PORT_1)
		return SENSOR_ID_ON_MIPI2;
	if (actual_id == CAMERA_PORT_2)
		return SENSOR_ID_ON_MIPI2;

	return SENSOR_ID_INVALID;
}

static int isp_set_stream_path(struct isp_context *isp, enum camera_port_id cid,
			       enum fw_cmd_resp_stream_id fw_stream_id)
{
	struct device *dev;
	enum camera_port_id actual_id = cid;
	struct cmd_set_stream_cfg stream_path_cmd = {0};
	int ret;

	dev = &isp->amd_cam->pdev->dev;

	memset(&stream_path_cmd, 0, sizeof(stream_path_cmd));
	stream_path_cmd.stream_cfg.mipi_pipe_path_cfg.sensor_id =
		isp_get_fw_sensor_id(isp, actual_id);
	stream_path_cmd.stream_cfg.mipi_pipe_path_cfg.b_enable = true;
	stream_path_cmd.stream_cfg.isp_pipe_path_cfg.isp_pipe_id =
		isp_get_pipeline_id(isp, actual_id);

	stream_path_cmd.stream_cfg.b_enable_tnr = true;
	dev_dbg(dev, "cid %u,stream %d, sensor_id %d, pipeId 0x%x EnableTnr %u",
		cid, fw_stream_id,
		stream_path_cmd.stream_cfg.mipi_pipe_path_cfg.sensor_id,
		stream_path_cmd.stream_cfg.isp_pipe_path_cfg.isp_pipe_id,
		stream_path_cmd.stream_cfg.b_enable_tnr);

	ret = isp_send_fw_cmd(isp, CMD_ID_SET_STREAM_CONFIG, fw_stream_id,
			      FW_CMD_PARA_TYPE_DIRECT,
			      &stream_path_cmd, sizeof(stream_path_cmd));
	if (ret) {
		dev_err(dev, "fail for CMD_ID_SET_STREAM_CONFIG");
		return -EINVAL;
	}

	return 0;
}

static int isp_setup_stream(struct isp_context *isp, enum camera_port_id cid,
			    enum fw_cmd_resp_stream_id fw_stream_id)
{
	struct device *dev;

	dev = &isp->amd_cam->pdev->dev;

	if (isp_set_stream_path(isp, cid, fw_stream_id)) {
		dev_err(dev, "%s fail for set_stream_path", __func__);
		return -EINVAL;
	}

	return 0;
}

static int isp_send_meta_buf(struct isp_context *isp, enum camera_port_id cid,
			     enum fw_cmd_resp_stream_id fw_stream_id)
{
	struct device *dev;
	struct fw_cmd_resp_str_info *stream_info;
	u32 i;
	struct cmd_send_buffer buf_type = { 0 };
	u32 cnt = 0;

	dev = &isp->amd_cam->pdev->dev;

	if (fw_stream_id >= FW_CMD_RESP_STREAM_ID_MAX) {
		dev_err(dev, "%s fail, bad para,cid:%d, fw_stream_id %u",
			__func__, cid, fw_stream_id);
		return -EINVAL;
	}

	stream_info = &isp->fw_cmd_resp_strs_info[fw_stream_id];
	for (i = 0; i < STREAM_META_BUF_COUNT; i++) {
		if (!stream_info->meta_data_buf[i] ||
		    !stream_info->meta_data_buf[i]->sys_addr) {
			dev_err(dev, "in  %s(%u:%u) fail, no meta data buf(%u)",
				__func__, cid, fw_stream_id, i);
			continue;
		}
		memset(&buf_type, 0, sizeof(buf_type));
		buf_type.buffer_type = BUFFER_TYPE_META_DATA;
		buf_type.buffer.buf_tags = 0;
		buf_type.buffer.vmid_space.bit.vmid = 0;
		buf_type.buffer.vmid_space.bit.space = ADDR_SPACE_TYPE_GPU_VA;
		isp_split_addr64(stream_info->meta_data_buf[i]->gpu_mc_addr,
				 &buf_type.buffer.buf_base_a_lo,
				 &buf_type.buffer.buf_base_a_hi);
		buf_type.buffer.buf_size_a =
			(u32)stream_info->meta_data_buf[i]->mem_size;
		if (isp_send_fw_cmd(isp, CMD_ID_SEND_BUFFER, fw_stream_id,
				    FW_CMD_PARA_TYPE_DIRECT, &buf_type,
				    sizeof(buf_type))) {
			dev_err(dev, "in  %s(%u) send meta(%u) fail", __func__,
				cid, i);
			continue;
		}
		cnt++;
	}

	for (i = 0; i < STREAM_META_BUF_COUNT; i++) {
		if (!stream_info->meta_info_buf[i] ||
		    !stream_info->meta_info_buf[i]->sys_addr) {
			dev_err(dev, "in  %s(%u:%u) fail, no meta info buf(%u)",
				__func__, cid, fw_stream_id, i);
			continue;
		}
		memset(stream_info->meta_info_buf[i]->sys_addr, 0,
		       stream_info->meta_info_buf[i]->mem_size);
		memset(&buf_type, 0, sizeof(buf_type));
		buf_type.buffer_type = BUFFER_TYPE_META_INFO;
		buf_type.buffer.buf_tags = 0;
		buf_type.buffer.vmid_space.bit.vmid = 0;
		buf_type.buffer.vmid_space.bit.space = ADDR_SPACE_TYPE_GPU_VA;
		isp_split_addr64(stream_info->meta_info_buf[i]->gpu_mc_addr,
				 &buf_type.buffer.buf_base_a_lo,
				 &buf_type.buffer.buf_base_a_hi);
		buf_type.buffer.buf_size_a =
			(u32)stream_info->meta_info_buf[i]->mem_size;
		if (isp_send_fw_cmd(isp, CMD_ID_SEND_BUFFER, fw_stream_id,
				    FW_CMD_PARA_TYPE_DIRECT, &buf_type,
				    sizeof(buf_type))) {
			dev_err(dev, "in  %s(%u) send meta(%u) fail", __func__,
				cid, i);
			continue;
		}
		cnt++;
	}
	if (cnt) {
		dev_dbg(dev, "%s, cid %u, %u meta sent suc", __func__, cid, cnt);
		return 0;
	}

	dev_err(dev, "%s, cid %u, fail, no meta sent", __func__, cid);
	return -EINVAL;
}

static int isp_set_stream_data_fmt(struct isp_context *isp,
				   enum camera_port_id cam_id,
				   enum stream_id stream_type,
				   enum pvt_img_fmt img_fmt)
{
	struct isp_stream_info *sif;
	struct device *dev;

	dev = &isp->amd_cam->pdev->dev;

	if (stream_type > STREAM_ID_NUM) {
		dev_err(dev, "%s,fail para,stream type%d", __func__, stream_type);
		return -EINVAL;
	}

	if (img_fmt == PVT_IMG_FMT_INVALID || img_fmt >= PVT_IMG_FMT_MAX) {
		dev_err(dev, "%s,fail fmt,cid%d,sid%d,fmt%d",
			__func__, cam_id, stream_type, img_fmt);
		return -EINVAL;
	}

	sif = &isp->sensor_info[cam_id].str_info[stream_type];

	if (sif->start_status == START_STATUS_NOT_START) {
		sif->format = img_fmt;
		dev_dbg(dev, "%s suc,cid %d,str %d,fmt %s",
			__func__, cam_id, stream_type,
			isp_dbg_get_pvt_fmt_str(img_fmt));
		return 0;
	}

	if (sif->format == img_fmt) {
		dev_dbg(dev, "%s suc,cid%d,str%d,fmt%s,do none",
			__func__, cam_id, stream_type,
			isp_dbg_get_pvt_fmt_str(img_fmt));
		sif->format = img_fmt;
		return 0;
	}
	dev_err(dev, "%s fail,cid%d,str%d,fmt%s,bad stat%d",
		__func__, cam_id, stream_type,
		isp_dbg_get_pvt_fmt_str(img_fmt),
		sif->start_status);
	return -EINVAL;
}

static bool isp_get_str_out_prop(struct isp_sensor_info *sen_info,
				 struct isp_stream_info *str_info,
				 struct image_prop_t *out_prop)
{
	u32 width = 0;
	u32 height = 0;
	bool ret = true;

	width = str_info->width;
	height = str_info->height;

	switch (str_info->format) {
	case PVT_IMG_FMT_NV12:
		out_prop->image_format = IMAGE_FORMAT_NV12;
		out_prop->width = width;
		out_prop->height = height;
		out_prop->luma_pitch = str_info->luma_pitch_set;
		out_prop->chroma_pitch = out_prop->luma_pitch;
		break;
	case PVT_IMG_FMT_P010:
		out_prop->image_format = IMAGE_FORMAT_P010;
		out_prop->width = width;
		out_prop->height = height;
		out_prop->luma_pitch = str_info->luma_pitch_set;
		out_prop->chroma_pitch = out_prop->luma_pitch;
		break;
	case PVT_IMG_FMT_L8:
		out_prop->image_format = IMAGE_FORMAT_NV12;
		out_prop->width = width;
		out_prop->height = height;
		out_prop->luma_pitch = str_info->luma_pitch_set;
		out_prop->chroma_pitch = str_info->luma_pitch_set;
		break;
	case PVT_IMG_FMT_NV21:
		out_prop->image_format = IMAGE_FORMAT_NV21;
		out_prop->width = width;
		out_prop->height = height;
		out_prop->luma_pitch = str_info->luma_pitch_set;
		out_prop->chroma_pitch = str_info->chroma_pitch_set;
		break;
	case PVT_IMG_FMT_YV12:
		out_prop->image_format = IMAGE_FORMAT_YV12;
		out_prop->width = width;
		out_prop->height = height;
		out_prop->luma_pitch = str_info->luma_pitch_set;
		out_prop->chroma_pitch = str_info->chroma_pitch_set;
		break;
	case PVT_IMG_FMT_I420:
		out_prop->image_format = IMAGE_FORMAT_I420;
		out_prop->width = width;
		out_prop->height = height;
		out_prop->luma_pitch = str_info->luma_pitch_set;
		out_prop->chroma_pitch = str_info->chroma_pitch_set;
		break;
	case PVT_IMG_FMT_YUV422P:
		out_prop->image_format = IMAGE_FORMAT_YUV422PLANAR;
		out_prop->width = width;
		out_prop->height = height;
		out_prop->luma_pitch = str_info->luma_pitch_set;
		out_prop->chroma_pitch = str_info->chroma_pitch_set;
		break;
	case PVT_IMG_FMT_YUV422_SEMIPLANAR:
		out_prop->image_format = IMAGE_FORMAT_YUV422SEMIPLANAR;
		out_prop->width = width;
		out_prop->height = height;
		out_prop->luma_pitch = str_info->luma_pitch_set;
		out_prop->chroma_pitch = str_info->chroma_pitch_set;
		break;
	case PVT_IMG_FMT_YUV422_INTERLEAVED:
		out_prop->image_format = IMAGE_FORMAT_YUV422INTERLEAVED;
		out_prop->width = width;
		out_prop->height = height;
		out_prop->luma_pitch = str_info->luma_pitch_set;
		out_prop->chroma_pitch = str_info->chroma_pitch_set;
		break;
	default:
		pr_err("%s fail by picture color format:%d",
		       __func__, str_info->format);
		ret = false;
		break;
	}

	return ret;
}

static int isp_kickoff_stream(struct isp_context *isp,
			      enum camera_port_id cid,
			      enum fw_cmd_resp_stream_id fw_stream_id,
			      u32 w, u32 h)
{
	struct device *dev;
	struct isp_sensor_info *sif;
	struct cmd_config_mmhub_prefetch prefetch = {0};

	dev = &isp->amd_cam->pdev->dev;

	if (fw_stream_id >= FW_CMD_RESP_STREAM_ID_MAX) {
		dev_err(dev, "%s fail for para,cid:%d, fw_stream_id %u", __func__,
			cid, fw_stream_id);
		return -EINVAL;
	};
	sif = &isp->sensor_info[cid];

	if (sif->status == START_STATUS_STARTED) {
		dev_dbg(dev, "%s suc, do none for already started",
			__func__);
		return 0;
	} else if (sif->status == START_STATUS_START_FAIL) {
		dev_err(dev, "%s fail for start fail before", __func__);
		return -EINVAL;
	}

	dev_dbg(dev, "%s cid:%d,w:%u,h:%u", __func__, cid, w, h);

	sif->status = START_STATUS_START_FAIL;

	isp->prev_buf_cnt_sent = 0;

	if (isp_send_meta_buf(isp, cid, fw_stream_id)) {
		dev_err(dev, "%s, fail for isp_send_meta_buf", __func__);
		return -EINVAL;
	};

	sif->status = START_STATUS_NOT_START;

	prefetch.b_rtpipe = 1;
	prefetch.b_soft_rtpipe = 1;
	prefetch.b_add_gap_for_yuv = 1;

	if (isp_send_fw_cmd(isp, CMD_ID_ENABLE_PREFETCH,
			    FW_CMD_RESP_STREAM_ID_GLOBAL,
			    FW_CMD_PARA_TYPE_DIRECT,
			    &prefetch, sizeof(prefetch))) {
		dev_warn(dev, "failed to config prefetch");
	} else {
		dev_dbg(dev, "config prefetch %d:%d suc",
			prefetch.b_rtpipe,
			prefetch.b_soft_rtpipe);
	}

	if (!sif->start_str_cmd_sent && sif->channel_buf_sent_cnt >=
	    MIN_CHANNEL_BUF_CNT_BEFORE_START_STREAM) {
		if (isp_send_fw_cmd(isp, CMD_ID_START_STREAM, fw_stream_id,
				    FW_CMD_PARA_TYPE_DIRECT, NULL, 0)) {
			dev_err(dev, "<-%s fail for START_STREAM", __func__);
			return -EINVAL;
		}
		sif->start_str_cmd_sent = 1;
	} else {
		dev_dbg(dev, "%s no send START_STREAM, start_sent %u, buf_sent %u",
			__func__, sif->start_str_cmd_sent,
			sif->channel_buf_sent_cnt);
	}

	return 0;
}

static int isp_setup_output(struct isp_context *isp, enum camera_port_id cid,
			    enum stream_id stream_id)
{
	struct device *dev;
	enum fw_cmd_resp_stream_id fw_stream_id;
	struct cmd_set_out_ch_prop cmd_ch_prop = {0};
	struct cmd_enable_out_ch cmd_ch_en = {0};
	struct isp_stream_info *sif;
	struct isp_sensor_info *sen_info = NULL;
	struct image_prop_t *out_prop;

	dev = &isp->amd_cam->pdev->dev;
	fw_stream_id = isp_get_fwresp_stream_id(isp, cid, stream_id);
	sen_info = &isp->sensor_info[cid];
	sif = &isp->sensor_info[cid].str_info[stream_id];
	dev_dbg(dev, "%s cid:%d,str:%d", __func__, cid, stream_id);

	if (sif->start_status == START_STATUS_STARTED) {
		dev_dbg(dev, "%s,suc do none", __func__);
		return 0;
	}

	if (sif->start_status == START_STATUS_START_FAIL) {
		dev_dbg(dev, "%s,fail do none", __func__);
		return 0;
	}

	sif->start_status = START_STATUS_STARTING;
	out_prop = &cmd_ch_prop.image_prop;
	if (stream_id == STREAM_ID_PREVIEW) {
		cmd_ch_prop.ch = ISP_PIPE_OUT_CH_PREVIEW;
		cmd_ch_en.ch = ISP_PIPE_OUT_CH_PREVIEW;
	} else if (stream_id == STREAM_ID_VIDEO) {
		cmd_ch_prop.ch = ISP_PIPE_OUT_CH_VIDEO;
		cmd_ch_en.ch = ISP_PIPE_OUT_CH_VIDEO;
	} else if (stream_id == STREAM_ID_ZSL) {
		cmd_ch_prop.ch = ISP_PIPE_OUT_CH_STILL;
		cmd_ch_en.ch = ISP_PIPE_OUT_CH_STILL;
	} else {
		/* now stream_id must be STREAM_ID_RAW */
		enum isp_pipe_out_ch_t ch = ISP_PIPE_OUT_CH_MIPI_RAW;

		cmd_ch_prop.ch = ch;
		cmd_ch_en.ch = ch;
	}
	cmd_ch_en.is_enable = true;
	if (!isp_get_str_out_prop(sen_info, sif, out_prop)) {
		dev_err(dev, "%s fail,get out prop", __func__);
		return -EINVAL;
	}

	dev_dbg(dev, "%s,cid %d, stream %d", __func__,
		cid, fw_stream_id);

	dev_dbg(dev, "in %s,channel:%s,fmt %s,w:h=%u:%u,lp:%u,cp%u",
		__func__,
		isp_dbg_get_out_ch_str(cmd_ch_prop.ch),
		isp_dbg_get_out_fmt_str(cmd_ch_prop.image_prop.image_format),
		cmd_ch_prop.image_prop.width, cmd_ch_prop.image_prop.height,
		cmd_ch_prop.image_prop.luma_pitch,
		cmd_ch_prop.image_prop.chroma_pitch);

	if (isp_send_fw_cmd(isp, CMD_ID_SET_OUT_CHAN_PROP,
			    fw_stream_id,
			    FW_CMD_PARA_TYPE_DIRECT,
			    &cmd_ch_prop,
			    sizeof(cmd_ch_prop))) {
		sif->start_status = START_STATUS_START_FAIL;
		dev_err(dev, "%s fail,set out prop", __func__);
		return -EINVAL;
	};

	if (isp_send_fw_cmd(isp, CMD_ID_ENABLE_OUT_CHAN,
			    fw_stream_id,
			    FW_CMD_PARA_TYPE_DIRECT,
			    &cmd_ch_en, sizeof(cmd_ch_en))) {
		sif->start_status = START_STATUS_START_FAIL;
		dev_err(dev, "%s,enable fail", __func__);
		return -EINVAL;
	}

	dev_dbg(dev, "%s,enable channel %s",
		__func__, isp_dbg_get_out_ch_str(cmd_ch_en.ch));

	if (!sen_info->start_str_cmd_sent) {
		if (isp_kickoff_stream(isp, cid, fw_stream_id,
				       out_prop->width,
				       out_prop->height)) {
			dev_err(dev, "%s, kickoff stream fail",
				__func__);
			return -EINVAL;
		}

		if (sen_info->start_str_cmd_sent) {
			sen_info->status = START_STATUS_STARTED;
			sif->start_status = START_STATUS_STARTED;
			dev_dbg(dev, "kickoff stream suc:STARTED");
		} else {
			dev_dbg(dev, "kickoff stream suc");
		}
	} else {
		dev_dbg(dev, "%s,stream running, no need kickoff", __func__);
		sif->start_status = START_STATUS_STARTED;
	}

	dev_dbg(dev, "%s,suc", __func__);
	return 0;
}

static int isp_set_str_res_fps_pitch(struct isp_context *isp,
				     enum camera_port_id cam_id,
				     enum stream_id stream_type,
				     struct pvt_img_res_fps_pitch *value)
{
	struct device *dev;
	u32 width;
	u32 height;
	u32 fps;
	u32 luma_pitch;
	u32 chroma_pitch;
	struct isp_stream_info *sif;
	int ret = 0;

	dev = &isp->amd_cam->pdev->dev;

	width = value->width;
	height = value->height;
	fps = value->fps;
	luma_pitch = abs(value->luma_pitch);
	chroma_pitch = abs(value->chroma_pitch);

	if (width == 0 || height == 0 || luma_pitch == 0) {
		dev_err(dev, "%s,fail para,cid%d,sid%d,w:h:p %d:%d:%d",
			__func__, cam_id, stream_type, width, height, luma_pitch);
		return -EINVAL;
	}

	sif = &isp->sensor_info[cam_id].str_info[stream_type];
	dev_dbg(dev, "%s,cid%d,sid%d,lp%d,cp%d,w:%d,h:%d,fpsId:%d,strSta %u,chaSta %u",
		__func__, cam_id, stream_type, luma_pitch, chroma_pitch, width, height,
		fps, isp->sensor_info[cam_id].status, sif->start_status);

	if (sif->start_status == START_STATUS_NOT_START) {
		sif->width = width;
		sif->height = height;
		sif->fps = fps;
		sif->luma_pitch_set = luma_pitch;
		sif->chroma_pitch_set = chroma_pitch;
		ret = 0;
		dev_dbg(dev, "%s suc, store", __func__);
	} else if (sif->start_status == START_STATUS_STARTING) {
		sif->width = width;
		sif->height = height;
		sif->fps = fps;
		sif->luma_pitch_set = luma_pitch;
		sif->chroma_pitch_set = chroma_pitch;

		ret = isp_setup_output(isp, cam_id, stream_type);
		if (ret == 0) {
			dev_dbg(dev, "%s suc aft setup out", __func__);
			ret = 0;
			goto quit;
		} else {
			dev_err(dev, "%s fail for setup out", __func__);
			ret = -EINVAL;
			goto quit;
		}
	} else {
		if (sif->width != width ||
		    sif->height != height ||
		    sif->fps != fps ||
		    sif->luma_pitch_set != luma_pitch ||
		    sif->chroma_pitch_set != chroma_pitch) {
			dev_err(dev, "%s fail for non-consis", __func__);
			ret = -EINVAL;
			goto quit;
		} else {
			dev_dbg(dev, "%s suc, do none", __func__);
			ret = 0;
			goto quit;
		}
	}
quit:
	return ret;
}

static int isp_alloc_fw_drv_shared_buf(struct isp_context *isp,
				       enum camera_port_id cam_id,
				       enum fw_cmd_resp_stream_id fw_stream_id)
{
	struct device *dev;
	struct fw_cmd_resp_str_info *stream_info;
	u32 i;
	u32 size;

	dev = &isp->amd_cam->pdev->dev;

	if (fw_stream_id >= FW_CMD_RESP_STREAM_ID_MAX) {
		dev_err(dev, "%s fail bad para,fw_stream_id %u",
			__func__, fw_stream_id);
		return -EINVAL;
	}

	stream_info = &isp->fw_cmd_resp_strs_info[fw_stream_id];

	dev_dbg(dev, "%s, cid %u,fw_cmd_resp_stream_id:%d", __func__,
		cam_id, fw_stream_id);

	for (i = 0; i < STREAM_META_BUF_COUNT; i++) {
		size = META_INFO_BUF_SIZE;
		if (!stream_info->meta_info_buf[i]) {
			stream_info->meta_info_buf[i] = isp_gpu_mem_alloc(isp, size);
			if (stream_info->meta_info_buf[i]) {
				dev_dbg(dev, "alloc %uth meta_info_buf ok", i);
			} else {
				dev_err(dev, "alloc %uth meta_info_buf fail", i);
				return -EINVAL;
			}
		}
	}

	for (i = 0; i < STREAM_META_BUF_COUNT; i++) {
		size = META_DATA_BUF_SIZE;
		if (!stream_info->meta_data_buf[i]) {
			stream_info->meta_data_buf[i] = isp_gpu_mem_alloc(isp, size);
			if (stream_info->meta_data_buf[i]) {
				dev_dbg(dev, "alloc %uth meta_data_buf ok", i);
			} else {
				dev_err(dev, "alloc %uth meta_data_buf fail", i);
				return -EINVAL;
			}
		}
	}

	if (!stream_info->cmd_resp_buf) {
		size = MAX_CMD_RESPONSE_BUF_SIZE;
		stream_info->cmd_resp_buf = isp_gpu_mem_alloc(isp, size);
		if (stream_info->cmd_resp_buf) {
			dev_dbg(dev, "alloc cmd_resp_buf ok");
		} else {
			dev_err(dev, "alloc cmd_resp_buf fail");
			return -EINVAL;
		}
	}

	return 0;
}

static int isp_init_stream(struct isp_context *isp, enum camera_port_id cam_id,
			   enum fw_cmd_resp_stream_id fw_stream_id)
{
	struct device *dev;

	dev = &isp->amd_cam->pdev->dev;
	dev_dbg(dev, "%s, cid:%d, fw streamID: %d", __func__, cam_id, fw_stream_id);

	if (isp->fw_cmd_resp_strs_info[fw_stream_id].status ==
	    FW_CMD_RESP_STR_STATUS_INITIALED) {
		dev_dbg(dev, "(cid:%d fw_stream_id:%d),suc do none", cam_id, fw_stream_id);
		return 0;
	}

	if (isp_setup_fw_mem_pool(isp, cam_id, fw_stream_id)) {
		dev_err(dev, "fail for isp_setup_fw_mem_pool");
		return -EINVAL;
	}

	if (isp_alloc_fw_drv_shared_buf(isp, cam_id, fw_stream_id)) {
		dev_err(dev, "fail for isp_alloc_fw_drv_shared_buf");
		return -EINVAL;
	}

	if (isp_setup_stream(isp, cam_id, fw_stream_id)) {
		dev_err(dev, "fail for isp_setup_stream");
		return -EINVAL;
	}

	dev_dbg(dev, "set fw stream_id %d to be initialed status", fw_stream_id);
	isp->fw_cmd_resp_strs_info[fw_stream_id].status =
		FW_CMD_RESP_STR_STATUS_INITIALED;

	return 0;
}

static void isp_reset_str_info(struct isp_context *isp, enum camera_port_id cid,
			       enum stream_id sid)
{
	struct device *dev;
	struct isp_sensor_info *sif;
	struct isp_stream_info *str_info;

	dev = &isp->amd_cam->pdev->dev;
	sif = &isp->sensor_info[cid];
	str_info = &sif->str_info[sid];
	str_info->format = PVT_IMG_FMT_INVALID;
	str_info->width = 0;
	str_info->height = 0;
	str_info->luma_pitch_set = 0;
	str_info->chroma_pitch_set = 0;
	str_info->max_fps_numerator = MAX_PHOTO_SEQUENCE_FRAME_RATE;
	str_info->max_fps_denominator = 1;
	str_info->start_status = START_STATUS_NOT_START;
	dev_dbg(dev, "%s,reset cam%d str[%d] Not start", __func__, cid, sid);
}

static void isp_reset_camera_info(struct isp_context *isp, enum camera_port_id cid)
{
	struct isp_sensor_info *info;
	enum stream_id stream_id;

	info = &isp->sensor_info[cid];

	info->cid = cid;
	info->actual_cid = cid;

	info->status = START_STATUS_NOT_START;
	memset(&info->ae_roi, 0, sizeof(info->ae_roi));
	memset(&info->af_roi, 0, sizeof(info->af_roi));
	memset(&info->awb_region, 0, sizeof(info->awb_region));
	for (stream_id = STREAM_ID_PREVIEW; stream_id <= STREAM_ID_NUM; stream_id++)
		isp_reset_str_info(isp, cid, stream_id);

	info->cur_res_fps_id = -1;
	info->tnr_enable = false;
	info->start_str_cmd_sent = false;
	info->stream_id = FW_CMD_RESP_STREAM_ID_MAX;
	info->sensor_opened = 0;
}

static int isp_uninit_stream(struct isp_context *isp, enum camera_port_id cam_id,
			     enum fw_cmd_resp_stream_id fw_stream_id)
{
	struct device *dev;
	struct isp_sensor_info *snr_info;
	struct isp_cmd_element *ele = NULL;
	int i;
	u32 out_cnt;

	dev = &isp->amd_cam->pdev->dev;

	if (isp->fw_cmd_resp_strs_info[fw_stream_id].status !=
	    FW_CMD_RESP_STR_STATUS_INITIALED) {
		dev_dbg(dev, "%s (cid:%d, fwstri:%d) do none for not started",
			__func__, cam_id, fw_stream_id);
		return 0;
	}

	dev_dbg(dev, "%s (cid:%d,fw stream_id:%d)", __func__, cam_id, fw_stream_id);

	isp_get_stream_output_bits(isp, cam_id, &out_cnt);

	if (out_cnt > 0) {
		dev_dbg(dev, "%s (cid:%d) fail for there is still %u output",
			__func__, cam_id, out_cnt);
		return -EINVAL;
	}

	isp->fw_cmd_resp_strs_info[fw_stream_id].status =
		FW_CMD_RESP_STR_STATUS_OCCUPIED;
	dev_dbg(dev, "%s: reset fw stream_id %d to be occupied", __func__,
		fw_stream_id);

	isp_reset_camera_info(isp, cam_id);
	snr_info = &isp->sensor_info[cam_id];
	do {
		ele = isp_rm_cmd_from_cmdq_by_stream(isp, fw_stream_id, false);
		if (!ele)
			break;
		if (ele->mc_addr)
			isp_fw_ret_indirect_cmd_pl(isp, &isp->fw_indirect_cmd_pl_buf_mgr,
						   ele->mc_addr);
		kfree(ele);
	} while (ele);

	for (i = 0; i < STREAM_META_BUF_COUNT; i++) {
		if (snr_info->meta_mc[i]) {
			isp_fw_ret_indirect_cmd_pl(isp, &isp->fw_indirect_cmd_pl_buf_mgr,
						   snr_info->meta_mc[i]);
			snr_info->meta_mc[i] = 0;
		}
	}

	return 0;
}

static struct sys_img_buf_info *sys_img_buf_handle_cpy(struct sys_img_buf_info *hdl_in)
{
	struct sys_img_buf_info *imgbuf;

	imgbuf = kmalloc(sizeof(*imgbuf), GFP_KERNEL);
	if (imgbuf)
		memcpy(imgbuf, hdl_in, sizeof(struct sys_img_buf_info));
	else
		pr_err("failed to alloc buf");

	return imgbuf;
};

int isp_intf_open_camera(struct isp_context *isp,
			 enum camera_port_id cid,
			 u32 res_fps_id,
			 u32 flag)
{
	struct device *dev;
	u32 index;
	enum camera_port_id actual_cid = cid;
	bool rel_sem = true;

	if (!is_para_legal(isp, cid) ||
	    !is_para_legal(isp, actual_cid))
		return -EINVAL;

	dev = &isp->amd_cam->pdev->dev;
	dev_dbg(dev, "%s cid[%d] fpsid[%d]  flag:0x%x",
		__func__, actual_cid, res_fps_id, flag);

	if (ISP_GET_STATUS(isp) == ISP_STATUS_UNINITED) {
		dev_err(dev, "%s cid[%d] fail for isp uninit",
			__func__, actual_cid);
		return -EINVAL;
	}

	mutex_lock(&isp->ops_mutex);
	if (isp->sensor_info[actual_cid].sensor_opened ||
	    isp->sensor_info[cid].sensor_opened) {
		dev_dbg(dev, "%s cid[%d] has opened, do nothing",
			__func__, actual_cid);
		mutex_unlock(&isp->ops_mutex);
		return 0;
	}
	if (is_camera_started(isp, actual_cid)) {
		mutex_unlock(&isp->ops_mutex);
		dev_dbg(dev, "%s cid[%d] suc for already",
			__func__, actual_cid);
		return 0;
	}

	dev_dbg(dev, "enable AGPIO85");
	enable_agpio85(isp, true);

	if (isp->fw_mem_pool[cid] && isp->fw_mem_pool[cid]->sys_addr &&
	    isp->fw_mem_pool[cid]->mem_size < INTERNAL_MEMORY_POOL_SIZE) {
		/* the original buffer is too small, free it and do re-alloc */
		isp_gpu_mem_free(isp, isp->fw_mem_pool[cid]);
		isp->fw_mem_pool[cid] = NULL;
	}
	if (!isp->fw_mem_pool[cid]) {
		isp->fw_mem_pool[cid] =
			isp_gpu_mem_alloc(isp, INTERNAL_MEMORY_POOL_SIZE);
		if (!isp->fw_mem_pool[cid]) {
			mutex_unlock(&isp->ops_mutex);
			dev_err(dev, "%s cid[%d] fail for mempool alloc",
				__func__, actual_cid);
			return 0;
		}
	}

	switch (actual_cid) {
	case CAMERA_PORT_1:
		isp->sensor_info[actual_cid].cam_type = CAMERA_PORT_1_RAW_TYPE;
		isp->sensor_info[cid].cam_type = CAMERA_PORT_1_RAW_TYPE;
		break;
	case CAMERA_PORT_2:
		isp->sensor_info[actual_cid].cam_type = CAMERA_PORT_2_RAW_TYPE;
		isp->sensor_info[cid].cam_type = CAMERA_PORT_2_RAW_TYPE;
		break;
	default:
		isp->sensor_info[actual_cid].cam_type = CAMERA_PORT_0_RAW_TYPE;
		isp->sensor_info[cid].cam_type = CAMERA_PORT_0_RAW_TYPE;
		break;
	}

	isp->sensor_info[actual_cid].start_str_cmd_sent = 0;
	isp->sensor_info[actual_cid].channel_buf_sent_cnt = 0;

	if (is_failure(isp_ip_pwr_on(isp, actual_cid, index,
				     flag & OPEN_CAMERA_FLAG_HDR))) {
		dev_err(dev, "isp_ip_pwr_on fail");
		goto fail;
	}

	if (!isp_semaphore_acquire(isp)) {
		/* try to continue opening sensor cause it may still work */
		dev_err(dev, "in %s, fail acquire isp semaphore,ignore",
			__func__);
		rel_sem = FALSE;
	}

	if (rel_sem)
		isp_semaphore_release(isp);

	if (is_failure(isp_boot_isp_fw_boot(isp))) {
		dev_err(dev, "isp_fw_start fail");
		goto fail;
	}

	isp->sensor_info[actual_cid].sensor_opened = 1;
	isp->sensor_info[cid].sensor_opened = 1;
	mutex_unlock(&isp->ops_mutex);

	get_available_fw_cmdresp_stream_id(isp, actual_cid);

	return 0;
fail:
	mutex_unlock(&isp->ops_mutex);
	isp_intf_close_camera(isp, cid);
	return -EINVAL;
}

int isp_intf_close_camera(struct isp_context *isp, enum camera_port_id cid)
{
	struct device *dev;
	struct isp_sensor_info *sif;
	u32 cnt;
	struct isp_cmd_element *ele = NULL;
	unsigned int index = 0;

	if (!is_para_legal(isp, cid))
		return -EINVAL;

	dev = &isp->amd_cam->pdev->dev;

	mutex_lock(&isp->ops_mutex);
	dev_dbg(dev, "%s, cid %d", __func__, cid);
	sif = &isp->sensor_info[cid];
	if (sif->status == START_STATUS_STARTED) {
		dev_err(dev, "%s, fail stream still running", __func__);
		goto fail;
	}
	sif->status = START_STATUS_NOT_START;

	if (sif->fw_stream_id != FW_CMD_RESP_STREAM_ID_MAX)
		reset_fw_cmdresp_strinfo(isp, sif->fw_stream_id);

	/* index = get_index_from_res_fps_id(isp, cid, sif->cur_res_fps_id); */
	cnt = isp_get_started_stream_count(isp);
	if (cnt > 0) {
		dev_dbg(dev, "%s, no need power off isp", __func__);
		isp_clk_change(isp, cid, index, sif->hdr_enable, false);
		goto suc;
	}

	dev_dbg(dev, "%s, power off isp", __func__);

	isp_boot_disable_ccpu(isp);
	isp_clk_change(isp, cid, index, sif->hdr_enable, false);
	ISP_SET_STATUS(isp, ISP_STATUS_PWR_OFF);
	isp_ip_pwr_off(isp);

	do {
		enum fw_cmd_resp_stream_id stream_id;

		stream_id = FW_CMD_RESP_STREAM_ID_GLOBAL;
		ele = isp_rm_cmd_from_cmdq_by_stream(isp, stream_id, false);

		if (!ele)
			break;
		if (ele->mc_addr)
			isp_fw_ret_indirect_cmd_pl(isp, &isp->fw_indirect_cmd_pl_buf_mgr, ele->mc_addr);
		kfree(ele);
	} while (ele);

	isp_gpu_mem_free(isp, isp->fw_cmd_resp_buf);
	isp->fw_cmd_resp_buf = NULL;
	isp_gpu_mem_free(isp, isp->fw_running_buf);
	isp->fw_running_buf = NULL;

	dev_dbg(dev, "disable AGPIO85 and delay 20ms");
	enable_agpio85(isp, false);
	msleep(20);
suc:
	mutex_unlock(&isp->ops_mutex);
	isp->sensor_info[cid].sensor_opened = 0;
	isp->prev_buf_cnt_sent = 0;
	dev_dbg(dev, "%s, suc", __func__);
	return 0;
fail:
	mutex_unlock(&isp->ops_mutex);
	dev_err(dev, "%s, fail", __func__);
	return -EINVAL;
}

void isp_unmap_sys_2_mc(struct isp_context *isp,
			struct isp_mapped_buf_info *buff)
{
}

s32 isp_get_pipeline_id(struct isp_context *isp, enum camera_port_id cid)
{
	return MIPI0CSISCSTAT0_LME_ISP_PIPELINE_ID;
}

/* start stream for cam_id, return 0 for success others for fail */
int isp_intf_start_stream(struct isp_context *isp,
			  enum camera_port_id cam_id,
			  enum stream_id stream_id)
{
	struct device *dev;
	int ret;
	enum pvt_img_fmt fmt;
	int ret_val = 0;
	enum fw_cmd_resp_stream_id fw_stream_id;
	struct isp_stream_info *sif = NULL;
	struct isp_sensor_info *snrif = NULL;

	if (!is_para_legal(isp, cam_id))
		return -EINVAL;

	dev = &isp->amd_cam->pdev->dev;

	if (stream_id > STREAM_ID_NUM) {
		dev_err(dev, "%s fail bad para, invalid stream_id:%d",
			__func__, stream_id);
		return -EINVAL;
	}

	fmt = isp->sensor_info[cam_id].str_info[stream_id].format;
	if (fmt == PVT_IMG_FMT_INVALID || fmt >= PVT_IMG_FMT_MAX) {
		dev_err(dev, "%s fail,cid:%d,str:%d,fmt not set",
			__func__, cam_id, stream_id);
		return -EINVAL;
	}

	mutex_lock(&isp->ops_mutex);
	if (ISP_GET_STATUS(isp) < ISP_STATUS_FW_RUNNING) {
		mutex_unlock(&isp->ops_mutex);
		dev_err(dev, "%s(cid:%d,str:%d) fail, bad fsm %d",
			__func__, cam_id, stream_id, ISP_GET_STATUS(isp));
		return -EINVAL;
	}

	dev_dbg(dev, "%s,cid:%d,sid:%d", __func__, cam_id, stream_id);
	snrif = &isp->sensor_info[cam_id];
	sif = &isp->sensor_info[cam_id].str_info[stream_id];
	fw_stream_id = isp_get_fwresp_stream_id(isp, cam_id, stream_id);
	if (fw_stream_id < FW_CMD_RESP_STREAM_ID_GLOBAL ||
	    fw_stream_id >= FW_CMD_RESP_STREAM_ID_MAX) {
		dev_err(dev, "fw_stream_id is illegal value, bad para, fw_stream_id: %d", fw_stream_id);
		ret = -EINVAL;
		goto quit;
	}

	dev_dbg(dev, "%s cid:%d, str:%d, fw stream id: %d", __func__,
		cam_id, stream_id, fw_stream_id);

	if (isp_init_stream(isp, cam_id, fw_stream_id)) {
		dev_err(dev, "%s fail for isp_init_stream", __func__);
		ret = -EINVAL;
		goto quit;
	}

	if (sif->start_status == START_STATUS_NOT_START ||
	    sif->start_status == START_STATUS_STARTING) {
		if (sif->width && sif->height && sif->luma_pitch_set) {
			goto do_out_setup;
		} else {
			sif->start_status = START_STATUS_STARTING;
			ret = 0;
			dev_dbg(dev, "%s suc,setup out later", __func__);
			goto quit;
		}
	} else if (sif->start_status == START_STATUS_STARTED) {
		ret = 0;
		dev_dbg(dev, "%s success, do nothing", __func__);
		goto quit;
	} else if (sif->start_status == START_STATUS_START_FAIL) {
		ret = -EINVAL;
		dev_err(dev, "%s start fail", __func__);
		goto quit;
	} else if (sif->start_status == START_STATUS_START_STOPPING) {
		ret = -EINVAL;
		dev_err(dev, "%s fail,in stopping", __func__);
		goto quit;
	} else {
		ret = -EINVAL;
		dev_err(dev, "%s fail,bad stat %d", __func__, sif->start_status);
		goto quit;
	}
do_out_setup:
	if (isp_setup_output(isp, cam_id, stream_id)) {
		dev_err(dev, "%s fail for setup out", __func__);
		ret = -EINVAL;
	} else {
		ret = 0;
		dev_dbg(dev, "%s suc,setup out suc", __func__);
	}
quit:
	if (is_failure(ret))
		ret_val = -EINVAL;
	else
		ret_val = 0;

	mutex_unlock(&isp->ops_mutex);
	if (ret_val) {
		isp_intf_stop_stream(isp, cam_id, stream_id);
		dev_err(dev, "%s fail", __func__);
	} else {
		dev_dbg(dev, "%s suc", __func__);
	}

	return ret_val;
}

/* stop stream for cam_id, return 0 for success others for fail */
int isp_intf_stop_stream(struct isp_context *isp,
			 enum camera_port_id cid,
			 enum stream_id sid)
{
	struct device *dev;
	struct isp_stream_info *sif = NULL;
	int ret_val = 0;
	struct cmd_enable_out_ch cmd_ch_disable;
	u32 out_cnt = 0;
	u32 timeout;
	enum fw_cmd_resp_stream_id fw_stream_id;
	struct isp_mapped_buf_info *cur = NULL;

	if (!is_para_legal(isp, cid))
		return -EINVAL;

	dev = &isp->amd_cam->pdev->dev;

	if (sid > STREAM_ID_NUM) {
		dev_err(dev, "%s fail,bad para,sid:%d", __func__, sid);
		return -EINVAL;
	}

	mutex_lock(&isp->ops_mutex);

	fw_stream_id = isp_get_fwresp_stream_id(isp, cid, sid);
	if (fw_stream_id < FW_CMD_RESP_STREAM_ID_GLOBAL ||
	    fw_stream_id >= FW_CMD_RESP_STREAM_ID_MAX) {
		dev_err(dev, "%s Invalid fw_stream_id", __func__);
		ret_val = -EINVAL;
		goto quit;
	}

	sif = &isp->sensor_info[cid].str_info[sid];
	if (!sif) {
		dev_err(dev, "%s nullptr failure of sif", __func__);
		ret_val = -EINVAL;
		goto quit;
	}

	dev_dbg(dev, "%s,cid:%d,str:%d,status %i",
		__func__, cid, sid, sif->start_status);

	if (sif->start_status == START_STATUS_NOT_START)
		goto goon;

	switch (sid) {
	case STREAM_ID_PREVIEW:
		cmd_ch_disable.ch = ISP_PIPE_OUT_CH_PREVIEW;
		break;
	case STREAM_ID_VIDEO:
		cmd_ch_disable.ch = ISP_PIPE_OUT_CH_VIDEO;
		break;
	case STREAM_ID_ZSL:
		cmd_ch_disable.ch = ISP_PIPE_OUT_CH_STILL;
		break;
	default:
		dev_dbg(dev, "%s,never here", __func__);
		ret_val = -EINVAL;
		break;
	}
	if (ret_val)
		goto quit;

	cmd_ch_disable.is_enable = false;

	if (sif->start_status != START_STATUS_STARTED)
		goto skip_stop;

	cur = (struct isp_mapped_buf_info *)
	      isp_list_get_first_without_rm(&sif->buf_in_fw);
	timeout = (1000 * 6);
	if (is_failure(isp_send_fw_cmd_sync(isp,
					    CMD_ID_ENABLE_OUT_CHAN,
					    fw_stream_id, FW_CMD_PARA_TYPE_DIRECT,
					    &cmd_ch_disable, sizeof(cmd_ch_disable),
					    300,
					    NULL, NULL))) {
		dev_err(dev, "%s,send disable str fail", __func__);
	} else {
		dev_dbg(dev, "%s wait disable suc", __func__);
	}

	if (isp_send_fw_cmd_sync(isp, CMD_ID_STOP_STREAM, fw_stream_id,
				 FW_CMD_PARA_TYPE_DIRECT,
				 NULL,
				 0,
				 timeout,
				 NULL, NULL)) {
		dev_err(dev, "send stop steam fail");
	} else {
		dev_dbg(dev, "wait stop stream suc");
	};

skip_stop:
	isp_take_back_str_buf(isp, sif, cid, sid);
	sif->start_status = START_STATUS_NOT_START;
	isp_reset_str_info(isp, cid, sid);

	ret_val = 0;

goon:
	isp_get_stream_output_bits(isp, cid, &out_cnt);
	if (out_cnt > 0) {
		ret_val = 0;
		goto quit;
	}

quit:
	if (ret_val) {
		dev_err(dev, "%s fail", __func__);
	} else {
		if (out_cnt == 0) {
			isp_uninit_stream(isp, cid, fw_stream_id);
			struct isp_sensor_info *sensor_if;

			/* Poweroff sensor before stop stream as */
			sensor_if = &isp->sensor_info[cid];
			if (cid < CAMERA_PORT_MAX &&
			    sensor_if->cam_type != CAMERA_TYPE_MEM) {
				/* isp_snr_close(isp, cid); */
			} else {
				struct isp_pwr_unit *pwr_unit;

				pwr_unit = &isp->isp_pu_cam[cid];
				mutex_lock(&pwr_unit->pwr_status_mutex);
				pwr_unit->pwr_status = ISP_PWR_UNIT_STATUS_OFF;
				mutex_unlock(&pwr_unit->pwr_status_mutex);
			}
			sensor_if->raw_width = 0;
			sensor_if->raw_height = 0;
		}
		dev_dbg(dev, "%s suc", __func__);
	}
	mutex_unlock(&isp->ops_mutex);

	return ret_val;
}

void isp_intf_reg_notify_cb(struct isp_context *isp, enum camera_port_id cam_id,
			    func_isp_module_cb cb, void *cb_context)
{
	struct device *dev;

	if (!is_para_legal(isp, cam_id) || !cb)
		return;

	dev = &isp->amd_cam->pdev->dev;
	isp->evt_cb[cam_id] = cb;
	isp->evt_cb_context[cam_id] = cb_context;
	dev_dbg(dev, "cid[%d] suc", cam_id);
}

void isp_intf_unreg_notify_cb(struct isp_context *isp, enum camera_port_id cam_id)
{
	struct device *dev;

	if (!is_para_legal(isp, cam_id))
		return;

	dev = &isp->amd_cam->pdev->dev;
	isp->evt_cb[cam_id] = NULL;
	isp->evt_cb_context[cam_id] = NULL;
	dev_dbg(dev, "cid[%d] suc", cam_id);
}

int isp_intf_set_stream_param(struct isp_context *isp,
			      enum camera_port_id cam_id,
			      enum stream_id stream_id,
			      enum para_id para_type,
			      void *para_value)
{
	struct device *dev;
	int ret = 0;
	int func_ret = 0;

	if (!is_para_legal(isp, cam_id))
		return -EINVAL;

	dev = &isp->amd_cam->pdev->dev;

	if (stream_id > STREAM_ID_NUM) {
		dev_err(dev, "%s fail bad para,sid%d", __func__, stream_id);
		return -EINVAL;
	}

	mutex_lock(&isp->ops_mutex);
	dev_dbg(dev, "%s,cid %d,sid %d,para %s(%d)",
		__func__, cam_id, stream_id,
		isp_dbg_get_para_str(para_type), para_type);

	switch (para_type) {
	case PARA_ID_DATA_FORMAT: {
		enum pvt_img_fmt data_fmat =
			(*(enum pvt_img_fmt *)para_value);
		ret = isp_set_stream_data_fmt(isp, cam_id, stream_id, data_fmat);
		if (is_failure(ret)) {
			dev_err(dev, "%s(FMT) fail for set fmt:%s",
				__func__,
				isp_dbg_get_pvt_fmt_str(data_fmat));
			func_ret = -EINVAL;
			break;
		}
		dev_dbg(dev, "%s(FMT) suc set fmt:%s", __func__,
			isp_dbg_get_pvt_fmt_str(data_fmat));
		break;
	}
	case PARA_ID_DATA_RES_FPS_PITCH: {
		struct pvt_img_res_fps_pitch *data_pitch =
			(struct pvt_img_res_fps_pitch *)para_value;
		ret = isp_set_str_res_fps_pitch(isp, cam_id, stream_id, data_pitch);
		if (is_failure(ret)) {
			dev_err(dev, "%s(RES_FPS_PITCH) fail for set",
				__func__);
			func_ret = -EINVAL;
			break;
		}
		dev_dbg(dev, "%s(RES_FPS_PITCH) suc", __func__);
		break;
	}
	default:
		dev_err(dev, "%s fail for not supported", __func__);
		func_ret = -EINVAL;
		break;
	}
	mutex_unlock(&isp->ops_mutex);
	return func_ret;
}

int isp_intf_set_stream_buf(struct isp_context *isp, enum camera_port_id cam_id,
			    enum stream_id stream_id,
			    struct sys_img_buf_info *buf_hdl)
{
	struct device *dev;
	struct isp_mapped_buf_info *gen_img = NULL;
	int result;
	int ret = -EINVAL;

	if (!is_para_legal(isp, cam_id))
		return -EINVAL;

	dev = &isp->amd_cam->pdev->dev;

	if (!buf_hdl || buf_hdl->planes[0].mc_addr == 0) {
		dev_err(dev, "fail bad para, sid[%d]", stream_id);
		return -EINVAL;
	}

	mutex_lock(&isp->ops_mutex);
	dev_dbg(dev, "cid[%d] sid[%d] buflen(%u)", cam_id, stream_id,
		buf_hdl->planes[0].len);
	if (ISP_GET_STATUS(isp) < ISP_STATUS_FW_RUNNING) {
		dev_err(dev, "fail fsm %d", ISP_GET_STATUS(isp));
		mutex_unlock(&isp->ops_mutex);
		return ret;
	}

	buf_hdl = sys_img_buf_handle_cpy(buf_hdl);
	if (!buf_hdl) {
		dev_err(dev, "fail for sys_img_buf_handle_cpy");
		goto quit;
	}

	gen_img = isp_map_sys_2_mc(isp, buf_hdl, cam_id, stream_id);

	/* isp_dbg_show_map_info(gen_img); */
	if (!gen_img) {
		dev_err(dev, "fail for isp_map_sys_2_mc");
		ret = -EINVAL;
		goto quit;
	}

	result = fw_if_send_img_buf(isp, gen_img, cam_id, stream_id);
	if (result) {
		dev_err(dev, "fail for fw_if_send_img_buf");
		goto quit;
	}

	if (!isp->sensor_info[cam_id].start_str_cmd_sent) {
		isp->sensor_info[cam_id].channel_buf_sent_cnt++;

		if (isp->sensor_info[cam_id].channel_buf_sent_cnt >=
		    MIN_CHANNEL_BUF_CNT_BEFORE_START_STREAM) {
			result = isp_send_fw_cmd(isp, CMD_ID_START_STREAM,
						 isp_get_fwresp_stream_id(isp, cam_id, stream_id),
						 FW_CMD_PARA_TYPE_DIRECT, NULL, 0);

			if (result) {
				dev_err(dev, "<-%s fail to START_STREAM", __func__);
				return -EINVAL;
			}
			isp->sensor_info[cam_id].start_str_cmd_sent = 1;
			isp->sensor_info[cam_id].str_info[stream_id].start_status = START_STATUS_STARTED;
			isp->sensor_info[cam_id].status = START_STATUS_STARTED;
		} else {
			dev_dbg(dev, "no send START_STREAM, start_sent %u, buf_sent %u",
				isp->sensor_info[cam_id].start_str_cmd_sent,
				isp->sensor_info[cam_id].channel_buf_sent_cnt);
		}
	}

	isp->sensor_info[cam_id].str_info[stream_id].buf_num_sent++;
	isp_list_insert_tail(&isp->sensor_info[cam_id].str_info[stream_id].buf_in_fw,
			     (struct list_node *)gen_img);
	ret = 0;

quit:
	mutex_unlock(&isp->ops_mutex);
	if (ret) {
		kfree(buf_hdl);
		if (gen_img) {
			isp_unmap_sys_2_mc(isp, gen_img);
			kfree(gen_img);
		}
	}

	return ret;
}

int isp_intf_set_roi(struct isp_context *isp,
		     enum camera_port_id cam_id,
		     u32 type,
		     struct isp_roi_info *roi)
{
	struct device *dev;
	enum fw_cmd_resp_stream_id fw_stream_id = FW_CMD_RESP_STREAM_ID_MAX;
	u32 fw_cmd = CMD_ID_SET_3A_ROI;
	u32 i;
	struct aa_roi *roi_param;

	if (!is_para_legal(isp, cam_id))
		return -EINVAL;

	dev = &isp->amd_cam->pdev->dev;

	if (!roi) {
		dev_err(dev, "%s fail bad para", __func__);
		return -EINVAL;
	}

	roi_param = kzalloc(sizeof(*roi_param), GFP_KERNEL);
	if (IS_ERR_OR_NULL(roi_param)) {
		dev_err(dev, "%s fail allocate roi_param", __func__);
		return -ENOMEM;
	}

	if (ISP_GET_STATUS(isp) < ISP_STATUS_FW_RUNNING) {
		dev_err(dev, "%s fail fsm %d, cid %u",
			__func__, ISP_GET_STATUS(isp), cam_id);
		kfree(roi_param);
		return -EINVAL;
	}

	dev_dbg(dev, "%s cid %u type %u(1:AE 2:AWB 4:AF),kind %u(1:Touch 2:Face)",
		__func__, cam_id, type, roi->kind);

	if (type & ISP_3A_TYPE_AF)
		roi_param->roi_type |= ROI_TYPE_MASK_AF;
	if (type & ISP_3A_TYPE_AE)
		roi_param->roi_type |= ROI_TYPE_MASK_AE;
	if (type & ISP_3A_TYPE_AWB)
		roi_param->roi_type |= ROI_TYPE_MASK_AWB;

	if (roi->kind & ISP_ROI_KIND_TOUCH)
		roi_param->mode_mask |= ROI_MODE_MASK_TOUCH;
	if (roi->kind & ISP_ROI_KIND_FACE)
		roi_param->mode_mask |= ROI_MODE_MASK_FACE;

	roi_param->touch_info.touch_num = roi->touch_info.num;
	for (i = 0; i < roi->touch_info.num; i++) {
		struct isp_touch_area_t *des = &roi_param->touch_info.touch_area[i];
		struct isp_touch_area *src = &roi->touch_info.area[i];

		des->points.top_left.x = src->points.top_left.x;
		des->points.top_left.y = src->points.top_left.y;
		des->points.bottom_right.x = src->points.bottom_right.x;
		des->points.bottom_right.y = src->points.bottom_right.y;
		des->touch_weight = src->weight;

		dev_dbg(dev, "touch %u/%u, top(%u:%u),bottom(%u:%u),weight %u",
			i, roi_param->touch_info.touch_num,
			des->points.top_left.x, des->points.top_left.y,
			des->points.bottom_right.x,
			des->points.bottom_right.y, des->touch_weight);
	}

	roi_param->fd_info.is_enabled = roi->fd_info.is_enabled;
	roi_param->fd_info.frame_count = roi->fd_info.frame_count;
	roi_param->fd_info.is_marks_enabled = roi->fd_info.is_marks_enabled;
	roi_param->fd_info.face_num = roi->fd_info.num;

	if (!roi_param->fd_info.frame_count)
		roi_param->fd_info.frame_count = isp->sensor_info[cam_id].poc;

	for (i = 0; i < roi->fd_info.num; i++) {
		struct isp_fd_face_info_t *des = &roi_param->fd_info.face[i];
		struct isp_fd_face_info *src = &roi->fd_info.face[i];

		des->face_id = src->face_id;
		des->score = src->score;
		des->face_area.top_left.x = src->face_area.top_left.x;
		des->face_area.top_left.y = src->face_area.top_left.y;
		des->face_area.bottom_right.x = src->face_area.bottom_right.x;
		des->face_area.bottom_right.y = src->face_area.bottom_right.y;
		des->marks.eye_left.x = src->marks.eye_left.x;
		des->marks.eye_left.y = src->marks.eye_left.y;
		des->marks.eye_right.x = src->marks.eye_right.x;
		des->marks.eye_right.y = src->marks.eye_right.y;
		des->marks.nose.x = src->marks.nose.x;
		des->marks.nose.y = src->marks.nose.y;
		des->marks.mouse_left.x = src->marks.mouse_left.x;
		des->marks.mouse_left.y = src->marks.mouse_left.y;
		des->marks.mouse_right.x = src->marks.mouse_right.x;
		des->marks.mouse_right.y = src->marks.mouse_right.y;

		dev_dbg(dev, "face %u/%u,en:%u,top(%u:%u),bottom(%u:%u),score %u,face_id %u",
			i, roi_param->fd_info.frame_count,
			roi_param->fd_info.is_marks_enabled,
			des->face_area.top_left.x, des->face_area.top_left.y,
			des->face_area.bottom_right.x, des->face_area.bottom_right.y,
			des->score, des->face_id);

		if (roi_param->fd_info.is_marks_enabled) {
			dev_dbg(dev, "marks eye_left(%u:%u) eye_right(%u:%u) nose(%u:%u)",
				des->marks.eye_left.x, des->marks.eye_left.y,
				des->marks.eye_right.x, des->marks.eye_right.y,
				des->marks.nose.x, des->marks.nose.y);
			dev_dbg(dev, "marks mouse_left(%u:%u) mouse_right(%u:%u)",
				des->marks.mouse_left.x, des->marks.mouse_left.y,
				des->marks.mouse_right.x, des->marks.mouse_right.y);
		}
	}

	/* Get fw stream id for normal stream */
	fw_stream_id = isp_get_fw_stream_id(isp, cam_id);
	if (fw_stream_id == FW_CMD_RESP_STREAM_ID_MAX) {
		dev_err(dev, "%s: failed for fw_stream_id:%d",
			__func__, fw_stream_id);
		kfree(roi_param);
		return -EINVAL;
	}

	if (is_failure(isp_send_fw_cmd(isp, fw_cmd, fw_stream_id,
				       FW_CMD_PARA_TYPE_INDIRECT,
				       &roi_param, sizeof(roi_param)))) {
		dev_err(dev, "%s: failed by send cmd for fw_stream_id:%d",
			__func__, fw_stream_id);
		kfree(roi_param);
		return -EINVAL;
	}

	dev_dbg(dev, "%s: suc for fw_stream_id:%d", __func__, fw_stream_id);
	kfree(roi_param);
	return 0;
}

int isp_intf_init(struct isp_context *isp, struct amd_cam *pamd_cam)
{
	if (!isp || !pamd_cam) {
		pr_err("%s NULL isp / amd_cam context", __func__);
		return -EINVAL;
	}

	isp->amd_cam = pamd_cam;
	ispm_context_init(isp);

	return 0;
}

void isp_intf_fini(struct isp_context *isp)
{
	if (!isp) {
		pr_err("%s NULL isp context", __func__);
		return;
	};

	ispm_context_uninit(isp);
}
