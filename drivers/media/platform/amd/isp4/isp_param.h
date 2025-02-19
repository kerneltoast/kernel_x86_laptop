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
#ifndef ISP_PARAM_H
#define ISP_PARAM_H

#define CAMERA_PORT_0_RAW_TYPE  CAMERA_TYPE_RGB_BAYER
#define CAMERA_PORT_1_RAW_TYPE  CAMERA_TYPE_RGB_BAYER
#define CAMERA_PORT_2_RAW_TYPE  CAMERA_TYPE_RGB_BAYER

#define INTERNAL_MEMORY_POOL_SIZE (200 * 1024 * 1024)

/*
 * Pipeline: LME
 */
#define LME_PIPELINE_ID 0x80

/*
 * Pipeline: Mipi0CsisCsta0=>LME=>Isp
 */
#define MIPI0CSISCSTAT0_LME_ISP_PIPELINE_ID 0x5f91

/*
 * @def MAX_OUTPUT_MCSC
 * The max output port of mcsc sub-Ip
 * output0 - Preview
 * output1 - video
 * output2 - still
 */
#define MAX_OUTPUT_MCSC                 (3)

/*
 * @enum sensor_id
 * The enum definition of sensor Id
 */
enum sensor_id {
	SENSOR_ID_INVALID   = -1, /* Invalid sensor id */
	SENSOR_ID_ON_MIPI0  = 0,  /* Sensor id for ISP input from MIPI port 0 */
	SENSOR_ID_ON_MIPI1  = 1,  /* Sensor id for ISP input from MIPI port 1 */
	SENSOR_ID_ON_MIPI2  = 2,  /* Sensor id for ISP input from MIPI port 2 */
	SENSOR_ID_RDMA      = 3,  /* Sensor id for ISP input from RDMA */
	SENSOR_ID_CREST     = 4,  /* Sensor id for ISP input from CREST */
	SENSOR_ID_MAX             /* Maximum sensor id for ISP input */
};

/*
 * @enum sensor_i2c_device_id
 * The enum definition of I2C device Id
 */
enum sensor_i2c_device_id {
	I2C_DEVICE_ID_INVALID = -1, /* I2C_DEVICE_ID_INVALID. */
	I2C_DEVICE_ID_A  = 0,       /* I2C_DEVICE_ID_A. */
	I2C_DEVICE_ID_B  = 1,       /* I2C_DEVICE_ID_B. */
	I2C_DEVICE_ID_C  = 2,       /* I2C_DEVICE_ID_C. */
	I2C_DEVICE_ID_MAX           /* I2C_DEVICE_ID_MAX. */
};

/*
 * @struct error_code
 * The struct definition of fw error code
 */
struct error_code {
	u32 code1;  /* See ERROR_CODE1_XXX for reference */
	u32 code2;  /* See ERROR_CODE2_XXX for reference */
	u32 code3;  /* See ERROR_CODE3_XXX for reference */
	u32 code4;  /* See ERROR_CODE4_XXX for reference */
	u32 code5;  /* See ERROR_CODE5_XXX for reference */
};

/*
 * @enum isp_log_level
 * The enum definition of different FW log level
 */
enum isp_log_level {
	ISP_LOG_LEVEL_DEBUG  = 0,   /* The FW will output all debug and above level log */
	ISP_LOG_LEVEL_INFO   = 1,   /* The FW will output all info and above level log */
	ISP_LOG_LEVEL_WARN   = 2,   /* The FW will output all warning and above level log */
	ISP_LOG_LEVEL_ERROR  = 3,   /* The FW will output all error and above level log */
	ISP_LOG_LEVEL_MAX           /* The FW will output none level log */
};

/*
 * @enum sensor_intf_type
 * The enum definition of different sensor interface type
 */
enum sensor_intf_type {
	SENSOR_INTF_TYPE_MIPI      = 0, /* The MIPI Csi2 sensor interface */
	SENSOR_INTF_TYPE_PARALLEL  = 1, /* The Parallel sensor interface */
	SENSOR_INTF_TYPE_RDMA      = 2, /* There is no sensor, just get data from RDMA */
	SENSOR_INTF_TYPE_MAX            /* The max value of enum sensor_intf_type */
};

/*
 * @enum error_level
 * The enum definition of error level
 */
enum error_level {
	ERROR_LEVEL_INVALID, /* invalid level */
	ERROR_LEVEL_FATAL,   /* The error has caused the stream stopped */
	ERROR_LEVEL_WARN,    /* Firmware has automatically restarted the stream */
	ERROR_LEVEL_NOTICE,  /* Should take notice of this error which may lead some other error */
	ERROR_LEVEL_MAX      /* max value of enum error_level */
};

/*
 * Command Structure for FW
 */

/*
 * @struct cmd_t
 * The host command is 64 bytes each.
 * The format of the command is defined as following
 */
struct cmd_t {
	u32 cmd_seq_num;
	u32 cmd_id;
	u32 cmd_param[12];
	u16 cmd_stream_id;
	u8 cmd_silent_resp;
	u8 reserved;
#ifdef CONFIG_ENABLE_CMD_RESP_256_BYTE
	u8  reserved_1[192];
#endif
	u32 cmd_check_sum;
};

/*
 * @struct  _cmd_param_package_t
 * The Definition of command parameter package structure
 */
struct cmd_param_package_t {
	u32 package_addr_lo;	/* The low 32 bit address of the package address. */
	u32 package_addr_hi;	/* The high 32 bit address of the package address. */
	u32 package_size;	/* The total package size in bytes. */
	u32 package_check_sum;	/* The byte sum of the package. */
};

/*
 * Response Structure for FW
 */

/*
 * @struct  _resp_t
 * The Definition of command response structure.
 * The struct resp_t should be totally 64 bytes.
 */
struct resp_t {
	u32 resp_seq_num;
	u32 resp_id;
	u32 resp_param[12];

	u8  reserved[4];
#ifdef CONFIG_ENABLE_CMD_RESP_256_BYTE
	u8  reserved_1[192]; /* reserved_1 for 256 byte align use */
#endif
	u32 resp_check_sum;
};

/*
 * @struct  _resp_param_package_t
 * The Definition of command response param package structure
 */
struct resp_param_package_t {
	u32 package_addr_lo;	/* The low 32 bit address of the package address. */
	u32 package_addr_hi;	/* The high 32 bit address of the package address. */
	u32 package_size;	/* The total package size in bytes. */
	u32 package_check_sum;	/* The byte sum of the package. */
};

/*
 * @enum cmd_chan_id_t
 * The enum definition of command channel ID
 */
enum cmd_chan_id_t {
	CMD_CHAN_ID_INVALID     = -1,   /* Invalid ID */
	CMD_CHAN_ID_STREAM_1    =  0,   /* Stream1 channel ID */
	CMD_CHAN_ID_STREAM_2    =  1,   /* Stream2 channel ID */
	CMD_CHAN_ID_STREAM_3    =  2,   /* Stream3 channel ID */
	CMD_CHAN_ID_ASYNC       =  3,   /* Async channel ID */
	CMD_CHAN_ID_MAX         =  4    /* Max value if command channel ID */
};

/*
 * @enum resp_chan_id_t
 * The enum definition of response channel ID
 */
enum resp_chan_id_t {
	RESP_CHAN_ID_INVALID    = -1,   /* Invalid ID */
	RESP_CHAN_ID_STREAM_1   =  0,   /* Stream1 channel ID */
	RESP_CHAN_ID_STREAM_2   =  1,   /* Stream2 channel ID */
	RESP_CHAN_ID_STREAM_3   =  2,   /* Stream3 channel ID */
	RESP_CHAN_ID_GLOBAL     =  3,   /* global channel ID */
	RESP_CHAN_ID_MAX        =  4    /* Max value if command channel ID */
};

/*
 * @struct window_t
 * The struct definition of window structure
 */
struct window_t {
	u32 h_offset;         /* The offset of window horizontal direction */
	u32 v_offset;         /* The offset of window vertical direction */
	u32 h_size;           /* The size of window horizontal direction */
	u32 v_size;           /* The size of window vertical direction */
};

/*
 * @struct point_t
 * The struct definition of a point structure
 */
struct point_t {
	u32 x; /* The x coordinate of the point */
	u32 y; /* The y coordinate of the point */
};

/*
 * @enum stream_id_t
 * The enum definition of stream Id
 */
enum stream_id_t {
	STREAM_ID_INVALID = -1, /* STREAM_ID_INVALID. */
	STREAM_ID_1 = 0,        /* STREAM_ID_1. */
	STREAM_ID_2 = 1,        /* STREAM_ID_2. */
	STREAM_ID_3 = 2,        /* STREAM_ID_3. */
	STREAM_ID_MAXIMUM       /* STREAM_ID_MAXIMUM. */
};

/*
 * @enum image_format_t
 * The enum definition of image format
 */
enum image_format_t {
	IMAGE_FORMAT_INVALID,           /* invalid */
	IMAGE_FORMAT_NV12,              /* 4:2:0,semi-planar, 8-bit */
	IMAGE_FORMAT_NV21,              /* 4:2:0,semi-planar, 8-bit */
	IMAGE_FORMAT_I420,              /* 4:2:0,planar, 8-bit */
	IMAGE_FORMAT_YV12,              /* 4:2:0,planar, 8-bit */
	IMAGE_FORMAT_YUV422PLANAR,      /* 4:2:2,planar, 8-bit */
	IMAGE_FORMAT_YUV422SEMIPLANAR,  /* semi-planar, 4:2:2,8-bit */
	IMAGE_FORMAT_YUV422INTERLEAVED, /* interleave, 4:2:2, 8-bit */
	IMAGE_FORMAT_P010,              /* semi-planar, 4:2:0, 10-bit */
	IMAGE_FORMAT_Y210,              /* interleave, 4:2:2, 10-bit */
	IMAGE_FORMAT_L8,                /* Only Y 8-bit */
	IMAGE_FORMAT_RGBBAYER8,         /* RGB bayer 8-bit */
	IMAGE_FORMAT_RGBBAYER10,        /* RGB bayer 10-bit */
	IMAGE_FORMAT_RGBBAYER12,        /* RGB bayer 12-bit */
	IMAGE_FORMAT_RGBBAYER14,        /* RGB bayer 14-bit */
	IMAGE_FORMAT_RGBBAYER16,        /* RGB bayer 16-bit */
	IMAGE_FORMAT_RGBBAYER20,        /* RGB bayer 20-bit */
	IMAGE_FORMAT_RGBIR8,            /* RGBIR 8-bit */
	IMAGE_FORMAT_RGBIR10,           /* RGBIR 10-bit */
	IMAGE_FORMAT_RGBIR12,           /* RGBIR 12-bit */
	IMAGE_FORMAT_Y210BF,            /* interleave, 4:2:2, 10-bit bubble free */
	IMAGE_FORMAT_RGB888,            /* RGB 888 */
	IMAGE_FORMAT_BAYER12,           /* Bayer 12-bit */
	IMAGE_FORMAT_RAWDATA,           /* Raw unformatted data */
	IMAGE_FORMAT_MAX                /* Max value of enum image_format_t */
};

/*
 * @struct mipi_pipe_path_cfg_t
 * Mipi pipe cfg info
 */
struct mipi_pipe_path_cfg_t {
	u32 b_enable;			/* If disabled, the RAW image only can be from host */
	enum sensor_id sensor_id;	/* Sensor Id */
};

/*
 * @enum isp_pipe_out_ch_t
 * The output channel type
 */
enum isp_pipe_out_ch_t {
	ISP_PIPE_OUT_CH_PREVIEW = 0,        /* Preview */
	ISP_PIPE_OUT_CH_VIDEO,              /* Video */
	ISP_PIPE_OUT_CH_STILL,              /* Still */
	ISP_PIPE_OUT_CH_RAW,                /* Raw */
	ISP_PIPE_OUT_CH_MIPI_RAW,           /* Mipi Raw */
	ISP_PIPE_OUT_CH_MIPI_HDR_RAW,       /* Mipi Raw for DoLHDR short exposure */
	ISP_PIPE_OUT_CH_MIPI_TMP,           /* Mipi Raw tmp */
	ISP_PIPE_OUT_CH_MIPI_HDR_RAW_TMP,   /* Mipi HDR Shor Exposure Raw */
	ISP_PIPE_OUT_CH_CSTAT_DS_PREVIEW,   /* Cstat downscaler */
	ISP_PIPE_OUT_CH_LME_MV0,
	ISP_PIPE_OUT_CH_LME_MV1,
	ISP_PIPE_OUT_CH_LME_WDMA,
	ISP_PIPE_OUT_CH_LME_SAD,
	ISP_PIPE_OUT_CH_BYRP_TAPOUT,        /* Byrp tapout */
	ISP_PIPE_OUT_CH_RGBP_TAPOUT,        /* Rgbp tapout */
	ISP_PIPE_OUT_CH_MCFP_TAPOUT,        /* Mcfp tapout */
	ISP_PIPE_OUT_CH_YUVP_TAPOUT,        /* Yuvp tapout */
	ISP_PIPE_OUT_CH_MCSC_TAPOUT,        /* Mcsc tapout */
	ISP_PIPE_OUT_CH_CSTAT_CDS,          /* Cstat CDS */
	ISP_PIPE_OUT_CH_CSTAT_FDPIG,        /* Cstat FDPIG */
	ISP_PIPE_OUT_CH_MAX,                /* Max */
};

/*
 * @struct isp_pipe_path_cfg_t
 * Isp pipe path cfg info
 * A combination value from enum isp_pipe_id
 */
struct isp_pipe_path_cfg_t {
	u32  isp_pipe_id;                  /* pipe ids for pipeline construction */
};

/*
 * @struct StreamPathCfg_t
 * Stream path cfg info
 */
struct stream_cfg_t {
	struct mipi_pipe_path_cfg_t mipi_pipe_path_cfg;	/* Isp mipi path */
	struct isp_pipe_path_cfg_t  isp_pipe_path_cfg;	/* Isp pipe path */
	u32 b_enable_tnr;				/* enable TNR */
	u32 rta_frames_per_proc;			/* number of frame rta per-processing,
							 * set to 0 to use fw default value
							 */
};

enum isp_yuv_range_t {
	ISP_YUV_RANGE_FULL = 0,     /* YUV value range in 0~255 */
	ISP_YUV_RANGE_NARROW = 1,   /* YUV value range in 16~235 */
	ISP_YUV_RANGE_MAX
};

/*
 * @struct image_prop_t
 *Image property
 */
struct image_prop_t {
	enum image_format_t image_format;	/* Image format */
	u32 width;				/* Width */
	u32 height;				/* Height */
	u32 luma_pitch;				/* Luma pitch */
	u32 chroma_pitch;			/* Chrom pitch */
	enum isp_yuv_range_t yuv_range;		/* YUV value range */
};

/*
 * @page RawPktFmt
 * @verbatim
 * Suppose the image pixel is in the sequence of:
 *             A B C D E F
 *             G H I J K L
 *             M N O P Q R
 *             ...
 * The following RawPktFmt define the raw picture output format.
 * For each format, different raw pixel width will have different memory
 * filling format. The raw pixel width is set by the SesorProp_t.
 *
 * RAW_PKT_FMT_0:
 *    --------+----------------------------------------------------------------
 *    Bit-Pos |  15  14  13  12  11  10  9   8   7   6   5   4   3   2   1   0
 *    --------+----------------------------------------------------------------
 *    8-BIT   |  0   0   0   0   0   0   0   0   A7  A6  A5  A4  A3  A2  A1  A0
 *    10-BIT  |  A1  A0  0   0   0   0   0   0   A9  A8  A7  A6  A5  A4  A3  A2
 *    12-BIT  |  A3  A2  A1  A0  0   0   0   0   A11 A10 A9  A8  A7  A6  A5  A4
 *    14-BIT  |  A5  A4  A3  A2  A1  A0  0   0   A13 A12 A11 A10 A9  A8  A7  A6
 *
 * RAW_PKT_FMT_1:
 *    --------+----------------------------------------------------------------
 *    Bit-Pos |  15  14  13  12  11  10  9   8   7   6   5   4   3   2   1   0
 *    --------+----------------------------------------------------------------
 *    8-BIT      B7  B6  B5  B4  B3  B2  B1  B0  A7  A6  A5  A4  A3  A2  A1  A0
 *    10-BIT     A1  A0  0   0   0   0   0   0   A9  A8  A7  A6  A5  A4  A3  A2
 *    12-BIT     A3  A2  A1  A0  0   0   0   0   A11 A10 A9  A8  A7  A6  A5  A4
 *    14-BIT     A5  A4  A3  A2  A1  A0  0   0   A13 A12 A11 A10 A9  A8  A7  A6
 *
 * RAW_PKT_FMT_2:
 *    --------+----------------------------------------------------------------
 *    Bit-Pos |  15  14  13  12  11  10  9   8   7   6   5   4   3   2   1   0
 *    --------+----------------------------------------------------------------
 *    8-BIT      0   0   0   0   0   0   0   0   A7  A6  A5  A4  A3  A2  A1  A0
 *    10-BIT     0   0   0   0   0   0   A9  A8  A7  A6  A5  A4  A3  A2  A1  A0
 *    12-BIT     0   0   0   0   A11 A10 A9  A8  A7  A6  A5  A4  A3  A2  A1  A0
 *    14-BIT     0   0   A13 A12 A11 A10 A9  A8  A7  A6  A5  A4  A3  A2  A1  A0
 *
 * RAW_PKT_FMT_3:
 *    --------+----------------------------------------------------------------
 *    Bit-Pos |  15  14  13  12  11  10  9   8   7   6   5   4   3   2   1   0
 *    --------+----------------------------------------------------------------
 *    8-BIT      B7  B6  B5  B4  B3  B2  B1  B0  A7  A6  A5  A4  A3  A2  A1  A0
 *    10-BIT     0   0   0   0   0   0   A9  A8  A7  A6  A5  A4  A3  A2  A1  A0
 *    12-BIT     0   0   0   0   A11 A10 A9  A8  A7  A6  A5  A4  A3  A2  A1  A0
 *    14-BIT     0   0   A13 A12 A11 A10 A9  A8  A7  A6  A5  A4  A3  A2  A1  A0
 *
 * RAW_PKT_FMT_4:
 *    (1) 8-BIT:
 *    --------+----------------------------------------------------------------
 *    Bit-Pos |  15  14  13  12  11  10  9   8   7   6   5   4   3   2   1   0
 *    --------+----------------------------------------------------------------
 *               B7  B6  B5  B4  B3  B2  B1  B0  A7  A6  A5  A4  A3  A2  A1  A0
 *    (2) 10-BIT:
 *    --------+----------------------------------------------------------------
 *    Bit-Pos |  15  14  13  12  11  10  9   8   7   6   5   4   3   2   1   0
 *    --------+----------------------------------------------------------------
 *               B5  B4  B3  B2  B1  B0  A9  A8  A7  A6  A5  A4  A3  A2  A1  A0
 *               D1  D0  C9  C8  C7  C6  C5  C4  C3  C2  C1  C0  B9  B8  B7  B6
 *               E7  E6  E5  E4  E3  E2  E1  E0  D9  D8  D7  D6  D5  D4  D3  D2
 *               G3  G2  G1  G0  F9  F8  F7  F6  F5  F4  F3  F2  F1  F0  E9  E8
 *               H9  H8  H7  H6  H5  H4  H3  H2  H1  H0  G9  G8  G7  G6  G5  G4
 *    (3) 12-BIT:
 *    --------+----------------------------------------------------------------
 *    Bit-Pos |  15  14  13  12  11  10  9   8   7   6   5   4   3   2   1   0
 *    --------+----------------------------------------------------------------
 *               B3  B2  B1  B0  A11 A10 A9  A8  A7  A6  A5  A4  A3  A2  A1  A0
 *               C7  C6  C5  C4  C3  C2  C1  C0  B11 B10 B9  B8  B7  B6  B5  B4
 *               D11 D10 D9  D8  D7  D6  D5  D4  D3  D2  D1  D0  C11 C10 C9  C8
 *    (4) 14-BIT:
 *    --------+----------------------------------------------------------------
 *    Bit-Pos |  15  14  13  12  11  10  9   8   7   6   5   4   3   2   1   0
 *    --------+----------------------------------------------------------------
 *               B1  B0  A13 A12 A11 A10 A9  A8  A7  A6  A5  A4  A3  A2  A1  A0
 *               C3  C2  C1  C0  B13 B12 B11 B10 B9  B8  B7  B6  B5  B4  B3  B2
 *               D5  D4  D3  D2  D1  D0  C13 C12 C11 C10 C9  C8  C7  C6  C5  C4
 *               E7  E6  E5  E4  E3  E2  E1  E0  D13 D12 D11 D10 D9  D8  D7  D6
 *               F9  F8  F7  F6  F5  F4  F3  F2  F1  F0  E13 E12 E11 E10 E9  E8
 *               G11 G10 G9  G8  G7  G6  G5  G4  G3  G2  G1  G0  F13 F12 F11 F10
 *               H13 H12 H11 H10 H9  H8  H7  H6  H5  H4  H3  H2  H1  H0  G13 G12
 *
 * RAW_PKT_FMT_5:
 *    --------+----------------------------------------------------------------
 *    Bit-Pos |  15  14  13  12  11  10  9   8   7   6   5   4   3   2   1   0
 *    --------+----------------------------------------------------------------
 *    8-BIT      B7  B6  B5  B4  B3  B2  B1  B0  A7  A6  A5  A4  A3  A2  A1  A0
 *    10-BIT     0   0   0   0   0   0   A9  A8  A7  A6  A5  A4  A3  A2  A1  A0
 *    12-BIT     0   0   0   0   A11 A10 A9  A8  A7  A6  A5  A4  A3  A2  A1  A0
 *
 * RAW_PKT_FMT_6:
 *    --------+----------------------------------------------------------------
 *    Bit-Pos |  15  14  13  12  11  10  9   8   7   6   5   4   3   2   1   0
 *    --------+----------------------------------------------------------------
 *    8-BIT      A7  A6  A5  A4  A3  A2  A1  A0  B7  B6  B5  B4  B3  B2  B1  B0
 *    10-BIT     A9  A8  A7  A6  A5  A4  A3  A2  A1  A0  0   0   0   0   0   0
 *    12-BIT     A11 A10 A9  A8  A7  A6  A5  A4  A3  A2  A1  A0  0   0   0   0
 * @endverbatim
 */

/*
 * @enum raw_pkt_fmt_t
 * Raw package format
 */
enum raw_pkt_fmt_t {
	RAW_PKT_FMT_0,  /* Default(ISP1P1 legacy format) */
	RAW_PKT_FMT_1,  /* ISP1P1 legacy format and bubble-free for 8-bit raw pixel */
	RAW_PKT_FMT_2,  /* Android RAW16 format */
	RAW_PKT_FMT_3,  /* Android RAW16 format and bubble-free for 8-bit raw pixel */
	RAW_PKT_FMT_4,  /* ISP2.0 bubble-free format */
	RAW_PKT_FMT_5,  /* RGB-IR format for GPU process */
	RAW_PKT_FMT_6,  /* RGB-IR format for GPU process with data swapped */
	RAW_PKT_FMT_MAX /* Max */
};

/*
 * @enum buffer_type_t
 * Buffer type
 */
enum buffer_type_t {
	BUFFER_TYPE_INVALID,             /* Enum value Invalid */

	BUFFER_TYPE_RAW,                 /* Enum value BUFFER_TYPE_RAW_ZSL, */
	BUFFER_TYPE_MIPI_RAW,            /* Enum value BUFFER_TYPE_MIPI_RAW, */
	BUFFER_TYPE_RAW_TEMP,            /* Enum value BUFFER_TYPE_RAW_TEMP */
	BUFFER_TYPE_MIPI_RAW_SHORT_EXPO, /* Enum value BUFFER_TYPE_MIPI_RAW_SHORT_EXPO(DoLHDR) */
	BUFFER_TYPE_EMB_DATA,            /* Enum value BUFFER_TYPE_EMB_DATA */
	BUFFER_TYPE_PD_DATA,             /* Enum value PD for stg1 or stg2 */

	BUFFER_TYPE_STILL,               /* Enum value BUFFER_TYPE_STILL */
	BUFFER_TYPE_PREVIEW,             /* Enum value BUFFER_TYPE_PREVIEW */
	BUFFER_TYPE_VIDEO,               /* Enum value BUFFER_TYPE_VIDEO */

	BUFFER_TYPE_META_INFO,           /* Enum value BUFFER_TYPE_META_INFO */
	BUFFER_TYPE_FRAME_INFO,          /* Enum value BUFFER_TYPE_FRAME_INFO */

	BUFFER_TYPE_TNR_REF,             /* Enum value BUFFER_TYPE_TNR_REF */
	BUFFER_TYPE_META_DATA,           /* Enum value BUFFER_TYPE_META_DATA */
	BUFFER_TYPE_SETFILE_DATA,        /* Enum value BUFFER_TYPE_SETFILE_DATA */
	BUFFER_TYPE_MEM_POOL,            /* Enum value BUFFER_TYPE_MEM_POOL */
	BUFFER_TYPE_CSTAT_DS,            /* Enum value BUFFER_TYPE_CSTAT_DS */

	/* Lme buffer types for DIAG loopback test */
	BUFFER_TYPE_LME_RDMA,
	BUFFER_TYPE_LME_PREV_RDMA,
	BUFFER_TYPE_LME_WDMA,
	BUFFER_TYPE_LME_MV0,
	BUFFER_TYPE_LME_MV1,
	BUFFER_TYPE_LME_SAD,

	BUFFER_TYPE_BYRP_TAPOUT,         /* Enum value BUFFER_TYPE_BYRP_TAPOUT */
	BUFFER_TYPE_RGBP_TAPOUT,         /* Enum value BUFFER_TYPE_RGBP_TAPOUT */
	BUFFER_TYPE_MCFP_TAPOUT,         /* Enum value BUFFER_TYPE_MCFP_TAPOUT */
	BUFFER_TYPE_YUVP_TAPOUT,         /* Enum value BUFFER_TYPE_YUVP_TAPOUT */
	BUFFER_TYPE_MCSC_TAPOUT,         /* Enum value BUFFER_TYPE_MCSC_TAPOUT */
	BUFFER_TYPE_CSTAT_CDS,           /* Enum value BUFFER_TYPE_CSTAT_CDS */
	BUFFER_TYPE_CSTAT_FDPIG,         /* Enum value BUFFER_TYPE_CSTAT_FDPIG */

	BUFFER_TYPE_YUVP_INPUT_SEG,      /* Enum value BUFFER_TYPE_YUVP_INPUT_SEG */
	BUFFER_TYPE_CTL_META_DATA,       /* Enum value BUFFER_TYPE_CTL_META_DATA */
	BUFFER_TYPE_EMUL_DATA,           /* Enum value BUFFER_TYPE_EMUL_DATA */
	BUFFER_TYPE_CSTAT_DRC,           /* Enum value BUFFER_TYPE_CSTAT_DRC */
	BUFFER_TYPE_MAX                  /* Enum value  Max */
};

/*
 * @enum addr_space_type_t
 * Address space type
 */
enum addr_space_type_t {
	ADDR_SPACE_TYPE_GUEST_VA        = 0,    /* Enum value ADDR_SPACE_TYPE_GUEST_VA */
	ADDR_SPACE_TYPE_GUEST_PA        = 1,    /* Enum value ADDR_SPACE_TYPE_GUEST_PA */
	ADDR_SPACE_TYPE_SYSTEM_PA       = 2,    /* Enum value ADDR_SPACE_TYPE_SYSTEM_PA */
	ADDR_SPACE_TYPE_FRAME_BUFFER_PA = 3,    /* Enum value ADDR_SPACE_TYPE_FRAME_BUFFER_PA */
	ADDR_SPACE_TYPE_GPU_VA          = 4,    /* Enum value ADDR_SPACE_TYPE_GPU_VA */
	ADDR_SPACE_TYPE_MAX             = 5     /* Enum value max */
};

/*
 * @struct buffer_t
 * The definition of struct buffer_t
 */
struct buffer_t {
	/* A check num for debug usage, host need to */
	/* set the buf_tags to different number */
	u32 buf_tags;
	union {
		u32 value;		/* The member of vmid_space union; Vmid[31:16], Space[15:0] */
		struct {
			u32 space : 16;	/* The member of vmid_space union; Vmid[31:16], Space[15:0] */
			u32 vmid  : 16;	/* The member of vmid_space union; Vmid[31:16], Space[15:0] */
		} bit;
	} vmid_space;
	u32 buf_base_a_lo;		/* Low address of buffer A */
	u32 buf_base_a_hi;		/* High address of buffer A */
	u32 buf_size_a;			/* Buffer size of buffer A */

	u32 buf_base_b_lo;		/* Low address of buffer B */
	u32 buf_base_b_hi;		/* High address of buffer B */
	u32 buf_size_b;			/* Buffer size of buffer B */

	u32 buf_base_c_lo;		/* Low address of buffer C */
	u32 buf_base_c_hi;		/* High address of buffer C */
	u32 buf_size_c;			/* Buffer size of buffer C */
};

/*
 * @enum irillu_status_t enum irillu_status_t
 */
/*
 * @enum  _irillu_status_t
 * @brief  The status of IR illuminator
 */
enum irillu_status_t {
	IR_ILLU_STATUS_UNKNOWN,	/* enum value: IR_ILLU_STATUS_UNKNOWN */
	IR_ILLU_STATUS_ON,	/* enum value: IR_ILLU_STATUS_ON */
	IR_ILLU_STATUS_OFF,	/* enum value: IR_ILLU_STATUS_OFF */
	IR_ILLU_STATUS_MAX,	/* enum value: IR_ILLU_STATUS_MAX */
};

/*
 * @struct irmeta_info_t struct irmeta_info_t
 */
/*
 * @struct irmeta_info_t
 * @brief  The IR MetaInfo for IR illuminator status
 */
struct irmeta_info_t {
	enum irillu_status_t ir_illu_status; /* IR illuminator status */
};

/* AAA */
/* ------- */
#define MAX_REGIONS               16

/* ISP firmware supported AE ROI region num */
#define MAX_AE_ROI_REGION_NUM     1

/* ISP firmware supported AWB ROI region num */
#define MAX_AWB_ROI_REGION_NUM    0

/* ISP firmware supported AF ROI region num */
#define MAX_AF_ROI_REGION_NUM     0

/*
 * _roi_type_mask_t
 */
enum roi_type_mask_t {
	ROI_TYPE_MASK_AE = 0x1, /* AE ROI */
	ROI_TYPE_MASK_AWB = 0x2,/* AWB ROI */
	ROI_TYPE_MASK_AF = 0x4, /* AF ROI */
	ROI_TYPE_MASK_MAX
};

/*
 * @enum roi_mode_mask_t
 * ROI modes
 */
enum roi_mode_mask_t {
	ROI_MODE_MASK_TOUCH = 0x1, /* Using touch ROI */
	ROI_MODE_MASK_FACE = 0x2   /* Using face ROI */
};

/*
 * @brief   Defines and area using the top left and bottom right corners
 */
struct isp_area_t {
	struct point_t top_left;       /* !< top left corner */
	struct point_t bottom_right;   /* !< bottom right corner */
};

/*
 * @brief Defines the touch area with weight
 */
struct isp_touch_area_t {
	struct isp_area_t points;	/* Touch region's top left and bottom right points */
	u32    touch_weight;		/* touch area's weight */
};

/*
 * @brief Face detection land marks
 */
struct isp_fd_landmarks_t {
	struct point_t eye_left;
	struct point_t eye_right;
	struct point_t nose;
	struct point_t mouse_left;
	struct point_t mouse_right;
};

/*
 * @brief Face detection all face info
 */
struct isp_fd_face_info_t {
	u32 face_id;			/* The ID of this face */
	u32 score;			/* The score of this face, larger than 0 for valid face */
	struct isp_area_t face_area;	/* The face region info */
	struct isp_fd_landmarks_t marks;/* The face landmarks info */
};

/*
 * @brief Face detection info
 */
struct isp_fd_info_t {
	u32 is_enabled;					/* Set to 0 to disable this face detection info */
	u32 frame_count;				/* Frame count of this face detection info from */
	u32 is_marks_enabled;				/* Set to 0 to disable the five marks on the faces */
	u32 face_num;					/* Number of faces */
	struct isp_fd_face_info_t face[MAX_REGIONS];	/* Face detection info */
};

/*
 * @brief Touch ROI info
 */
struct isp_touch_info_t {
	u32 touch_num;						/* Touch region numbers */
	struct isp_touch_area_t  touch_area[MAX_REGIONS];	/* Touch regions */
};

/*
 * @enum buffer_status_t enum buffer_status_t
 */
/*
 * @enum  _buffer_status_t
 * @brief  The enumeration about BufferStatus
 */
enum buffer_status_t {
	BUFFER_STATUS_INVALID,  /* The buffer is INVALID */
	BUFFER_STATUS_SKIPPED,  /* The buffer is not filled with image data */
	BUFFER_STATUS_EXIST,    /* The buffer is exist and waiting for filled */
	BUFFER_STATUS_DONE,     /* The buffer is filled with image data */
	BUFFER_STATUS_LACK,     /* The buffer is unavailable */
	BUFFER_STATUS_DIRTY,    /* The buffer is dirty, probably caused by LMI leakage */
	BUFFER_STATUS_MAX       /* The buffer STATUS_MAX */
};

/*
 * @enum buffer_source_t enum buffer_source_t
 */
/*
 * @enum  _buffer_source_t
 * @brief  The enumeration about BufferStatus
 */
enum buffer_source_t {
	BUFFER_SOURCE_INVALID,		/* BUFFER_SOURCE_INVALID */
	BUFFER_SOURCE_CMD_CAPTURE,	/* The buffer is from a capture command */
	BUFFER_SOURCE_STREAM,		/* The buffer is from the stream buffer queue */
	BUFFER_SOURCE_TEMP,		/* BUFFER_SOURCE_TEMP */
	BUFFER_SOURCE_MAX		/* BUFFER_SOURCE_MAX */
};

/*
 * @struct mipi_crc struct mipi_crc
 */
/*
 * @struct mipi_crc
 * @brief  The Meta info crc
 */
struct mipi_crc {
	u32 crc[8]; /* crc */
};

/*
 * @struct ch_crop_win_based_on_acq_t
 * @brief  The ch_crop_win_based_on_acq_t
 */
struct ch_crop_win_based_on_acq_t {
	struct window_t window;/* based on Acq window */
};

/*
 * @struct buffer_meta_info_t struct buffer_meta_info_t
 */
/*
 * @struct buffer_meta_info_t
 * @brief  The Meta info crc
 */
struct buffer_meta_info_t {
	u32 enabled;					/* enabled flag */
	enum buffer_status_t status;			/* BufferStatus */
	struct error_code err;				/* err code */
	enum buffer_source_t source;			/* BufferSource */
	struct image_prop_t image_prop;			/* image_prop */
	struct buffer_t buffer;				/* buffer */
	struct mipi_crc wdma_crc;			/* wdma_crc */
	struct ch_crop_win_based_on_acq_t crop_win_acq;	/* crop_win_acq */
};

struct byrp_crc {
	u32 rdma_crc; /* rdma input crc */
	u32 wdma_crc; /* wdma output crc */
};

struct mcsc_crc {
	/* wdma 1P/2P crc for
	 * output0 - Preview
	 * output1 - video
	 * output2 - still
	 */
	u32 wdma1_pcrc[MAX_OUTPUT_MCSC];
	u32 wdma2_pcrc[MAX_OUTPUT_MCSC];
};

struct gdc_crc {
	u32 rdma_ycrc;    /* rdma crc of input Y plane. */
	u32 rdma_uv_crc;  /* rdma crc of input UV plane. */
	u32 wdma1_pcrc;   /* wdma crc of output Y plane. */
	u32 wdma2_pcrc;   /* wdma crc of output UV plane. */
};

struct lme_crc {
	/* only WDMA related RTL logic found, for RDMA only SEED is configured. */
	u32 sps_mv_out_crc; /* wdma sub pixel search motion vector crc */
	u32 sad_out_crc;    /* wdma sad crc */
	u32 mbmv_out_crc;   /* wdma mbmv crc */

};

struct rgbp_crc {
	u32 rdma_rep_rgb_even_crc;   /* rdma input crc */
	u32 wdma_ycrc;               /* wdma y plane crc */
	u32 wdma_uv_crc;             /* wdma UV plane crc */
};

struct yuvp_crc {
	u32 rdma_ycrc;     /* rdma crc of input Y plane. */
	u32 rdma_uv_crc;   /* rdma crc of input UV plane. */
	u32 rdma_seg_crc;  /* rdma crc of segmentation. */
	u32 rdma_drc_crc;  /* rdma crc of Drc */
	u32 rdma_drc1_crc; /* rdma crc of Drc1 */
	u32 wdma_ycrc;     /* wdma crc of output Y plane. */
	u32 wdma_uv_crc;   /* wdma crc of output UV plane. */
};

struct mcfp_crc {
	u32 rdma_curr_ycrc;  /* rdma crc of curr input Y plane. */
	u32 rdma_curr_uv_crc;/* rdma crc of curr input UV plane. */
	u32 rdma_prev_ycrc;  /* rdma crc of prev input Y plane. */
	u32 rdma_prev_uv_crc;/* rdma crc of prev input UV plane. */
	u32 wdma_curr_ycrc;  /* wdma crc of curr output Y plane. */
	u32 wdma_curr_uv_crc;/* wdma crc of curr output Uv plane. */
	u32 wdma_prev_ycrc;  /* wdma crc of prev output Y plane. */
	u32 wdma_prev_uv_crc;/* wdma crc of prev output UV plane. */
};

struct cstat_crc {
	u32 rdma_byr_in_crc;   /* rdma crc of input bayer. */
	u32 wdma_rgb_hist_crc; /* wdma crc of rgb histogram */
	u32 wdma_thstat_pre;   /* wdma crc of TH stat Pre */
	u32 wdma_thstat_awb;   /* wdma crc of TH stat Awb */
	u32 wdma_thstat_ae;    /* wdma crc of TH stat Ae */
	u32 wdma_drc_grid;     /* wdma crc of Drc grid */
	u32 wdma_lme_ds0;      /* wdma crc of lme down scaler0 */
	u32 wdma_lme_ds1;      /* wdma crc of lme down scaler1 */
	u32 wdma_fdpig;        /* wdma crc of FD pre img generator */
	u32 wdma_cds0;         /* wdma crc of scene detect scaler */
};

struct pdp_crc {
	u32 rdma_afcrc;        /* rdma crc of AF */
	u32 wdma_stat_crc;     /* wdma crc of stat */
};

/*
 * @struct usr_ctrlmeta_info_t struct usr_ctrlmeta_info_t
 */
struct usr_ctrlmeta_info_t {
	u32 brightness;      /* The brightness value. */
	u32 contrast;        /* The contrast value */
	u32 saturation;      /* The saturation value */
	u32 hue;             /* The hue value */
};

/*
 * @struct guid_t
 * The Definition of SecureBIO secure buffer GUID structure
 */
struct secure_buf_guid_t {
	u32 guid_data1;        /* GUID1 */
	u16 guid_data2;        /* GUID2 */
	u16 guid_data3;        /* GUID3 */
	u8  guid_data4[8];     /* GUID4 */
};

/*
 * @struct meta_info_secure_t
 * @brief  The meta_info_secure_t
 */

struct meta_info_secure_t {
	u32 b_is_secure;		/* is secure frame */
	struct secure_buf_guid_t guid;	/* guid of the frame */
};

/*
 * @struct meta_info_t
 * @brief  The MetaInfo
 */
struct meta_info_t {
	u32 poc;					/* frame id */
	u32 fc_id;					/* frame ctl id */
	u32 time_stamp_lo;				/* time_stamp_lo */
	u32 time_stamp_hi;				/* time_stamp_hi */
	struct buffer_meta_info_t preview;		/* preview BufferMetaInfo */
	struct buffer_meta_info_t video;		/* video BufferMetaInfo */
	struct buffer_meta_info_t still;		/* yuv zsl BufferMetaInfo */
	struct buffer_meta_info_t full_still;		/* full_still zsl BufferMetaInfo; */
	struct buffer_meta_info_t raw;			/* x86 raw */
	struct buffer_meta_info_t raw_mipi;		/* raw mipi */
	struct buffer_meta_info_t raw_mipi_short_expo;	/* DolHDR short exposure raw mipi */
	struct buffer_meta_info_t metadata;		/* Host Camera Metadata */
	struct buffer_meta_info_t lme_mv0;		/* Lme Mv0 */
	struct buffer_meta_info_t lme_mv1;		/* Lme Mv1 */
	struct buffer_meta_info_t lme_wdma;		/* Lme Wdma */
	struct buffer_meta_info_t lme_sad;		/* Lme Sad */
	struct buffer_meta_info_t cstatds;		/* Cstat Downscaler */
	enum raw_pkt_fmt_t raw_pkt_fmt;			/* The raw buffer packet format if the raw is exist */
	struct byrp_crc byrp_crc;			/* byrp crc */
	struct mcsc_crc mcsc_crc;			/* mcsc crc */
	struct gdc_crc gdc_crc;				/* gdc crc */
	struct lme_crc lme_crc;				/* lme crc */
	struct rgbp_crc rgbp_crc;			/* rgbp crc */
	struct yuvp_crc yuvp_crc;			/* yuvp crc */
	struct mcfp_crc mcfp_crc;			/* mcfp crc */
	struct cstat_crc cstat_crc;			/* cstat crc */
	struct pdp_crc pdp_crc;				/* pdp crc */
	struct mipi_crc mipi_crc;			/* mipi crc */
	u32 is_still_cfm;				/* is_still_cfm, flag to indicate
							 * if the image in preview buffer is still
							 * confirmation image, The value is only valid
							 * for response of capture still
							 */
	struct irmeta_info_t i_rmeta;			/* i_rmetadata */
	struct usr_ctrlmeta_info_t ctrls;		/* user ctrls */
	struct buffer_meta_info_t byrp_tap_out;		/* Byrp tapout BufferMetaInfo */
	struct buffer_meta_info_t rgbp_tap_out;		/* Rgbp tapout BufferMetaInfo */
	struct buffer_meta_info_t mcfp_tap_out;		/* mcfp tapout BufferMetaInfo */
	struct buffer_meta_info_t yuvp_tap_out;		/* yuvp tapout BufferMetaInfo */
	struct buffer_meta_info_t yuvp_tap_in_seg_conf;	/* yuvp tapin SingleBufferMetaInfo */
	struct buffer_meta_info_t mcsc_tap_out;		/* mcsc tapout BufferMetaInfo */
	struct buffer_meta_info_t cds;			/* Cstat cds BufferMetaInfo */
	struct buffer_meta_info_t fdpig;		/* Cstat fdpig BufferMetaInfo */
	struct meta_info_secure_t secure_meta;		/* secure meta */
};

#endif
