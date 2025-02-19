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
#ifndef CMD_RESP_PUB_H
#define CMD_RESP_PUB_H

#define FW_VERSION_IP_GEN_SHIFT (26)
#define FW_VERSION_SILICON_STAGE_SHIFT (24)
#define FW_VERSION_MAJOR_SHIFT (16)
#define FW_VERSION_MINOR_SHIFT (8)
#define FW_VERSION_BUILD_SHIFT (0)

#define FW_VERSION_IP_GEN_MASK (0x3f << FW_VERSION_IP_GEN_SHIFT)
#define FW_VERSION_SILICON_STAGE_MASK (0x03 << FW_VERSION_SILICON_STAGE_SHIFT)
#define FW_VERSION_MAJOR_MASK (0xff << FW_VERSION_MAJOR_SHIFT)
#define FW_VERSION_MINOR_MASK (0xff << FW_VERSION_MINOR_SHIFT)
#define FW_VERSION_BUILD_MASK (0xff << FW_VERSION_BUILD_SHIFT)

/* build ISP4.1 as default */
#ifndef CONFIG_ISPVER
#define CONFIG_ISPVER 41
#endif

#if (CONFIG_ISPVER == 41)

#define FW_VERSION_IP_GENERATION (0x4)
#ifdef CONFIG_ASIC_PLATFORM
#define FW_VERSION_SILICON_STAGE (0x1)
#define FW_VERSION_MAJOR (0x5)
#define FW_VERSION_MINOR (0x1)
#define FW_VERSION_BUILD (0x0)
#define FW_VERSION_STRING "ISP Firmware Version: 41.5.1.0"
#else
#define FW_VERSION_SILICON_STAGE (0x0)
#define FW_VERSION_MAJOR (0x12)
#define FW_VERSION_MINOR (0x1)
#define FW_VERSION_BUILD (0x0)
#define FW_VERSION_STRING "ISP Firmware Version: 40.18.1.0"
#endif

#elif (CONFIG_ISPVER == 42)

#define FW_VERSION_IP_GENERATION (0x5)
#ifdef CONFIG_ASIC_PLATFORM
#define FW_VERSION_SILICON_STAGE (0x1)
#define FW_VERSION_MAJOR (0x0)
#define FW_VERSION_MINOR (0x0)
#define FW_VERSION_BUILD (0x0)
#define FW_VERSION_STRING "ISP Firmware Version: 51.0.0.0"
#else
#define FW_VERSION_SILICON_STAGE (0x0)
#define FW_VERSION_MAJOR (0x0)
#define FW_VERSION_MINOR (0x2)
#define FW_VERSION_BUILD (0x0)
#define FW_VERSION_STRING "ISP Firmware Version: 50.0.2.0"
#endif

#else

#error "Not supported version"

#endif

#define FW_VERSION                                                       \
	(((FW_VERSION_IP_GENERATION << FW_VERSION_IP_GEN_SHIFT) &        \
	  FW_VERSION_IP_GEN_MASK) |                                      \
	 ((FW_VERSION_SILICON_STAGE << FW_VERSION_SILICON_STAGE_SHIFT) & \
	  FW_VERSION_SILICON_STAGE_MASK) |                               \
	 ((FW_VERSION_MAJOR << FW_VERSION_MAJOR_SHIFT) &                 \
	  FW_VERSION_MAJOR_MASK) |                                       \
	 ((FW_VERSION_MINOR << FW_VERSION_MINOR_SHIFT) &                 \
	  FW_VERSION_MINOR_MASK) |                                       \
	 ((FW_VERSION_BUILD << FW_VERSION_BUILD_SHIFT) &                 \
	  FW_VERSION_BUILD_MASK))
/*
 * @brief Host and Firmware command & response channel.
 *        Two types of command/response channel.
 *          Type Async Command has one command/response channel.
 *          Type Stream Commands has three command/response channel.
 *-----------                                        ------------
 *|         |       ---------------------------      |          |
 *|         |  ---->|     Async Command       |----> |          |
 *|         |       ---------------------------      |          |
 *|         |                                        |          |
 *|         |                                        |          |
 *|         |       ---------------------------      |          |
 *|         |  ---->|   Stream1 Sync Command  |----> |          |
 *|         |       ---------------------------      |          |
 *|         |                                        |          |
 *|         |                                        |          |
 *|         |       ---------------------------      |          |
 *|         |  ---->|   Stream2 Sync Command  |----> |          |
 *|         |       ---------------------------      |          |
 *|         |                   .                    |          |
 *|         |                   .                    |          |
 *|         |                   .                    |          |
 *|         |       ---------------------------      |          |
 *|         |  ---->|   StreamX Sync Command  |----> |          |
 *|         |       ---------------------------      |          |
 *|         |                                        |          |
 *|         |                                        |          |
 *|         |                                        |          |
 *|  HOST   |                                        | Firmware |
 *|         |                                        |          |
 *|         |                                        |          |
 *|         |                                        |          |
 *|         |                                        |          |
 *|         |       --------------------------       |          |
 *|         |  <----|  Global Response       |<----  |          |
 *|         |       --------------------------       |          |
 *|         |                                        |          |
 *|         |                                        |          |
 *|         |       --------------------------       |          |
 *|         |  <----|  Stream1 response      |<----  |          |
 *|         |       --------------------------       |          |
 *|         |                                        |          |
 *|         |                                        |          |
 *|         |       --------------------------       |          |
 *|         |  <----|  Stream2 Response      |<----  |          |
 *|         |       --------------------------       |          |
 *|         |                   .                    |          |
 *|         |                   .                    |          |
 *|         |                   .                    |          |
 *|         |       --------------------------       |          |
 *|         |  <----|  StreamX Response      |<----  |          |
 *|         |       --------------------------       |          |
 *|         |                                        |          |
 *-----------                                        ------------
 */

/*
 * @brief command ID format
 *        cmd_id is in the format of following type:
 *        type: indicate command type, global/stream commands.
 *        group: indicate the command group.
 *        id: A unique command identification in one type and group.
 *        |<-Bit31 ~ Bit24->|<-Bit23 ~ Bit16->|<-Bit15 ~ Bit0->|
 *        |      type       |      group      |       id       |
 */

#define CMD_TYPE_SHIFT (24) /* @def CMD_TYPE_SHIFT */
#define CMD_TYPE_MASK ((u32)0xff << CMD_TYPE_SHIFT) /* @def CMD_TYPE_MASK */
#define CMD_GROUP_SHIFT (16) /* @def CMD_GROUP_SHIFT */
#define CMD_GROUP_MASK \
	((u32)0xff << CMD_GROUP_SHIFT) /* @def CMD_GROUP_MASK */
#define CMD_ID_MASK ((u32)0xffff) /* @def CMD_ID_MASK */

#define CMD_TYPE_GLOBAL_CTRL \
	((u32)0x1 << CMD_TYPE_SHIFT) /* @def CMD_TYPE_GLOBAL_CTRL */
#define CMD_TYPE_STREAM_CTRL \
	((u32)0x2 << CMD_TYPE_SHIFT) /* @def CMD_TYPE_STREAM_CTRL */

#define GET_CMD_TYPE_VALUE(cmd_id)   \
	(((cmd_id) & CMD_TYPE_MASK) >> \
	 CMD_TYPE_SHIFT) /* @def GET_CMD_TYPE_VALUE */
#define GET_CMD_GROUP_VALUE(cmd_id)   \
	(((cmd_id) & CMD_GROUP_MASK) >> \
	 CMD_GROUP_SHIFT) /* @def GET_CMD_GROUP_VALUE */
#define GET_CMD_ID_VALUE(cmd_id) \
	((cmd_id) & CMD_ID_MASK) /* @def GET_CMD_ID_VALUE */

/* Groups for CMD_TYPE_GLOBAL_CTRL */
#define CMD_GROUP_GLOBAL_GENERAL \
	((u32)0x1 << CMD_GROUP_SHIFT) /* @def CMD_GROUP_GLOBAL_GENERAL */
#define CMD_GROUP_GLOBAL_DEBUG \
	((u32)0x2 << CMD_GROUP_SHIFT) /* @def CMD_GROUP_GLOBAL_DEBUG */
#define CMD_GROUP_GLOBAL_PNP \
	((u32)0x3 << CMD_GROUP_SHIFT) /* @def CMD_GROUP_GLOBAL_PNP */

/* Groups for CMD_TYPE_STREAM_CTRL */
#define CMD_GROUP_STREAM_CTRL \
	((u32)0x1 << CMD_GROUP_SHIFT) /* @def CMD_GROUP_STREAM_CTRL */
#define CMD_GROUP_3A_RTA_CTRL \
	((u32)0x2 << CMD_GROUP_SHIFT) /* @def CMD_GROUP_3A_RTA_CTRL */
#define CMD_GROUP_STREAM_BUFFER \
	((u32)0x4 << CMD_GROUP_SHIFT) /* @def CMD_GROUP_STREAM_BUFFER */

/* General Command */
#define CMD_ID_GET_FW_VERSION                              \
	(CMD_TYPE_GLOBAL_CTRL | CMD_GROUP_GLOBAL_GENERAL | \
	 0x1) /* @def CMD_ID_GET_FW_VERSION */

/* Debug Command */
#define CMD_ID_SET_LOG_LEVEL                             \
	(CMD_TYPE_GLOBAL_CTRL | CMD_GROUP_GLOBAL_DEBUG | \
	 0x3) /* @def CMD_ID_SET_LOG_LEVEL */
#define CMD_ID_SET_LOG_MODULE                            \
	(CMD_TYPE_GLOBAL_CTRL | CMD_GROUP_GLOBAL_DEBUG | \
	 0x4) /* @def CMD_ID_SET_LOG_MODULE */
#define CMD_ID_SET_LOG_MODULE_LEVEL                      \
	(CMD_TYPE_GLOBAL_CTRL | CMD_GROUP_GLOBAL_DEBUG | \
	 0x9) /* @def CMD_ID_SET_LOG_MODULE_LEVEL */

/* Clock/Power/Performance Control Command */
#define CMD_ID_ENABLE_PREFETCH                         \
	(CMD_TYPE_GLOBAL_CTRL | CMD_GROUP_GLOBAL_PNP | \
	 0x3) /* @def CMD_ID_ENABLE_PREFETCH */

/* Stream  Command */
#define CMD_ID_SET_STREAM_CONFIG                        \
	(CMD_TYPE_STREAM_CTRL | CMD_GROUP_STREAM_CTRL | \
	 0x1) /* @def CMD_ID_SET_STREAM_CONFIG */
#define CMD_ID_SET_OUT_CHAN_PROP                        \
	(CMD_TYPE_STREAM_CTRL | CMD_GROUP_STREAM_CTRL | \
	 0x3) /* @def CMD_ID_SET_OUT_CHAN_PROP */
#define CMD_ID_SET_OUT_CHAN_FRAME_RATE_RATIO            \
	(CMD_TYPE_STREAM_CTRL | CMD_GROUP_STREAM_CTRL | \
	 0x4) /* @def CMD_ID_SET_OUT_CHAN_FRAME_RATE_RATIO */
#define CMD_ID_ENABLE_OUT_CHAN                          \
	(CMD_TYPE_STREAM_CTRL | CMD_GROUP_STREAM_CTRL | \
	 0x5) /* @def CMD_ID_ENABLE_OUT_CHAN */
#define CMD_ID_START_STREAM                             \
	(CMD_TYPE_STREAM_CTRL | CMD_GROUP_STREAM_CTRL | \
	 0x7) /* @def CMD_ID_START_STREAM */
#define CMD_ID_STOP_STREAM                              \
	(CMD_TYPE_STREAM_CTRL | CMD_GROUP_STREAM_CTRL | \
	 0x8) /* @def CMD_ID_STOP_STREAM */

/* 3A/RTA Control Command */
#define CMD_ID_SET_3A_ROI                               \
	(CMD_TYPE_STREAM_CTRL | CMD_GROUP_3A_RTA_CTRL | \
	 0x4) /* @def CMD_ID_SET_3A_ROI */

/* Stream Buffer Command */
#define CMD_ID_SEND_BUFFER                                \
	(CMD_TYPE_STREAM_CTRL | CMD_GROUP_STREAM_BUFFER | \
	 0x1) /* @def CMD_ID_SEND_BUFFER */

/*
 * @brief response ID format
 *        resp_id is in the format of following type:
 *        type: indicate command type, global/stream commands.
 *        group: indicate the command group.
 *        id: A unique command identification in one type and group.
 *        |<-Bit31 ~ Bit24->|<-Bit23 ~ Bit16->|<-Bit15 ~ Bit0->|
 *        |      type       |      group      |       id       |
 */

#define RESP_GROUP_SHIFT (16) /* @def RESP_GROUP_SHIFT */
#define RESP_GROUP_MASK (0xff << RESP_GROUP_SHIFT) /* @def RESP_GROUP_MASK */

#define GET_RESP_GROUP_VALUE(resp_id)   \
	(((resp_id) & RESP_GROUP_MASK) >> \
	 RESP_GROUP_SHIFT) /* @def GET_RESP_GROUP_VALUE */
#define GET_RESP_ID_VALUE(resp_id) \
	((resp_id) & 0xffff) /* @def GET_RESP_ID_VALUE */

#define RESP_GROUP_GENERAL \
	(0x1 << RESP_GROUP_SHIFT) /* @def RESP_GROUP_GENERAL */
#define RESP_GROUP_SENSOR \
	(0x2 << RESP_GROUP_SHIFT) /* @def RESP_GROUP_SENSOR */
#define RESP_GROUP_NOTIFICATION \
	(0x3 << RESP_GROUP_SHIFT) /* @def RESP_GROUP_NOTIFICATION */

/* General  Response */
#define RESP_ID_CMD_DONE (RESP_GROUP_GENERAL | 0x1) /* @def RESP_ID_CMD_DONE */

/* Notification */
#define RESP_ID_NOTI_FRAME_DONE \
	(RESP_GROUP_NOTIFICATION | 0x1) /* @def RESP_ID_NOTI_FRAME_DONE */
#define RESP_ID_NOTI_ERROR \
	(RESP_GROUP_NOTIFICATION | 0x2) /* @def RESP_ID_NOTI_ERROR */
#define RESP_ID_NOTI_REQUEST_NON_RTA \
	(RESP_GROUP_NOTIFICATION | 0x3) /* @def RESP_ID_NOTI_REQUEST_NON_RTA */
#define RESP_ID_NOTI_PRIVACY \
	(RESP_GROUP_NOTIFICATION | 0x4) /* @def RESP_ID_NOTI_PRIVACY */

struct cmd_log_set_level_t {
	enum isp_log_level level; /* Log level */
};

#define LOG_MOD_ID_MAX 220			/* The MAX value of module ID */
#define LOG_EXT_NUM ((LOG_MOD_ID_MAX + 7) / 8) /* @def LOG_EXT_NUM */

struct cmd_set_log_mod_level_t {
	u32 level_bits[LOG_EXT_NUM]; /* level bit */
};

struct resp_cmd_done {
	/* The host2fw command seqNum. To indicate which command this response refer to. */
	u32 cmd_seq_num;
	/* The host2fw command id for host double check. */
	u32 cmd_id;
	/* Indicate the command process status. 0 means success. 1 means fail. 2 means skipped */
	u16 cmd_status;
	/* If the cmd_status is 1, that means the command is processed fail, */
	/* host can check the error_code to get the detail error information */
	u16 error_code;
	/* The response payload will be in different struct type */
	/* according to different cmd done response. */
	u8 payload[36];
};

/* @CmdStatus: */
#define CMD_STATUS_SUCCESS (0) /* @def CMD_STATUS_SUCCESS */
#define CMD_STATUS_FAIL (1) /* @def CMD_STATUS_FAIL */
#define CMD_STATUS_SKIPPED (2) /* @def CMD_STATUS_SKIPPED */
/* @ErrorCode: */
#define RESP_ERROR_CODE_NO_ERROR (0) /* @def RESP_ERROR_CODE_NO_ERROR */
#define RESP_ERROR_CODE_CALIB_NOT_SETUP \
	(1) /* @def RESP_ERROR_CODE_CALIB_NOT_SETUP */
#define RESP_ERROR_CODE_SENSOR_PROP_NOT_SETUP \
	(2) /* @def RESP_ERROR_CODE_SENSOR_PROP_NOT_SETUP */
#define RESP_ERROR_CODE_UNSUPPORTED_SENSOR_INTF \
	(3) /* @def RESP_ERROR_CODE_UNSUPPORTED_SENSOR_INTF */
#define RESP_ERROR_CODE_SENSOR_RESOLUTION_INVALID \
	(4) /* @def RESP_ERROR_CODE_SENSOR_RESOLUTION_INVALID */
#define RESP_ERROR_CODE_UNSUPPORTED_STREAM_MODE \
	(5) /* @def RESP_ERROR_CODE_UNSUPPORTED_STREAM_MODE */
#define RESP_ERROR_CODE_UNSUPPORTED_SENSOR_ID \
	(6) /* @def RESP_ERROR_CODE_UNSUPPORTED_SENSOR_ID */
#define RESP_ERROR_CODE_ASPECT_RATIO_WINDOW_INVALID \
	(7) /* @def RESP_ERROR_CODE_ASPECT_RATIO_WINDOW_INVALID */
#define RESP_ERROR_CODE_UNSUPPORTED_IMAGE_FORMAT \
	(8) /* @def RESP_ERROR_CODE_UNSUPPORTED_IMAGE_FORMAT */
#define RESP_ERROR_CODE_OUT_RESOLUTION_OUT_OF_RANGE \
	(9) /* @def RESP_ERROR_CODE_OUT_RESOLUTION_OUT_OF_RANGE */
#define RESP_ERROR_CODE_LOG_WRONG_PARAMETER \
	(10) /* @def RESP_ERROR_CODE_LOG_WRONG_PARAMETER */
#define RESP_ERROR_CODE_INVALID_BUFFER_SIZE \
	(11) /* @def RESP_ERROR_CODE_INVALID_BUFFER_SIZE */
#define RESP_ERROR_CODE_INVALID_AWB_STATE \
	(12) /* @def RESP_ERROR_CODE_INVALID_AWB_STATE */
#define RESP_ERROR_CODE_INVALID_AF_STATE \
	(13) /* @def RESP_ERROR_CODE_INVALID_AF_STATE */
#define RESP_ERROR_CODE_INVALID_AE_STATE \
	(14) /* @def RESP_ERROR_CODE_INVALID_AE_STATE */
#define RESP_ERROR_CODE_UNSUPPORTED_CMD \
	(15) /* @def RESP_ERROR_CODE_UNSUPPORTED_CMD */
#define RESP_ERROR_CODE_QUEUE_OVERFLOW \
	(16) /* @def RESP_ERROR_CODE_QUEUE_OVERFLOW */
#define RESP_ERROR_CODE_SENSOR_ID_OUT_OF_RANGE \
	(17) /* @def RESP_ERROR_CODE_SENSOR_ID_OUT_OF_RANGE */
#define RESP_ERROR_CODE_CHECK_SUM_ERROR \
	(18) /* @def RESP_ERROR_CODE_CHECK_SUM_ERROR */
#define RESP_ERROR_CODE_BUFFER_SIZE_ERROR \
	(19) /* @def RESP_ERROR_CODE_BUFFER_SIZE_ERROR */
#define RESP_ERROR_CODE_UNSUPPORTED_BUFFER_TYPE \
	(20) /* @def RESP_ERROR_CODE_UNSUPPORTED_BUFFER_TYPE */
#define RESP_ERROR_CODE_UNSUPPORTED_COMMAND_ID \
	(21) /* @def RESP_ERROR_CODE_UNSUPPORTED_COMMAND_ID */
#define RESP_ERROR_CODE_STREAM_ID_OUT_OF_RANGE \
	(22) /* @def RESP_ERROR_CODE_STREAM_ID_OUT_OF_RANGE */
#define RESP_ERROR_CODE_INVALID_STREAM_STATE \
	(23) /* @def RESP_ERROR_CODE_INVALID_STREAM_STATE */
#define RESP_ERROR_CODE_INVALID_STREAM_PARAM \
	(24) /* @def RESP_ERROR_CODE_INVALID_STREAM_PARAM */
#define RESP_ERROR_CODE_UNSUPPORTED_MULTI_STREAM_MODE1 \
	(25) /* @def RESP_ERROR_CODE_UNSUPPORTED_MULTI_STREAM_MODE1 */
#define RESP_ERROR_CODE_UNSUPPORTED_LOG_DEBUG \
	(26) /* @def RESP_ERROR_CODE_UNSUPPORTED_LOG_DEBUG */
#define RESP_ERROR_CODE_PACKAGE_SIZE_ERROR \
	(27) /* @def RESP_ERROR_CODE_PACKAGE_SIZE_ERROR */
#define RESP_ERROR_CODE_PACKAGE_CHECK_SUM_ERROR \
	(28) /* @def RESP_ERROR_CODE_PACKAGE_CHECK_SUM_ERROR */
#define RESP_ERROR_CODE_INVALID_PARAM \
	(29) /* @def RESP_ERROR_CODE_INVALID_PARAM */
#define RESP_ERROR_CODE_TIME_OUT (30) /* @def RESP_ERROR_CODE_TIME_OUT */
#define RESP_ERROR_CODE_CANCEL (31) /* @def RESP_ERROR_CODE_CANCEL */
#define RESP_ERROR_CODE_REPEAT_TNR_REF_BUF \
	(32) /* @def RESP_ERROR_CODE_REPEAT_TNR_REF_BUF */
#define RESP_ERROR_CODE_HARDWARE_ERROR \
	(33) /* @def RESP_ERROR_CODE_HARDWARE_ERROR */
#define RESP_ERROR_CODE_MEMORY_LACK \
	(34) /* @def RESP_ERROR_CODE_MEMORY_LACK */
#define RESP_ERROR_CODE_TNR_REF_BUF_INVALID \
	(35) /* @def RESP_ERROR_CODE_TNR_REF_BUF_INVALID */
#define RESP_ERROR_CODE_INVALID_DYNAMIC_IQ_STATE \
	(36) /* @def RESP_ERROR_CODE_INVALID_DYNAMIC_IQ_STATE */
#define RESP_ERROR_CODE_INVALID_LSC_STATE \
	(37) /* @def RESP_ERROR_CODE_INVALID_LSC_STATE */
#define RESP_ERROR_CODE_UNSUPPORTED_SHARPEN_ID \
	(38) /* @def RESP_ERROR_CODE_UNSUPPORTED_SHARPEN_ID */
#define RESP_ERROR_CODE_PIPELINE_ERROR \
	(39) /* @def RESP_ERROR_CODE_PIPELINE_ERROR */
#define RESP_ERROR_CODE_BUFFERMGR_ERROR \
	(40) /* @def RESP_ERROR_CODE_BUFFERMGR_ERROR */
#define RESP_ERROR_CODE_SENSOR_ERROR \
	(41) /* @def RESP_ERROR_CODE_SENSOR_ERROR */
#define RESP_ERROR_CODE_I2C_ERROR (42) /* @def RESP_ERROR_CODE_I2C_ERROR */
#define RESP_ERROR_CODE_RTA_ERROR (43) /* @def RESP_ERROR_CODE_RTA_ERROR */
#define RESP_ERROR_CODE_SECURE_SKIP (44) /* @def RESP_ERROR_CODE_SECURE_SKIP */

struct resp_error {
	enum error_level error_level;	/* error_level */
	struct error_code error_code;	/* error_code */
};

struct cmd_send_buffer {
	enum buffer_type_t buffer_type;	/* buffer Type */
	struct buffer_t buffer;		/* buffer info */
};

struct cmd_set_out_ch_prop {
	enum isp_pipe_out_ch_t ch;	/* ISP pipe out channel */
	struct image_prop_t image_prop;	/* image property */
};

struct cmd_set_out_ch_frame_rate_ratio {
	enum isp_pipe_out_ch_t ch;	/* ISP pipe out channel */
	u32 ratio;			/* ratio: Please refer to "Description:" above. */
};

struct cmd_enable_out_ch {
	enum isp_pipe_out_ch_t ch;	/* ISP pipe out channel */
	u32 is_enable;			/* If enable channel or not */
};

struct cmd_set_stream_cfg {
	struct stream_cfg_t stream_cfg; /* stream path config */
};

struct aa_roi {
	u32 roi_type;				/* See enum roi_type_mask_t */
	u32 mode_mask;				/* See enum roi_mode_mask_t */
	struct isp_touch_info_t touch_info;	/* Rouch ROI data */
	struct isp_fd_info_t fd_info;		/* Face detection data */
};

struct cmd_config_mmhub_prefetch {
	u32 b_rtpipe;			/* b_rtpipe */
	u32 b_soft_rtpipe;		/* b_soft_rtpipe */
	u32 b_add_gap_for_yuv;		/* b_add_gap_for_yuv */
};

#endif
