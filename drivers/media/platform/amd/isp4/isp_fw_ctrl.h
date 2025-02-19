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

#ifndef _ISP_FW_CTRL_
#define _ISP_FW_CTRL_

#include "isp_common.h"

/* ISP FW cmd control */

bool no_fw_cmd_ringbuf_slot(struct isp_context *isp,
			    enum fw_cmd_resp_stream_id cmd_buf_idx);

int fw_if_send_img_buf(struct isp_context *isp,
		       struct isp_mapped_buf_info *buffer,
		       enum camera_port_id cam_id, enum stream_id stream_id);

struct isp_cmd_element *isp_append_cmd_2_cmdq(struct isp_context *isp,
					      struct isp_cmd_element *command);

struct isp_cmd_element *isp_rm_cmd_from_cmdq(struct isp_context *isp,
					     u32 seq_num, u32 cmd_id,
					     int signal_evt);

int insert_isp_fw_cmd(struct isp_context *isp,
		      enum fw_cmd_resp_stream_id cmd_buf_idx,
		      struct cmd_t *cmd);

int isp_get_f2h_resp(struct isp_context *isp,
		     enum fw_cmd_resp_stream_id stream,
		     struct resp_t *response);

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
		       u32 *resp_pl_len);

int isp_send_fw_cmd(struct isp_context *isp,
		    u32 cmd_id,
		    enum fw_cmd_resp_stream_id stream,
		    enum fw_cmd_para_type directcmd,
		    void *package,
		    u32 package_size);

int isp_send_fw_cmd_sync(struct isp_context *isp,
			 u32 cmd_id,
			 enum fw_cmd_resp_stream_id stream,
			 enum fw_cmd_para_type directcmd,
			 void *package,
			 u32 package_size,
			 u32 timeout /* in ms */,
			 void *resp_pl,
			 u32 *resp_pl_len);

/* ISP FW boot control */

void isp_boot_disable_ccpu(struct isp_context *isp);
void isp_boot_enable_ccpu(struct isp_context *isp);
int isp_boot_fw_init(struct isp_context *isp);
int isp_boot_cmd_resp_rb_init(struct isp_context *isp);
int isp_boot_wait_fw_ready(struct isp_context *isp, u32 isp_status_addr);
int isp_boot_isp_fw_boot(struct isp_context *isp);

/* ISP FW thread process */

s32 isp_fw_resp_thread_wrapper(void *context);
s32 isp_start_resp_proc_threads(struct isp_context *isp);
s32 isp_stop_resp_proc_threads(struct isp_context *isp);
void wake_up_resp_thread(struct isp_context *isp, u32 index);

#endif /* _ISP_FW_CTRL_ */
