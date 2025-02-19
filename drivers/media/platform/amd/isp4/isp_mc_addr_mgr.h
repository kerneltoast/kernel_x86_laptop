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

#ifndef ISP_MC_ADDR_MGR_H
#define ISP_MC_ADDR_MGR_H

void isp_fw_indirect_cmd_pl_buf_init(struct isp_context *isp,
				     struct isp_fw_work_buf_mgr *mgr,
				     u64 sys_addr,
				     u64 mc_addr,
				     u32 len);
void isp_fw_indirect_cmd_pl_buf_uninit(struct isp_fw_work_buf_mgr *mgr);
s32 isp_fw_get_nxt_indirect_cmd_pl(struct isp_context *isp,
				   struct isp_fw_work_buf_mgr *mgr,
				   u64 *sys_addr,
				   u64 *mc_addr, u32 *len);
s32 isp_fw_ret_indirect_cmd_pl(struct isp_context *isp,
			       struct isp_fw_work_buf_mgr *mgr,
			       u64 mc_addr);
void isp_fw_buf_get_cmd_base(struct isp_context *isp,
			     enum fw_cmd_resp_stream_id id,
			     u64 *sys_addr, u64 *mc_addr, u32 *len);
void isp_fw_buf_get_resp_base(struct isp_context *isp,
			      enum fw_cmd_resp_stream_id id,
			      u64 *sys_addr, u64 *mc_addr, u32 *len);
#endif
