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
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/types.h>
#include "isp_hw_reg.h"
#include "isp_debug.h"
#include "isp_utils.h"
#include "isp_hwa.h"

#define MACRO_2_STR(X) #X

#define MAX_ONE_TIME_LOG_INFO_LEN 510

#ifdef CONFIG_DEBUG_FS
u32 g_fw_log_enable;

static u8 g_fw_log_buf[ISP_FW_TRACE_BUF_SIZE + 32];

void isp_debugfs_create(struct amd_cam *cam)
{
	cam->debugfs_dir = debugfs_create_dir("amd_isp", NULL);
	debugfs_create_u32("fw_log_enable", 0644, cam->debugfs_dir, &g_fw_log_enable);
	debugfs_create_u32("drv_dpm_level", 0644, cam->debugfs_dir, &g_drv_dpm_level);
}

void isp_debugfs_remove(struct amd_cam *cam)
{
	debugfs_remove_recursive(cam->debugfs_dir);
	cam->debugfs_dir = NULL;
}

static u32 isp_fw_get_fw_rb_log(struct isp_context *isp, u8 *buf, u32 fw_log_en)
{
	u32 rd_ptr, wr_ptr;
	u32 total_cnt;
	u32 cnt;
	u8 *sys;
	u32 rb_size;
	u32 offset = 0;

	static int calculate_cpy_time;

	total_cnt = 0;
	cnt = 0;

	sys = isp->fw_log_buf;
	rb_size = isp->fw_log_buf_len;
	if (rb_size == 0)
		return 0;

	if (calculate_cpy_time) {
		long long bef;
		long long cur;
		u32 cal_cnt;

		calculate_cpy_time = 0;
		isp_get_cur_time_tick(&bef);
		for (cal_cnt = 0; cal_cnt < 10; cal_cnt++)
			memcpy(buf, (u8 *)(sys), ISP_FW_TRACE_BUF_SIZE);

		isp_get_cur_time_tick(&cur);
		cur -= bef;
		bef = (long long)((ISP_FW_TRACE_BUF_SIZE * 10) / 1024);
		cur = bef * 10000000 / cur;
		pr_err("%s: memcpy speed %llxK/S\n", __func__, cur);
	}

	rd_ptr = isp_hwa_rreg(isp, ISP_LOG_RB_RPTR0);
	wr_ptr = isp_hwa_rreg(isp, ISP_LOG_RB_WPTR0);

	do {
		if (wr_ptr > rd_ptr)
			cnt = wr_ptr - rd_ptr;
		else if (wr_ptr < rd_ptr)
			cnt = rb_size - rd_ptr;
		else
			return 0;

		if (cnt > rb_size) {
			pr_err("%s: fail fw log size %u\n", __func__, cnt);
			return 0;
		}

		if (fw_log_en)
			memcpy(buf + offset, (u8 *)(sys + rd_ptr), cnt);

		offset += cnt;
		total_cnt += cnt;
		rd_ptr = (rd_ptr + cnt) % rb_size;
	} while (rd_ptr < wr_ptr);

	isp_hwa_wreg(isp, ISP_LOG_RB_RPTR0, rd_ptr);

	return total_cnt;
}

void isp_fw_log_print(struct isp_context *isp)
{
	u32 cnt;
	u32 fw_log_en = g_fw_log_enable; /* read at the beginning */

	mutex_lock(&isp->command_mutex);
	cnt = isp_fw_get_fw_rb_log(isp, g_fw_log_buf, fw_log_en);
	mutex_unlock(&isp->command_mutex);

	if (cnt && fw_log_en) {
		char temp_ch;
		char *str;
		char *end;
		char *line_end;

		str = (char *)g_fw_log_buf;
		end = ((char *)g_fw_log_buf + cnt);
		g_fw_log_buf[cnt] = 0;

		while (str < end) {
			line_end = strchr(str, 0x0A);
			if ((line_end && (str + MAX_ONE_TIME_LOG_INFO_LEN) >= line_end) ||
			    (!line_end && (str + MAX_ONE_TIME_LOG_INFO_LEN) >= end)) {
				if (line_end)
					*line_end = 0;

				if (*str != '\0')
					pr_debug("%s", str);

				if (line_end) {
					*line_end = 0x0A;
					str = line_end + 1;
				} else {
					break;
				}
			} else {
				u32 tmp_len = MAX_ONE_TIME_LOG_INFO_LEN;

				temp_ch = str[tmp_len];
				str[tmp_len] = 0;
				pr_debug("%s", str);
				str[tmp_len] = temp_ch;
				str = &str[tmp_len];
			}
		}
	}
}
#endif

char *isp_dbg_get_buf_src_str(u32 src)
{
	switch (src) {
	case BUFFER_SOURCE_INVALID:
		return MACRO_2_STR(BUFFER_SOURCE_INVALID);
	case BUFFER_SOURCE_CMD_CAPTURE:
		return MACRO_2_STR(BUFFER_SOURCE_CMD_CAPTURE);
	case BUFFER_SOURCE_STREAM:
		return MACRO_2_STR(BUFFER_SOURCE_STREAM);
	case BUFFER_SOURCE_TEMP:
		return MACRO_2_STR(BUFFER_SOURCE_TEMP);
	case BUFFER_SOURCE_MAX:
		return MACRO_2_STR(BUFFER_SOURCE_MAX);
	default:
		return "Unknown output fmt";
	}
}

char *isp_dbg_get_buf_done_str(u32 status)
{
	switch (status) {
	case BUFFER_STATUS_INVALID:
		return MACRO_2_STR(BUFFER_STATUS_INVALID);
	case BUFFER_STATUS_SKIPPED:
		return MACRO_2_STR(BUFFER_STATUS_SKIPPED);
	case BUFFER_STATUS_EXIST:
		return MACRO_2_STR(BUFFER_STATUS_EXIST);
	case BUFFER_STATUS_DONE:
		return MACRO_2_STR(BUFFER_STATUS_DONE);
	case BUFFER_STATUS_LACK:
		return MACRO_2_STR(BUFFER_STATUS_LACK);
	case BUFFER_STATUS_DIRTY:
		return MACRO_2_STR(BUFFER_STATUS_DIRTY);
	case BUFFER_STATUS_MAX:
		return MACRO_2_STR(BUFFER_STATUS_MAX);
	default:
		return "Unknown Buf Done Status";
	}
};

char *isp_dbg_get_img_fmt_str(void *in /* enum image_format_t * */)
{
	enum image_format_t *t;

	t = (enum image_format_t *)in;

	switch (*t) {
	case IMAGE_FORMAT_INVALID:
		return "INVALID";
	case IMAGE_FORMAT_NV12:
		return "NV12";
	case IMAGE_FORMAT_NV21:
		return "NV21";
	case IMAGE_FORMAT_I420:
		return "I420";
	case IMAGE_FORMAT_YV12:
		return "YV12";
	case IMAGE_FORMAT_YUV422PLANAR:
		return "YUV422P";
	case IMAGE_FORMAT_YUV422SEMIPLANAR:
		return "YUV422SEMIPLANAR";
	case IMAGE_FORMAT_YUV422INTERLEAVED:
		return "YUV422INTERLEAVED";
	case IMAGE_FORMAT_RGBBAYER8:
		return "RGBBAYER8";
	case IMAGE_FORMAT_RGBBAYER10:
		return "RGBBAYER10";
	case IMAGE_FORMAT_RGBBAYER12:
		return "RGBBAYER12";
	case IMAGE_FORMAT_RGBIR8:
		return "RGBIR8";
	case IMAGE_FORMAT_RGBIR10:
		return "RGBIR10";
	case IMAGE_FORMAT_RGBIR12:
		return "RGBIR12";
	default:
		return "Unknown";
	}
}

void isp_dbg_show_bufmeta_info(char *pre, u32 cid, void *in, void *orig_buf)
{
	struct buffer_meta_info_t *p;
	struct sys_img_buf_info *orig;

	if (!in)
		return;
	if (!pre)
		pre = "";
	p = (struct buffer_meta_info_t *)in;
	orig = (struct sys_img_buf_info *)orig_buf;

	pr_debug("%s(%s)%u en:%d,stat:%s(%u),src:%s\n", pre,
		 isp_dbg_get_img_fmt_str(&p->image_prop.image_format), cid,
		 p->enabled, isp_dbg_get_buf_done_str(p->status), p->status,
		 isp_dbg_get_buf_src_str(p->source));

	pr_debug("%p,0x%llx(%u) %p,0x%llx(%u) %p,0x%llx(%u)\n",
		 orig->planes[0].sys_addr, orig->planes[0].mc_addr,
		 orig->planes[0].len, orig->planes[1].sys_addr,
		 orig->planes[1].mc_addr, orig->planes[1].len,
		 orig->planes[2].sys_addr, orig->planes[2].mc_addr,
		 orig->planes[2].len);
}

char *isp_dbg_get_out_fmt_str(int fmt /* enum enum _image_format_t */)
{
	switch (fmt) {
	case IMAGE_FORMAT_INVALID:
		return MACRO_2_STR(IMAGE_FORMAT_INVALID);
	case IMAGE_FORMAT_NV12:
		return MACRO_2_STR(IMAGE_FORMAT_NV12);
	case IMAGE_FORMAT_NV21:
		return MACRO_2_STR(IMAGE_FORMAT_NV21);
	case IMAGE_FORMAT_I420:
		return MACRO_2_STR(IMAGE_FORMAT_I420);
	case IMAGE_FORMAT_YV12:
		return MACRO_2_STR(IMAGE_FORMAT_YV12);
	case IMAGE_FORMAT_YUV422PLANAR:
		return MACRO_2_STR(IMAGE_FORMAT_YUV422PLANAR);
	case IMAGE_FORMAT_YUV422SEMIPLANAR:
		return MACRO_2_STR(IMAGE_FORMAT_YUV422SEMIPLANAR);
	case IMAGE_FORMAT_YUV422INTERLEAVED:
		return MACRO_2_STR(IMAGE_FORMAT_YUV422INTERLEAVED);
	case IMAGE_FORMAT_RGBBAYER8:
		return MACRO_2_STR(IMAGE_FORMAT_RGBBAYER8);
	case IMAGE_FORMAT_RGBBAYER10:
		return MACRO_2_STR(IMAGE_FORMAT_RGBBAYER10);
	case IMAGE_FORMAT_RGBBAYER12:
		return MACRO_2_STR(IMAGE_FORMAT_RGBBAYER12);
	case IMAGE_FORMAT_RGBIR8:
		return MACRO_2_STR(IMAGE_FORMAT_RGBIR8);
	case IMAGE_FORMAT_RGBIR10:
		return MACRO_2_STR(IMAGE_FORMAT_RGBIR10);
	case IMAGE_FORMAT_RGBIR12:
		return MACRO_2_STR(IMAGE_FORMAT_RGBIR12);
	default:
		return "Unknown output fmt";
	};
}

char *isp_dbg_get_buf_type(u32 type)
{
	/* enum buffer_type_t */
	switch (type) {
	case BUFFER_TYPE_RAW:
		return MACRO_2_STR(BUFFER_TYPE_RAW);
	case BUFFER_TYPE_MIPI_RAW:
		return MACRO_2_STR(BUFFER_TYPE_MIPI_RAW);
	case BUFFER_TYPE_RAW_TEMP:
		return MACRO_2_STR(BUFFER_TYPE_RAW_TEMP);
	case BUFFER_TYPE_EMB_DATA:
		return MACRO_2_STR(BUFFER_TYPE_EMB_DATA);
	case BUFFER_TYPE_PD_DATA:
		return MACRO_2_STR(BUFFER_TYPE_PD_DATA);
	case BUFFER_TYPE_STILL:
		return MACRO_2_STR(BUFFER_TYPE_STILL);
	case BUFFER_TYPE_PREVIEW:
		return MACRO_2_STR(BUFFER_TYPE_PREVIEW);
	case BUFFER_TYPE_VIDEO:
		return MACRO_2_STR(BUFFER_TYPE_VIDEO);
	case BUFFER_TYPE_META_INFO:
		return MACRO_2_STR(BUFFER_TYPE_META_INFO);
	case BUFFER_TYPE_META_DATA:
		return MACRO_2_STR(BUFFER_TYPE_META_DATA);
	case BUFFER_TYPE_FRAME_INFO:
		return MACRO_2_STR(BUFFER_TYPE_FRAME_INFO);
	case BUFFER_TYPE_MEM_POOL:
		return MACRO_2_STR(BUFFER_TYPE_MEM_POOL);
	case BUFFER_TYPE_SETFILE_DATA:
		return MACRO_2_STR(BUFFER_TYPE_SETFILE_DATA);
	case BUFFER_TYPE_TNR_REF:
		return MACRO_2_STR(BUFFER_TYPE_TNR_REF);
	case BUFFER_TYPE_CSTAT_DS:
		return MACRO_2_STR(BUFFER_TYPE_CSTAT_DS);
	case BUFFER_TYPE_LME_RDMA:
		return MACRO_2_STR(BUFFER_TYPE_LME_RDMA);
	case BUFFER_TYPE_LME_PREV_RDMA:
		return MACRO_2_STR(BUFFER_TYPE_LME_PREV_RDMA);
	case BUFFER_TYPE_LME_WDMA:
		return MACRO_2_STR(BUFFER_TYPE_LME_WDMA);
	case BUFFER_TYPE_LME_MV0:
		return MACRO_2_STR(BUFFER_TYPE_LME_MV0);
	case BUFFER_TYPE_LME_MV1:
		return MACRO_2_STR(BUFFER_TYPE_LME_MV1);
	case BUFFER_TYPE_LME_SAD:
		return MACRO_2_STR(BUFFER_TYPE_LME_SAD);
	case BUFFER_TYPE_BYRP_TAPOUT:
		return MACRO_2_STR(BUFFER_TYPE_BYRP_TAPOUT);
	case BUFFER_TYPE_RGBP_TAPOUT:
		return MACRO_2_STR(BUFFER_TYPE_RGBP_TAPOUT);
	case BUFFER_TYPE_YUVP_TAPOUT:
		return MACRO_2_STR(BUFFER_TYPE_YUVP_TAPOUT);
	case BUFFER_TYPE_EMUL_DATA:
		return MACRO_2_STR(BUFFER_TYPE_EMUL_DATA);
	default:
		return "Unknown type";
	}
}

char *isp_dbg_get_cmd_str(u32 cmd)
{
	switch (cmd) {
	case CMD_ID_GET_FW_VERSION:
		return MACRO_2_STR(CMD_ID_GET_FW_VERSION);
	case CMD_ID_SET_LOG_MODULE_LEVEL:
		return MACRO_2_STR(CMD_ID_SET_LOG_MODULE_LEVEL);
	case CMD_ID_START_STREAM:
		return MACRO_2_STR(CMD_ID_START_STREAM);
	case CMD_ID_STOP_STREAM:
		return MACRO_2_STR(CMD_ID_STOP_STREAM);
	case CMD_ID_SEND_BUFFER:
		return MACRO_2_STR(CMD_ID_SEND_BUFFER);
	case CMD_ID_SET_STREAM_CONFIG:
		return MACRO_2_STR(CMD_ID_SET_STREAM_CONFIG);
	case CMD_ID_SET_OUT_CHAN_PROP:
		return MACRO_2_STR(CMD_ID_SET_OUT_CHAN_PROP);
	case CMD_ID_SET_OUT_CHAN_FRAME_RATE_RATIO:
		return MACRO_2_STR(CMD_ID_SET_OUT_CHAN_FRAME_RATE_RATIO);
	case CMD_ID_ENABLE_OUT_CHAN:
		return MACRO_2_STR(CMD_ID_ENABLE_OUT_CHAN);
	case CMD_ID_SET_3A_ROI:
		return MACRO_2_STR(CMD_ID_SET_3A_ROI);
	case CMD_ID_ENABLE_PREFETCH:
		return MACRO_2_STR(CMD_ID_ENABLE_PREFETCH);
	default:
		return "Unknown cmd";
	};
}

char *isp_dbg_get_resp_str(u32 cmd)
{
	switch (cmd) {
	case RESP_ID_CMD_DONE:
		return MACRO_2_STR(RESP_ID_CMD_DONE);
	case RESP_ID_NOTI_FRAME_DONE:
		return MACRO_2_STR(RESP_ID_NOTI_FRAME_DONE);
		/* case RESP_ID_HEART_BEAT: */
		return MACRO_2_STR(RESP_ID_HEART_BEAT);
	default:
		return "Unknown respid";
	};
}

char *isp_dbg_get_pvt_fmt_str(int fmt /* enum pvt_img_fmt */)
{
	switch (fmt) {
	case PVT_IMG_FMT_INVALID:
		return "PVT_IMG_FMT_INVALID";
	case PVT_IMG_FMT_YV12:
		return "PVT_IMG_FMT_YV12";
	case PVT_IMG_FMT_I420:
		return "PVT_IMG_FMT_I420";
	case PVT_IMG_FMT_NV21:
		return "PVT_IMG_FMT_NV21";
	case PVT_IMG_FMT_NV12:
		return "PVT_IMG_FMT_NV12";
	case PVT_IMG_FMT_YUV422P:
		return "PVT_IMG_FMT_YUV422P";
	case PVT_IMG_FMT_YUV422_SEMIPLANAR:
		return "PVT_IMG_FMT_YUV422_SEMIPLANAR";
	case PVT_IMG_FMT_YUV422_INTERLEAVED:
		return "PVT_IMG_FMT_YUV422_INTERLEAVED";
	case PVT_IMG_FMT_L8:
		return "PVT_IMG_FMT_L8";
	default:
		return "Unknown PVT fmt";
	};
}

char *isp_dbg_get_stream_str(u32 stream /* enum fw_cmd_resp_stream_id */)
{
	switch (stream) {
	case FW_CMD_RESP_STREAM_ID_GLOBAL:
		return "STREAM_GLOBAL";
	case FW_CMD_RESP_STREAM_ID_1:
		return "STREAM1";
	case FW_CMD_RESP_STREAM_ID_2:
		return "STREAM2";
	case FW_CMD_RESP_STREAM_ID_3:
		return "STREAM3";
	default:
		return "Unkonow streamID";
	}
}

char *isp_dbg_get_para_str(u32 para /* enum para_id */)
{
	switch (para) {
	/* para value type is pvt_img_fmtdone */
	case PARA_ID_DATA_FORMAT:
		return MACRO_2_STR(PARA_ID_DATA_FORMAT);
	/* para value type is pvt_img_res_fps_pitchdone */
	case PARA_ID_DATA_RES_FPS_PITCH:
		return MACRO_2_STR(PARA_ID_DATA_RES_FPS_PITCH);
	default:
		return "Unknown paraId";
	};
}

char *isp_dbg_get_out_ch_str(int ch /* enum isp_pipe_out_ch_t */)
{
	switch ((enum isp_pipe_out_ch_t)ch) {
	case ISP_PIPE_OUT_CH_PREVIEW:
		return "prev";
	case ISP_PIPE_OUT_CH_VIDEO:
		return "video";
	case ISP_PIPE_OUT_CH_STILL:
		return "still";
	case ISP_PIPE_OUT_CH_CSTAT_DS_PREVIEW:
		return "DS_PREVIEW";
	case ISP_PIPE_OUT_CH_MIPI_RAW:
		return "raw";
	case ISP_PIPE_OUT_CH_BYRP_TAPOUT:
		return "BYRP";
	case ISP_PIPE_OUT_CH_RGBP_TAPOUT:
		return "RGBP";
	case ISP_PIPE_OUT_CH_YUVP_TAPOUT:
		return "YUVP";
	default:
		return "fail unknown channel";
	}
}
