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
#ifndef ISP_DEBUG_H
#define ISP_DEBUG_H

#include "isp_common.h"
#include "isp_common.h"
#include <linux/printk.h>
#include <linux/dev_printk.h>

#ifdef CONFIG_DEBUG_FS
extern u32 g_drv_dpm_level;
extern u32 g_fw_log_enable;

void isp_debugfs_create(struct amd_cam *cam);
void isp_debugfs_remove(struct amd_cam *cam);
void isp_fw_log_print(struct isp_context *isp);

#else

#define isp_debugfs_create(cam)
#define isp_debugfs_remove(cam)
#define isp_fw_log_print(isp)

#endif /* CONFIG_DEBUG_FS */

void isp_dbg_show_map_info(void *p);
void isp_dbg_show_bufmeta_info(char *pre, u32 cid, void *p,
			       void *orig_buf /* struct sys_img_buf_handle* */);
void isp_dbg_show_img_prop(char *pre,
			   void *p /* struct _image_prop_t * */);
char *isp_dbg_get_isp_status_str(u32 status);
char *isp_dbg_get_img_fmt_str(void *in /* enum _image_format_t * */);
char *isp_dbg_get_pvt_fmt_str(int fmt /* enum pvt_img_fmt */);
char *isp_dbg_get_out_ch_str(int ch /* enum _isp_pipe_out_ch_t */);
char *isp_dbg_get_out_fmt_str(int fmt /* enum enum _image_format_t */);
char *isp_dbg_get_cmd_str(u32 cmd);
char *isp_dbg_get_buf_type(u32 type);/* enum _buffer_type_t */
char *isp_dbg_get_resp_str(u32 resp);
char *isp_dbg_get_buf_src_str(u32 src);
char *isp_dbg_get_buf_done_str(u32 status);
char *isp_dbg_get_stream_str(u32 stream);
char *isp_dbg_get_para_str(u32 para /* enum para_id */);
char *isp_dbg_get_reg_name(u32 reg);

#endif
