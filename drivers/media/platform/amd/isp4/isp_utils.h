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

#ifndef ISP_UTILS_H
#define ISP_UTILS_H

#include "isp_common.h"

static inline bool is_failure(s32 ret)
{
	if (ret)
		return true;
	else
		return false;
}

static inline bool is_para_legal(void *context, enum camera_port_id cam_id)
{
	if (!context) {
		pr_err("invalid context");
		return false;
	}

	switch (cam_id) {
	case CAMERA_PORT_0:
	case CAMERA_PORT_1:
	case CAMERA_PORT_2:
		return true;
	default:
		pr_err("invalid cam_id:%d", cam_id);
		return false;
	}
}

enum camera_port_id get_actual_cid(void *context, enum camera_port_id cid);
bool get_available_fw_cmdresp_stream_id(void *context, enum camera_port_id cam_id);
void reset_fw_cmdresp_strinfo(void *context,
			      enum fw_cmd_resp_stream_id fw_stream_id);
u32 isp_get_started_stream_count(struct isp_context *isp);
enum fw_cmd_resp_stream_id isp_get_fwresp_stream_id(struct isp_context *isp,
						    enum camera_port_id cid,
						    enum stream_id stream_id);
u32 isp_get_stream_output_bits(struct isp_context *isp,
			       enum camera_port_id,
			       u32 *stream_cnt);
struct isp_cmd_element *isp_rm_cmd_from_cmdq_by_stream(struct isp_context *isp,
						       enum fw_cmd_resp_stream_id fw_stream_id,
						       int signal_evt);

#endif
