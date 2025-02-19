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

#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include "isp_debug.h"
#include "isp_common.h"
#include "isp_hwa.h"
#include "isp_module_intf.h"
#include "isp_core.h"
#include "isp_common.h"
#include "isp_mc_addr_mgr.h"
#include "isp_utils.h"

void isp_clear_cmdq(struct isp_context *isp)
{
	struct isp_cmd_element *cur;
	struct isp_cmd_element *nxt;

	mutex_lock(&isp->cmd_q_mtx);
	cur = isp->cmd_q;
	isp->cmd_q = NULL;
	while (cur) {
		nxt = cur->next;
		if (cur->mc_addr)
			isp_fw_ret_indirect_cmd_pl(isp, &isp->fw_indirect_cmd_pl_buf_mgr,
						   cur->mc_addr);
		kfree(cur);
		cur = nxt;
	}
	mutex_unlock(&isp->cmd_q_mtx);
}

bool is_camera_started(struct isp_context *isp_context,
		       enum camera_port_id cam_id)
{
	struct isp_pwr_unit *pwr_unit;

	if (!is_para_legal(isp_context, cam_id))
		return false;

	pwr_unit = &isp_context->isp_pu_cam[cam_id];
	if (pwr_unit->pwr_status == ISP_PWR_UNIT_STATUS_ON)
		return 1;
	else
		return 0;
}

bool get_available_fw_cmdresp_stream_id(void *context, enum camera_port_id cam_id)
{
	struct device *dev;
	struct isp_context *isp = (struct isp_context *)context;
	struct isp_sensor_info *p_sensor_info;
	enum camera_port_id actual_cid = cam_id;
	bool ret = false;

	if (!is_para_legal(isp, cam_id))
		return false;

	dev = &isp->amd_cam->pdev->dev;

	dev_dbg(dev, "-> %s, cid[%d]", __func__, cam_id);

	p_sensor_info = &isp->sensor_info[actual_cid];
	for (enum fw_cmd_resp_stream_id idx = FW_CMD_RESP_STREAM_ID_1;
	     idx < FW_CMD_RESP_STREAM_ID_MAX; idx++) {
		struct fw_cmd_resp_str_info *p_fwstr_info =
				&isp->fw_cmd_resp_strs_info[idx];
		if (p_fwstr_info->status == FW_CMD_RESP_STR_STATUS_IDLE) {
			p_sensor_info->fw_stream_id = idx;
			p_fwstr_info->status = FW_CMD_RESP_STR_STATUS_OCCUPIED;
			p_fwstr_info->cid_owner = actual_cid;
			dev_dbg(dev, "%s, cid[%d], fw stream_id: %d", __func__,
				cam_id, idx);
			ret = true;
			break;
		}
	}

	if (!ret) {
		dev_err(dev, "<-%s, can't get valid fw stream_id for cid%d", __func__,
			cam_id);
		return ret;
	}

	return ret;
}

void reset_fw_cmdresp_strinfo(void *context,
			      enum fw_cmd_resp_stream_id fw_stream_id)
{
	struct device *dev;
	struct isp_context *isp = (struct isp_context *)context;
	struct fw_cmd_resp_str_info *p_fwstr_info;

	if (!isp || fw_stream_id >= FW_CMD_RESP_STREAM_ID_MAX) {
		pr_err("-><- %s, fail for para fw stream_id:%d",
		       __func__, fw_stream_id);
		return;
	}

	dev = &isp->amd_cam->pdev->dev;
	p_fwstr_info = &isp->fw_cmd_resp_strs_info[fw_stream_id];
	p_fwstr_info->status = FW_CMD_RESP_STR_STATUS_IDLE;
	p_fwstr_info->cid_owner = CAMERA_PORT_MAX;

	dev_dbg(dev, "-><- %s, for fw stream_id: %d", __func__, fw_stream_id);
}

u32 isp_get_started_stream_count(struct isp_context *isp)
{
	u32 cnt = 0;
	enum camera_port_id cid;

	if (!isp)
		return 0;

	for (cid = CAMERA_PORT_0; cid < CAMERA_PORT_MAX; cid++) {
		if (isp->sensor_info[cid].status == START_STATUS_STARTED)
			cnt++;
	}
	return cnt;
}

struct isp_cmd_element
*isp_rm_cmd_from_cmdq_by_stream(struct isp_context *isp,
				enum fw_cmd_resp_stream_id stream,
				int signal_evt)
{
	struct isp_cmd_element *curr_element;
	struct isp_cmd_element *prev_element;
	struct device *dev = &isp->amd_cam->pdev->dev;

	mutex_lock(&isp->cmd_q_mtx);

	curr_element = isp->cmd_q;
	if (!curr_element) {
		dev_warn(dev, "%s: fail empty cmd q, stream[%u]",
			 __func__, stream);
		goto quit;
	}

	/* process the first element */
	if (curr_element->stream == stream) {
		isp->cmd_q = curr_element->next;
		curr_element->next = NULL;
		goto quit;
	}

	prev_element = curr_element;
	curr_element = curr_element->next;

	while (curr_element) {
		if (curr_element->stream == stream) {
			prev_element->next = curr_element->next;
			curr_element->next = NULL;
			goto quit;
		}

		prev_element = curr_element;
		curr_element = curr_element->next;
	}

	dev_err(dev, "%s: stream[%u] no found", __func__, stream);
quit:
	if (curr_element && curr_element->evt && signal_evt)
		isp_event_signal(0, curr_element->evt);
	mutex_unlock(&isp->cmd_q_mtx);
	return curr_element;
}

enum fw_cmd_resp_stream_id isp_get_fwresp_stream_id(struct isp_context *isp,
						    enum camera_port_id cid,
						    enum stream_id stream_id)
{
	struct device *dev;
	enum camera_port_id actual_cid = cid;
	enum fw_cmd_resp_stream_id fw_stream_id;
	struct isp_sensor_info *sensor_info;

	if (!is_para_legal(isp, cid))
		return FW_CMD_RESP_STREAM_ID_MAX;

	dev = &isp->amd_cam->pdev->dev;

	sensor_info = &isp->sensor_info[actual_cid];
	fw_stream_id = sensor_info->fw_stream_id;

	dev_dbg(dev, "-><- %s actual_cid:%d[stream%d] the related fw stream_id:%d",
		__func__, actual_cid, stream_id, fw_stream_id);

	return fw_stream_id;
}

enum fw_cmd_resp_stream_id isp_get_fw_stream_id(struct isp_context *isp,
						enum camera_port_id cid)
{
	struct device *dev;
	struct isp_sensor_info *sensor_info;
	enum fw_cmd_resp_stream_id fw_stream_id = FW_CMD_RESP_STREAM_ID_MAX;

	if (!is_para_legal(isp, cid))
		return FW_CMD_RESP_STREAM_ID_MAX;

	dev = &isp->amd_cam->pdev->dev;
	sensor_info = &isp->sensor_info[cid];
	dev_dbg(dev, "-> %s cid:%d, camtype %u", __func__,
		cid, sensor_info->cam_type);

	fw_stream_id = sensor_info->fw_stream_id;
	if (fw_stream_id != FW_CMD_RESP_STREAM_ID_MAX &&
	    isp->fw_cmd_resp_strs_info[fw_stream_id].status
	    != FW_CMD_RESP_STR_STATUS_INITIALED)
		fw_stream_id = FW_CMD_RESP_STREAM_ID_MAX;

	dev_dbg(dev, "<- %s, cid:%d, fw stream_id:%d", __func__, cid, fw_stream_id);
	return fw_stream_id;
}

u32 isp_get_stream_output_bits(struct isp_context *isp,
			       enum camera_port_id cam_id,
			       u32 *stream_cnt)
{
	u32 ret = 0;
	u32 cnt = 0;
	enum start_status stat;
	struct isp_sensor_info *sif;

	if (stream_cnt)
		*stream_cnt = 0;

	if (!is_para_legal(isp, cam_id))
		return 0;

	sif = &isp->sensor_info[cam_id];
	stat = sif->str_info[STREAM_ID_PREVIEW].start_status;
	if (stat == START_STATUS_STARTED ||
	    stat == START_STATUS_STARTING) {
		ret |= STREAM__PREVIEW_OUTPUT_BIT;
		cnt++;
	}

	stat = sif->str_info[STREAM_ID_VIDEO].start_status;
	if (stat == START_STATUS_STARTED ||
	    stat == START_STATUS_STARTING) {
		ret |= STREAM__VIDEO_OUTPUT_BIT;
		cnt++;
	}

	stat = sif->str_info[STREAM_ID_ZSL].start_status;
	if (stat == START_STATUS_STARTED ||
	    stat == START_STATUS_STARTING) {
		ret |= STREAM__ZSL_OUTPUT_BIT;
		cnt++;
	}

	if (stream_cnt)
		*stream_cnt = cnt;
	return ret;
}
