/* SPDX-License-Identifier: MIT */
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

#ifndef ISP_COMMON_H
#define ISP_COMMON_H

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/videodev2.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/types.h>
#include <linux/debugfs.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-vmalloc.h>
#include <media/v4l2-device.h>
#include "isp_hw_reg.h"
#include "isp_param.h"
#include "isp_module_intf.h"
#include "isp_buf_mgr.h"
#include "isp_cmd_resp.h"
#include "isp_queue.h"
#include "isp_pwr.h"
#include "isp_events.h"

#define ISP4_VDEV_NUM		1
#define ISP4_VDEV_PREVIEW	0

#define DRI_VERSION_MAJOR_SHIFT          (24)
#define DRI_VERSION_MINOR_SHIFT          (16)
#define DRI_VERSION_REVISION_SHIFT       (8)
#define DRI_VERSION_SUB_REVISION_SHIFT   (0)

#define DRI_VERSION_MAJOR_MASK           (0xff << DRI_VERSION_MAJOR_SHIFT)
#define DRI_VERSION_MINOR_MASK           (0xff << DRI_VERSION_MINOR_SHIFT)
#define DRI_VERSION_REVISION_MASK        (0xff << DRI_VERSION_REVISION_SHIFT)
#define DRI_VERSION_SUB_REVISION_MASK   (0xff << DRI_VERSION_SUB_REVISION_SHIFT)

#define DRI_VERSION_MAJOR          (0x4)
#define DRI_VERSION_MINOR          (0x0)
#define DRI_VERSION_REVISION       (0x1)
#define DRI_VERSION_SUB_REVISION   (0x0)
#define DRI_VERSION_STRING "ISP Driver Version: 4.0.1.0"
#define DRI_VERSION (((DRI_VERSION_MAJOR & 0xff) << DRI_VERSION_MAJOR_SHIFT) |\
	((DRI_VERSION_MINOR & 0xff) << DRI_VERSION_MINOR_SHIFT) |\
	((DRI_VERSION_REVISION & 0xff) << DRI_VERSION_REVISION_SHIFT) |\
	((DRI_VERSION_SUB_REVISION & 0xff) << DRI_VERSION_SUB_REVISION_SHIFT))

#define MAX_HW_NUM		10
#define FW_STREAM_TYPE_NUM	7
#define MAX_REQUEST_DEPTH	10
#define NORMAL_YUV_STREAM_CNT	3
#define MAX_KERN_METADATA_BUF_SIZE 56320 /* 55kb */
/* 2^16(Format 16.16=>16 bits for integer part and 16 bits for fractional part) */
#define POINT_TO_FLOAT	65536
#define POINT_TO_DOUBLE	4294967296 /* 2^32(Format 32.32 ) */
#define STEP_NUMERATOR		1
#define STEP_DENOMINATOR	3
#define SIZE_ALIGN		8
#define SIZE_ALIGN_DOWN(size) \
	((unsigned int)(SIZE_ALIGN) * \
	((unsigned int)(size) / (unsigned int)(SIZE_ALIGN)))

#define ISP_LOGRB_SIZE (2 * 1024 * 1024)
#define RB_MAX (25)
#define RESP_CHAN_TO_RB_OFFSET (9)
#define RB_PMBMAP_MEM_SIZE (16 * 1024 * 1024 - 1)
#define RB_PMBMAP_MEM_CHUNK (RB_PMBMAP_MEM_SIZE / (RB_MAX - 1))

#define ISP_ADDR_ALIGN_UP(addr, addr_align) ALIGN((addr), (addr_align))
#define ISP_SIZE_ALIGN_UP(size, size_align) ALIGN((addr), (size_align))

#define ISP_ALIGN_SIZE_1K (0x400)
#define ISP_ALIGN_SIZE_4K (0x1000)
#define ISP_ALIGN_SIZE_32K (0x8000)
#define ISP_BUFF_PADDING_64K (0x10000)

#define ISP_ADDR_ALIGN_UP_1K(addr) (ISP_ADDR_ALIGN_UP(addr, ISP_ALIGN_SIZE_1K))
#define ISP_ADDR_ALIGN_UP_4K(size) (ISP_ADDR_ALIGN_UP(size, ISP_ALIGN_SIZE_4K))
#define ISP_SIZE_ALIGN_UP_32K(size) \
	(ISP_ADDR_ALIGN_UP(size, ISP_ALIGN_SIZE_32K))

/* fw binary, stack, heap, etc */
#define ISP_RESV_FB_SIZE_DEFAULT (2 * 1024 * 1024)

#define ISP_FW_WORK_BUF_SIZE (12 * 1024 * 1024)
#define CMD_RESPONSE_BUF_SIZE (64 * 1024)
#define MAX_CMD_RESPONSE_BUF_SIZE (4 * 1024)
#define MIN_CHANNEL_BUF_CNT_BEFORE_START_STREAM 4

/*
 * command single buffer is to save small data for some indirect commands
 * Max single buffer is 4K for the current commands with single buffer.
 */
#define MAX_SINGLE_BUF_SIZE (4 * 1024)

#define INDIRECT_BUF_SIZE (12 * 1024)
#define INDIRECT_BUF_CNT 100
#define BRSZ_FULL_RAW_TMP_BUF_SIZE \
	(MAX_REAR_SENSOR_WIDTH * MAX_REAR_SENSOR_HEIGHT * 2)
#define BRSZ_FULL_RAW_TMP_BUF_CNT 4

#define META_INFO_BUF_SIZE ISP_SIZE_ALIGN_UP_32K(sizeof(struct meta_info_t))
#define META_INFO_BUF_CNT 4
#define META_DATA_BUF_SIZE (128 * 1024)

/*
 * The SEND_FW_CMD_TIMEOUT is used in tuning tool when sending FW command.
 * Some FW commands like dump engineer data needs 4 frames and during
 * development phase we sometimes enabled very low fps around 1, so the time
 * is about 4S, it'll be 5S by adding some redundancy
 */
#define SEND_FW_CMD_TIMEOUT (1000 * 5) /* in MS */

#define SKIP_FRAME_COUNT_AT_START 0

#define ISP_MC_ADDR_ALIGN (1024 * 32)
#define ISP_MC_PREFETCH_GAP (1024 * 32)

#define BUF_NUM_BEFORE_START_CMD 2
#define BUFFER_ALIGN_SIZE (0x400)

#define MAX_HOST2FW_SEQ_NUM (16 * 1024)
#define HOST2FW_COMMAND_SIZE (sizeof(struct cmd_t))
#define FW2HOST_RESPONSE_SIZE (sizeof(struct resp_t))

#define MAX_NUM_HOST2FW_COMMAND (40)
#define MAX_NUM_FW2HOST_RESPONSE (1000)

#define ISP_FW_CODE_BUF_SIZE (2 * 1024 * 1024)
#define ISP_FW_STACK_BUF_SIZE (8 * 64 * 1024)
#define ISP_FW_HEAP_BUF_SIZE (11 * 1024 * 1024 / 2)
#define ISP_FW_TRACE_BUF_SIZE ISP_LOGRB_SIZE
#define ISP_FW_CMD_BUF_SIZE (MAX_NUM_HOST2FW_COMMAND * HOST2FW_COMMAND_SIZE)
#define ISP_FW_CMD_BUF_COUNT 4
#define ISP_FW_RESP_BUF_SIZE (MAX_NUM_FW2HOST_RESPONSE * FW2HOST_RESPONSE_SIZE)
#define ISP_FW_RESP_BUF_COUNT 4

#define ISP_FW_CMD_PAY_LOAD_BUF_SIZE                     \
	(ISP_FW_WORK_BUF_SIZE -                          \
	 (ISP_FW_CODE_BUF_SIZE + ISP_FW_STACK_BUF_SIZE + \
	  ISP_FW_HEAP_BUF_SIZE + ISP_FW_TRACE_BUF_SIZE + \
	  ISP_FW_CMD_BUF_SIZE * ISP_FW_CMD_BUF_COUNT +   \
	  ISP_FW_RESP_BUF_SIZE * ISP_FW_RESP_BUF_COUNT))

#define ISP_FW_CMD_PAY_LOAD_BUF_ALIGN 64

#define STREAM_META_BUF_COUNT 6

#define MAX_REAL_FW_RESP_STREAM_NUM 4
#define MAX_AF_ROI_NUM 3
#define MAX_PHOTO_SEQUENCE_FPS 30
#define MAX_PHOTO_SEQUENCE_FRAME_RATE MAX_PHOTO_SEQUENCE_FPS
#define MAX_MODE_TYPE_STR_LEN 16
#define MAX_SLEEP_COUNT (10)
#define MAX_SLEEP_TIME (100)

#define ISP_GET_STATUS(isp) ((isp)->isp_status)
#define ISP_SET_STATUS(isp, state)                                      \
	{                                                               \
		struct isp_context *c = (isp);				\
		enum isp_status s = (state);                            \
									\
		c->isp_status = s;					\
		if (s == ISP_STATUS_FW_RUNNING)				\
			isp_get_cur_time_tick(                          \
				&c->isp_pu_isp.idle_start_time);	\
		else                                                    \
			c->isp_pu_isp.idle_start_time = MAX_ISP_TIME_TICK;\
	}

#define MAX_REG_DUMP_SIZE (64 * 1024) /* 64KB for each subIp register dump */

#define ISP_SEMAPHORE_ID_X86 0x0100
#define ISP_SEMAPHORE_ATTEMPTS 15
#define ISP_SEMAPHORE_DELAY 10 /* ms */

#define MAX_ISP_TIME_TICK 0x7fffffffffffffff
#define NANOSECONDS 10000000

enum sensor_idx {
	CAM_IDX_BACK = 0,
	CAM_IDX_FRONT_L = 1,
	CAM_IDX_FRONT_R = 2,
	CAM_IDX_MAX = 3,
};

enum fw_cmd_resp_stream_id {
	FW_CMD_RESP_STREAM_ID_GLOBAL = 0,
	FW_CMD_RESP_STREAM_ID_1 = 1,
	FW_CMD_RESP_STREAM_ID_2 = 2,
	FW_CMD_RESP_STREAM_ID_3 = 3,
	FW_CMD_RESP_STREAM_ID_MAX = 4
};

enum fw_cmd_para_type {
	FW_CMD_PARA_TYPE_INDIRECT = 0,
	FW_CMD_PARA_TYPE_DIRECT = 1
};

enum list_type_id {
	LIST_TYPE_ID_FREE = 0,
	LIST_TYPE_ID_IN_FW = 1,
	LIST_TYPE_ID_MAX = 2
};

enum isp_status {
	ISP_STATUS_UNINITED,
	ISP_STATUS_INITED,
	ISP_STATUS_PWR_OFF = ISP_STATUS_INITED,
	ISP_STATUS_PWR_ON,
	ISP_STATUS_FW_RUNNING,
	ISP_FSM_STATUS_MAX
};

enum start_status {
	START_STATUS_NOT_START,
	START_STATUS_STARTING,
	START_STATUS_STARTED,
	START_STATUS_START_FAIL,
	START_STATUS_START_STOPPING
};

enum isp_aspect_ratio {
	ISP_ASPECT_RATIO_16_9, /* 16:9 */
	ISP_ASPECT_RATIO_16_10, /* 16:10 */
	ISP_ASPECT_RATIO_4_3, /* 4:3 */
};

#define STREAM__PREVIEW_OUTPUT_BIT BIT(STREAM_ID_PREVIEW)
#define STREAM__VIDEO_OUTPUT_BIT BIT(STREAM_ID_VIDEO)
#define STREAM__ZSL_OUTPUT_BIT BIT(STREAM_ID_ZSL)

struct isp_gpu_mem_info {
	u32	mem_domain;
	u64	mem_size;
	u32	mem_align;
	u64	gpu_mc_addr;
	void	*sys_addr;
	void	*mem_handle;
};

struct isp_mc_addr_node {
	u64 start_addr;
	u64 align_addr;
	u64 end_addr;
	u64 size;
	struct isp_mc_addr_node *next;
	struct isp_mc_addr_node *prev;
};

struct isp_mc_addr_mgr {
	struct isp_mc_addr_node head;
	struct mutex mutex; /* mutex */
	u64 start;
	u64 len;
};

struct sys_to_mc_map_info {
	u64 sys_addr;
	u64 mc_addr;
	u32 len;
};

struct isp_mapped_buf_info {
	struct list_node node;
	u8 camera_port_id;
	u8 stream_id;
	struct sys_img_buf_info *sys_img_buf_hdl;
	u64 multi_map_start_mc;
	struct sys_to_mc_map_info y_map_info;
	struct sys_to_mc_map_info u_map_info;
	struct sys_to_mc_map_info v_map_info;
	void *map_hdl;
	void *cos_mem_handle;
	void *mdl_for_map;
	struct isp_gpu_mem_info *map_sys_to_fb_gpu_info;
};

struct isp_stream_info {
	enum pvt_img_fmt format;
	u32 width;
	u32 height;
	u32 fps;
	u32 luma_pitch_set;
	u32 chroma_pitch_set;
	u32 max_fps_numerator;
	u32 max_fps_denominator;
	struct isp_list buf_free;
	struct isp_list buf_in_fw;
	enum start_status start_status;
	u8 running;
	u8 buf_num_sent;
};

struct roi_info {
	u32 h_offset;
	u32 v_offset;
	u32 h_size;
	u32 v_size;
};

struct isp_cos_sys_mem_info {
	u64 mem_size;
	void *sys_addr;
	void *mem_handle;
};

struct isp_sensor_info {
	enum camera_port_id cid;
	enum camera_port_id actual_cid;
	enum fw_cmd_resp_stream_id fw_stream_id;
	u64 meta_mc[STREAM_META_BUF_COUNT];
	enum start_status status;
	struct roi_info ae_roi;
	struct roi_info af_roi[MAX_AF_ROI_NUM];
	struct roi_info awb_region;
	u32 raw_width;
	u32 raw_height;
	struct isp_stream_info str_info[STREAM_ID_NUM + 1];

	u32 zsl_ret_width;
	u32 zsl_ret_height;
	u32 zsl_ret_stride;
	u32 open_flag;
	enum camera_type cam_type;
	enum camera_type cam_type_prev;
	enum fw_cmd_resp_stream_id stream_id;
	u8 zsl_enable;
	u8 resend_zsl_enable;
	char cur_res_fps_id;
	char sensor_opened;
	char hdr_enable;
	char tnr_enable;
	char start_str_cmd_sent;
	char channel_buf_sent_cnt;
	u32 poc;
};

#define I2C_REGADDR_NULL 0xffff

struct isp_cmd_element {
	u32 seq_num;
	u32 cmd_id;
	enum fw_cmd_resp_stream_id stream;
	u64 mc_addr;
	s64 send_time;
	struct isp_event *evt;
	struct isp_gpu_mem_info *gpu_pkg;
	void *resp_payload;
	u32 *resp_payload_len;
	u16 i2c_reg_addr;
	enum camera_port_id cam_id;
	struct isp_cmd_element *next;
};

enum isp_pipe_used_status {
	ISP_PIPE_STATUS_USED_BY_NONE,
	ISP_PIPE_STATUS_USED_BY_CAM_R = (CAMERA_PORT_0 + 1),
	ISP_PIPE_STATUS_USED_BY_CAM_FL = (CAMERA_PORT_1 + 1),
	ISP_PIPE_STATUS_USED_BY_CAM_FR = (CAMERA_PORT_2 + 1)
};

enum isp_config_mode {
	ISP_CONFIG_MODE_INVALID,
	ISP_CONFIG_MODE_PREVIEW,
	ISP_CONFIG_MODE_RAW,
	ISP_CONFIG_MODE_VIDEO2D,
	ISP_CONFIG_MODE_VIDEO3D,
	ISP_CONFIG_MODE_VIDEOSIMU,
	ISP_CONFIG_MODE_DATAT_TRANSFER,
	ISP_CONFIG_MODE_MAX
};

enum isp_bayer_pattern {
	ISP_BAYER_PATTERN_INVALID,
	ISP_BAYER_PATTERN_RGRGGBGB,
	ISP_BAYER_PATTERN_GRGRBGBG,
	ISP_BAYER_PATTERN_GBGBRGRG,
	ISP_BAYER_PATTERN_BGBGGRGR,
	ISP_BAYER_PATTERN_MAX
};

enum fw_cmd_resp_str_status {
	FW_CMD_RESP_STR_STATUS_IDLE = 0,
	FW_CMD_RESP_STR_STATUS_OCCUPIED,
	FW_CMD_RESP_STR_STATUS_INITIALED,
};

struct fw_cmd_resp_str_info {
	enum fw_cmd_resp_str_status status;
	enum camera_port_id cid_owner;
	struct isp_gpu_mem_info *meta_info_buf[STREAM_META_BUF_COUNT];
	struct isp_gpu_mem_info *meta_data_buf[STREAM_META_BUF_COUNT];
	struct isp_gpu_mem_info *cmd_resp_buf;
};

struct isp_fw_cmd_pay_load_buf {
	u64 sys_addr;
	u64 mc_addr;
	struct isp_fw_cmd_pay_load_buf *next;
};

struct isp_fw_work_buf_mgr {
	u64 sys_base;
	u64 mc_base;
	u32 pay_load_pkg_size;
	u32 pay_load_num;
	struct mutex mutex; /* mutex */
	struct isp_fw_cmd_pay_load_buf *free_cmd_pl_list;
	struct isp_fw_cmd_pay_load_buf *used_cmd_pl_list;
};

struct thread_handler {
	struct isp_event wakeup_evt;
	struct task_struct *thread;
	struct mutex mutex; /* mutex */
	wait_queue_head_t waitq;
};

struct isphwip_version_info {
	u32 major;
	u32 minor;
	u32 revision;
	u32 variant;
};

struct isp_context {
	enum isp_status isp_status;
	struct mutex ops_mutex; /* ops_mutex */

	struct isp_pwr_unit isp_pu_isp;
	struct isp_pwr_unit isp_pu_dphy;
	struct isp_pwr_unit isp_pu_cam[CAMERA_PORT_MAX];
	u32 isp_fw_ver;

	struct isp_fw_work_buf_mgr fw_indirect_cmd_pl_buf_mgr;
	struct isp_gpu_mem_info fb_buf;
	struct isp_gpu_mem_info nfb_buf;

	struct fw_cmd_resp_str_info
		fw_cmd_resp_strs_info[FW_CMD_RESP_STREAM_ID_MAX];

	u64 fw_cmd_buf_sys[ISP_FW_CMD_BUF_COUNT];
	u64 fw_cmd_buf_mc[ISP_FW_CMD_BUF_COUNT];
	u32 fw_cmd_buf_size[ISP_FW_CMD_BUF_COUNT];
	u64 fw_resp_buf_sys[ISP_FW_RESP_BUF_COUNT];
	u64 fw_resp_buf_mc[ISP_FW_RESP_BUF_COUNT];
	u32 fw_resp_buf_size[ISP_FW_RESP_BUF_COUNT];
	u64 fw_log_sys;
	u64 fw_log_mc;
	u32 fw_log_size;

	struct isp_cmd_element *cmd_q;
	struct mutex cmd_q_mtx; /* cmd_q_mtx */

	u32 sensor_count;
	struct thread_handler fw_resp_thread[MAX_REAL_FW_RESP_STREAM_NUM];
	u64 irq_enable_id[MAX_REAL_FW_RESP_STREAM_NUM];

	struct mutex command_mutex; /* mutex to command */
	struct mutex response_mutex; /* mutex to retrieve response */
	struct mutex isp_semaphore_mutex; /* mutex to access isp semaphore */
	u32 isp_semaphore_acq_cnt; /* how many times the isp semaphore is acquired */

	u32 host2fw_seq_num;

	u32 reg_value;
	u32 fw2host_response_result;
	u32 fw2host_sync_response_payload[40];

	func_isp_module_cb evt_cb[CAMERA_PORT_MAX];
	void *evt_cb_context[CAMERA_PORT_MAX];
	void *fw_data;
	u32 fw_len;
	u32 sclk; /* In MHZ */
	u32 iclk; /* In MHZ */
	u32 xclk; /* In MHZ */
	u32 refclk; /* In MHZ */
	u32 fw_ctrl_3a;
	u32 clk_info_set_2_fw;
	u32 snr_info_set_2_fw[CAMERA_PORT_MAX];
	u32 req_fw_load_suc;
	struct mutex map_unmap_mutex; /* mutex for unmap */
	struct isp_sensor_info sensor_info[CAMERA_PORT_MAX];
	struct isphwip_version_info isphw_info;

	/* buffer to include code, stack, heap, bss, dmamem, log info */
	struct isp_gpu_mem_info *fw_running_buf;
	struct isp_gpu_mem_info *fw_cmd_resp_buf;
	struct isp_gpu_mem_info *indirect_cmd_payload_buf;
	u8 *fw_log_buf;
	u32 fw_log_buf_len;
	u32 prev_buf_cnt_sent;
	struct isp_gpu_mem_info *fw_mem_pool[CAMERA_PORT_MAX];
	u64 timestamp_fw_base;
	u64 timestamp_sw_prev;
	s64 timestamp_sw_base;

	void *isp_power_cb_context;

	u32 fw_loaded; /* ISP FW is loaded */
	struct amd_cam *amd_cam;
};

struct isp_fw_resp_thread_para {
	u32 idx;
	struct isp_context *isp;
};

struct isp4_capture_buffer {
	/*
	 * struct vb2_v4l2_buffer must be the first element
	 * the videobuf2 framework will allocate this struct based on
	 * buf_struct_size and use the first sizeof(struct vb2_buffer) bytes of
	 * memory as a vb2_buffer
	 */
	struct vb2_v4l2_buffer vb2;
	struct list_head list;
};

struct isp4_video_dev {
	struct video_device vdev;
	struct media_pad vdev_pad;
	struct v4l2_pix_format format;

	/* mutex that protects vb2_queue */
	struct mutex vbq_lock;
	struct vb2_queue vbq;

	/*
	 * NOTE: in a real driver, a spin lock must be used to access the
	 * queue because the frames are generated from a hardware interruption
	 * and the isr is not allowed to sleep.
	 * Even if it is not necessary a spinlock in the vimc driver, we
	 * use it here as a code reference
	 */
	spinlock_t qlock;
	struct list_head buf_list;

	u32 sequence;
	u32 fw_run;
	struct task_struct *kthread;

	struct media_pipeline pipe;

	struct amd_cam *cam;
	struct v4l2_fract timeperframe;
};

struct amd_cam {
	struct isp4_video_dev isp_vdev[ISP4_VDEV_NUM];

	struct v4l2_subdev sdev;
	struct media_pad sdev_pad[ISP4_VDEV_NUM];

	struct v4l2_device v4l2_dev;
	struct media_device mdev;

	struct isp_context ispctx;

	void __iomem *isp_mmio;
	void __iomem *isp_phy_mmio;
	struct amdisp_platform_data *pltf_data;
	struct platform_device *pdev;

#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs_dir;
#endif
	struct v4l2_subdev *sensor;
	struct v4l2_async_notifier notifier;
};

static inline void isp_get_cur_time_tick(long long *ptimetick)
{
	if (ptimetick)
		*ptimetick = get_jiffies_64();
}

static inline int isp_is_timeout(long long *start, long long *end,
				 unsigned int timeout_ms)
{
	if (!start || !end || timeout_ms == 0)
		return 1;
	if ((*end - *start) * 1000 / HZ >= (long long)timeout_ms)
		return 1;
	else
		return 0;
}

enum stream_id get_vdev_stream_id(struct isp4_video_dev *vdev);

void isp_init_fw_rb_log_buffer(struct isp_context *isp_context,
			       u32 fw_rb_log_base_lo, u32 fw_rb_log_base_hi,
			       u32 fw_rb_log_size);

bool is_camera_started(struct isp_context *isp_context,
		       enum camera_port_id cam_id);

void isp_unmap_sys_2_mc(struct isp_context *isp,
			struct isp_mapped_buf_info *buff);

void isp_init_fw_ring_buf(struct isp_context *isp,
			  enum fw_cmd_resp_stream_id idx, u32 cmd);
void isp_get_cmd_buf_regs(struct isp_context *isp,
			  enum fw_cmd_resp_stream_id idx, u32 *rreg, u32 *wreg,
			  u32 *baselo_reg, u32 *basehi_reg, u32 *size_reg);
void isp_get_resp_buf_regs(struct isp_context *isp,
			   enum fw_cmd_resp_stream_id idx, u32 *rreg, u32 *wreg,
			   u32 *baselo_reg, u32 *basehi_reg, u32 *size_reg);

static inline void isp_split_addr64(u64 addr, u32 *lo, u32 *hi)
{
	if (lo)
		*lo = (u32)(addr & 0xffffffff);
	if (hi)
		*hi = (u32)(addr >> 32);
}

static inline u64 isp_join_addr64(u32 lo, u32 hi)
{
	return (((u64)hi) << 32) | (u64)lo;
}

static inline u32 isp_get_cmd_pl_size(void)
{
	return INDIRECT_BUF_SIZE;
}

static inline bool is_isp_poweron(struct isp_context *isp)
{
	if (isp->isp_pu_isp.pwr_status == ISP_PWR_UNIT_STATUS_ON)
		return true;
	else
		return false;
}

enum fw_cmd_resp_stream_id isp_get_fw_stream_id(struct isp_context *isp,
						enum camera_port_id cid);

void isp_fw_resp_func(struct isp_context *isp,
		      enum fw_cmd_resp_stream_id fw_stream_id);

void isp_fw_resp_cmd_done(struct isp_context *isp,
			  enum fw_cmd_resp_stream_id fw_stream_id,
			  struct resp_cmd_done *para);

void isp_fw_resp_cmd_done_extra(struct isp_context *isp,
				enum camera_port_id cid, struct resp_cmd_done *para,
				struct isp_cmd_element *ele);

void isp_fw_resp_cmd_skip_extra(struct isp_context *isp,
				enum camera_port_id cid, struct resp_cmd_done *para,
				struct isp_cmd_element *ele);

void isp_fw_resp_frame_done(struct isp_context *isp,
			    enum fw_cmd_resp_stream_id fw_stream_id,
			    struct resp_param_package_t *para);

void isp_clear_cmdq(struct isp_context *isp);
s32 isp_get_pipeline_id(struct isp_context *isp, enum camera_port_id cid);

bool isp_semaphore_acquire(struct isp_context *isp);
void isp_semaphore_release(struct isp_context *isp);
bool isp_semaphore_acquire_one_try(struct isp_context *isp);

#endif
