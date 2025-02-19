/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2025 Advanced Micro Devices, Inc. All rights reserved.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 */

#include <linux/acpi.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <linux/gpio/consumer.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-cci.h>

/* Chip ID */
#define OV05_REG_CHIP_ID		CCI_REG24(0x00)
#define OV05_CHIP_ID			0x430556

/* Control registers */
#define OV05_REG_PAGE_CTL		CCI_REG8(0xFD)
#define OV05_REG_TRIGGER		CCI_REG8(0x01)

/* V_TIMING internal */
#define OV05_REG_VTS			CCI_REG16(0x05)
/* Cross check these VTS values */
#define OV05_VTS_30FPS			0x1388
#define OV05_VTS_BIN_30FPS		0x115c
#define OV05_VTS_MAX			0x7fff

/* H TIMING internal */
#define OV05_REG_HTS			CCI_REG16(0x37)
#define OV05_HTS_30FPS			0x0280

/* Exposure control */
#define OV05_REG_EXPOSURE		CCI_REG24(0x02)
#define OV05_EXPOSURE_MAX_MARGIN	33
#define OV05_EXPOSURE_MIN		4
#define OV05_EXPOSURE_STEP		1
#define OV05_EXPOSURE_DEFAULT		0x40

/* Short Exposure control */
//#define OV05_REG_SHORT_EXPOSURE	CCI_REG24(0x3540)

/* Analog gain control */
#define OV05_REG_ANALOG_GAIN		CCI_REG8(0x24)
#define OV05_ANA_GAIN_MIN		0x80
#define OV05_ANA_GAIN_MAX		0x07c0
#define OV05_ANA_GAIN_STEP		1
#define OV05_ANA_GAIN_DEFAULT		0x80

/* Digital gain control */
#define OV05_REG_DGTL_GAIN_H		CCI_REG8(0x21)
#define OV05_REG_DGTL_GAIN_L		CCI_REG8(0x22)

#define OV05_DGTL_GAIN_MIN		64	     /* Min = 1 X */
#define OV05_DGTL_GAIN_MAX		(256 - 1)   /* Max = 4 X */
#define OV05_DGTL_GAIN_DEFAULT		0x80	     /* Default gain = 2x */
#define OV05_DGTL_GAIN_STEP		1            /* Each step = 1/64 */

#define OV05_DGTL_GAIN_L_MASK		0xFF
#define OV05_DGTL_GAIN_H_SHIFT		8
#define OV05_DGTL_GAIN_H_MASK		0xFF00

/* Test Pattern Control */
#define OV05_REG_TEST_PATTERN_CTL	CCI_REG8(0xF3)
#define OV05_REG_TEST_PATTERN		CCI_REG8(0x12)
#define OV05_TEST_PATTERN_ENABLE	BIT(0)

/* Flip Control */
#define OV05_REG_FLIP			CCI_REG8(0x32)

#define NUM_OF_PADS 3

#define TIMEOUT_MIN_US 25
#define TIMEOUT_MAX_US 250

#define IO_OFFSET		0  /* from amdisp-pinctrl base */
#define IO_ENABLE_SENSOR	0

enum {
	OV05_LINK_FREQ_24MHZ_INDEX,
};

struct ov05_reg_list {
	u32 num_of_regs;
	const struct cci_reg_sequence *regs;
};

/* Link frequency config */
struct ov05_link_freq_config {
	/* registers for this link frequency */
	struct ov05_reg_list reg_list;
};

/* Mode : resolution and related config&values */
struct ov05_mode {
	/* Frame width */
	u32 width;
	/* Frame height */
	u32 height;

	u32 lanes;
	/* V-timing */
	u32 vts_def;
	u32 vts_min;

	/* HTS */
	u32 hts;

	/* Index of Link frequency config to be used */
	u32 link_freq_index;

	/* Default register values */
	struct ov05_reg_list reg_list;

};

static const struct cci_reg_sequence
	mode_2888_1808_30fps_1800mbps_2lane_24mhz_regs[] = {
	{ CCI_REG8(0xfd),  0x00 },
	{ CCI_REG8(0x20),  0x00 },
	{ CCI_REG8(0xfd),  0x00 },
	{ CCI_REG8(0x20),  0x0b },
	{ CCI_REG8(0xc1),  0x09 },
	{ CCI_REG8(0x21),  0x06 },
	{ CCI_REG8(0x14),  0x78 },
	{ CCI_REG8(0xe7),  0x03 },
	{ CCI_REG8(0xe7),  0x00 },
	{ CCI_REG8(0x21),  0x00 },
	{ CCI_REG8(0xfd),  0x01 },
	{ CCI_REG8(0x03),  0x00 },
	{ CCI_REG8(0x04),  0x06 },
	{ CCI_REG8(0x05),  0x07 },
	{ CCI_REG8(0x06),  0x44 },
	{ CCI_REG8(0x07),  0x08 },
	{ CCI_REG8(0x1b),  0x01 },
	{ CCI_REG8(0x24),  0xff },
	{ CCI_REG8(0x32),  0x03 },
	{ CCI_REG8(0x42),  0x5d },
	{ CCI_REG8(0x43),  0x08 },
	{ CCI_REG8(0x44),  0x81 },
	{ CCI_REG8(0x46),  0x5f },
	{ CCI_REG8(0x48),  0x18 },
	{ CCI_REG8(0x49),  0x04 },
	{ CCI_REG8(0x5c),  0x18 },
	{ CCI_REG8(0x5e),  0x13 },
	{ CCI_REG8(0x70),  0x15 },
	{ CCI_REG8(0x77),  0x35 },
	{ CCI_REG8(0x79),  0x00 },
	{ CCI_REG8(0x7b),  0x08 },
	{ CCI_REG8(0x7d),  0x08 },
	{ CCI_REG8(0x7e),  0x08 },
	{ CCI_REG8(0x7f),  0x08 },
	{ CCI_REG8(0x90),  0x37 },
	{ CCI_REG8(0x91),  0x05 },
	{ CCI_REG8(0x92),  0x18 },
	{ CCI_REG8(0x93),  0x27 },
	{ CCI_REG8(0x94),  0x05 },
	{ CCI_REG8(0x95),  0x38 },
	{ CCI_REG8(0x9b),  0x00 },
	{ CCI_REG8(0x9c),  0x06 },
	{ CCI_REG8(0x9d),  0x28 },
	{ CCI_REG8(0x9e),  0x06 },
	{ CCI_REG8(0xb2),  0x0f },
	{ CCI_REG8(0xb3),  0x29 },
	{ CCI_REG8(0xbf),  0x3c },
	{ CCI_REG8(0xc2),  0x04 },
	{ CCI_REG8(0xc4),  0x00 },
	{ CCI_REG8(0xca),  0x20 },
	{ CCI_REG8(0xcb),  0x20 },
	{ CCI_REG8(0xcc),  0x28 },
	{ CCI_REG8(0xcd),  0x28 },
	{ CCI_REG8(0xce),  0x20 },
	{ CCI_REG8(0xcf),  0x20 },
	{ CCI_REG8(0xd0),  0x2a },
	{ CCI_REG8(0xd1),  0x2a },
	{ CCI_REG8(0xfd),  0x0f },
	{ CCI_REG8(0x00),  0x00 },
	{ CCI_REG8(0x01),  0xa0 },
	{ CCI_REG8(0x02),  0x48 },
	{ CCI_REG8(0x07),  0x8f },
	{ CCI_REG8(0x08),  0x70 },
	{ CCI_REG8(0x09),  0x01 },
	{ CCI_REG8(0x0b),  0x40 },
	{ CCI_REG8(0x0d),  0x07 },
	{ CCI_REG8(0x11),  0x33 },
	{ CCI_REG8(0x12),  0x77 },
	{ CCI_REG8(0x13),  0x66 },
	{ CCI_REG8(0x14),  0x65 },
	{ CCI_REG8(0x15),  0x37 },
	{ CCI_REG8(0x16),  0xbf },
	{ CCI_REG8(0x17),  0xff },
	{ CCI_REG8(0x18),  0xff },
	{ CCI_REG8(0x19),  0x12 },
	{ CCI_REG8(0x1a),  0x10 },
	{ CCI_REG8(0x1c),  0x77 },
	{ CCI_REG8(0x1d),  0x77 },
	{ CCI_REG8(0x20),  0x0f },
	{ CCI_REG8(0x21),  0x0f },
	{ CCI_REG8(0x22),  0x0f },
	{ CCI_REG8(0x23),  0x0f },
	{ CCI_REG8(0x2b),  0x20 },
	{ CCI_REG8(0x2c),  0x20 },
	{ CCI_REG8(0x2d),  0x04 },
	{ CCI_REG8(0xfd),  0x03 },
	{ CCI_REG8(0x9d),  0x0f },
	{ CCI_REG8(0x9f),  0x40 },
	{ CCI_REG8(0xfd),  0x00 },
	{ CCI_REG8(0x20),  0x1b },
	{ CCI_REG8(0xfd),  0x04 },
	{ CCI_REG8(0x19),  0x60 },
	{ CCI_REG8(0xfd),  0x02 },
	{ CCI_REG8(0x75),  0x05 },
	{ CCI_REG8(0x7f),  0x06 },
	{ CCI_REG8(0x9a),  0x03 },
	{ CCI_REG8(0xa2),  0x07 },
	{ CCI_REG8(0xa3),  0x10 },
	{ CCI_REG8(0xa5),  0x02 },
	{ CCI_REG8(0xa6),  0x0b },
	{ CCI_REG8(0xa7),  0x48 },
	{ CCI_REG8(0xfd),  0x07 },
	{ CCI_REG8(0x42),  0x00 },
	{ CCI_REG8(0x43),  0x80 },
	{ CCI_REG8(0x44),  0x00 },
	{ CCI_REG8(0x45),  0x80 },
	{ CCI_REG8(0x46),  0x00 },
	{ CCI_REG8(0x47),  0x80 },
	{ CCI_REG8(0x48),  0x00 },
	{ CCI_REG8(0x49),  0x80 },
	{ CCI_REG8(0x00),  0xf7 },
	{ CCI_REG8(0xfd),  0x00 },
	{ CCI_REG8(0xe7),  0x03 },
	{ CCI_REG8(0xe7),  0x00 },
	{ CCI_REG8(0xfd),  0x00 },
	{ CCI_REG8(0x93),  0x18 },
	{ CCI_REG8(0x94),  0xff },
	{ CCI_REG8(0x95),  0xbd },
	{ CCI_REG8(0x96),  0x1a },
	{ CCI_REG8(0x98),  0x04 },
	{ CCI_REG8(0x99),  0x08 },
	{ CCI_REG8(0x9b),  0x10 },
	{ CCI_REG8(0x9c),  0x3f },
	{ CCI_REG8(0xa1),  0x05 },
	{ CCI_REG8(0xa4),  0x2f },
	{ CCI_REG8(0xc0),  0x0c },
	{ CCI_REG8(0xc1),  0x08 },
	{ CCI_REG8(0xc2),  0x00 },
	{ CCI_REG8(0xb6),  0x20 },
	{ CCI_REG8(0xbb),  0x80 },
	{ CCI_REG8(0xfd),  0x00 },
	{ CCI_REG8(0xa0),  0x01 },
	{ CCI_REG8(0xfd),  0x01 },
};

static const struct cci_reg_sequence
	mode_2888_1808_hdr_30fps_1800mbps_2lane_24mhz_regs[] = {
	{ CCI_REG8(0xfd),  0x00 },
	{ CCI_REG8(0x20),  0x00 },
	{ CCI_REG8(0xfd),  0x00 },
	{ CCI_REG8(0x20),  0x0b },
	{ CCI_REG8(0xc1),  0x09 },
	{ CCI_REG8(0x21),  0x06 },
	{ CCI_REG8(0x14),  0x78 },
	{ CCI_REG8(0xe7),  0x03 },
	{ CCI_REG8(0xe7),  0x00 },
	{ CCI_REG8(0x21),  0x00 },
	{ CCI_REG8(0xfd),  0x01 },
	{ CCI_REG8(0x03),  0x00 },
	{ CCI_REG8(0x04),  0x06 },
	{ CCI_REG8(0x07),  0x08 },
	{ CCI_REG8(0x1b),  0x01 },
	{ CCI_REG8(0x24),  0xff },
	{ CCI_REG8(0x31),  0x20 },
	{ CCI_REG8(0x32),  0x03 },
	{ CCI_REG8(0x42),  0x5d },
	{ CCI_REG8(0x43),  0x08 },
	{ CCI_REG8(0x44),  0x81 },
	{ CCI_REG8(0x46),  0x5f },
	{ CCI_REG8(0x48),  0x18 },
	{ CCI_REG8(0x49),  0x04 },
	{ CCI_REG8(0x4f),  0x06 },
	{ CCI_REG8(0x5c),  0x18 },
	{ CCI_REG8(0x5e),  0x13 },
	{ CCI_REG8(0x70),  0x15 },
	{ CCI_REG8(0x77),  0x35 },
	{ CCI_REG8(0x79),  0x00 },
	{ CCI_REG8(0x7b),  0x08 },
	{ CCI_REG8(0x7d),  0x08 },
	{ CCI_REG8(0x7e),  0x08 },
	{ CCI_REG8(0x7f),  0x08 },
	{ CCI_REG8(0x90),  0x37 },
	{ CCI_REG8(0x91),  0x05 },
	{ CCI_REG8(0x92),  0x18 },
	{ CCI_REG8(0x93),  0x27 },
	{ CCI_REG8(0x94),  0x05 },
	{ CCI_REG8(0x95),  0x38 },
	{ CCI_REG8(0x9b),  0x00 },
	{ CCI_REG8(0x9c),  0x06 },
	{ CCI_REG8(0x9d),  0x28 },
	{ CCI_REG8(0x9e),  0x06 },
	{ CCI_REG8(0xb2),  0x0f },
	{ CCI_REG8(0xb3),  0x29 },
	{ CCI_REG8(0xbf),  0x3c },
	{ CCI_REG8(0xc2),  0x04 },
	{ CCI_REG8(0xc4),  0x00 },
	{ CCI_REG8(0xca),  0x20 },
	{ CCI_REG8(0xcb),  0x20 },
	{ CCI_REG8(0xcc),  0x28 },
	{ CCI_REG8(0xcd),  0x28 },
	{ CCI_REG8(0xce),  0x20 },
	{ CCI_REG8(0xcf),  0x20 },
	{ CCI_REG8(0xd0),  0x2a },
	{ CCI_REG8(0xd1),  0x2a },
	{ CCI_REG8(0xfd),  0x0f },
	{ CCI_REG8(0x00),  0x00 },
	{ CCI_REG8(0x01),  0xa0 },
	{ CCI_REG8(0x02),  0x48 },
	{ CCI_REG8(0x07),  0x8f },
	{ CCI_REG8(0x08),  0x70 },
	{ CCI_REG8(0x09),  0x01 },
	{ CCI_REG8(0x0b),  0x40 },
	{ CCI_REG8(0x0d),  0x07 },
	{ CCI_REG8(0x11),  0x33 },
	{ CCI_REG8(0x12),  0x77 },
	{ CCI_REG8(0x13),  0x66 },
	{ CCI_REG8(0x14),  0x65 },
	{ CCI_REG8(0x15),  0x37 },
	{ CCI_REG8(0x16),  0xbf },
	{ CCI_REG8(0x17),  0xff },
	{ CCI_REG8(0x18),  0xff },
	{ CCI_REG8(0x19),  0x12 },
	{ CCI_REG8(0x1a),  0x10 },
	{ CCI_REG8(0x1c),  0x77 },
	{ CCI_REG8(0x1d),  0x77 },
	{ CCI_REG8(0x20),  0x0f },
	{ CCI_REG8(0x21),  0x0f },
	{ CCI_REG8(0x22),  0x0f },
	{ CCI_REG8(0x23),  0x0f },
	{ CCI_REG8(0x2b),  0x20 },
	{ CCI_REG8(0x2c),  0x20 },
	{ CCI_REG8(0x2d),  0x04 },
	{ CCI_REG8(0xfd),  0x03 },
	{ CCI_REG8(0x9d),  0x0f },
	{ CCI_REG8(0x9f),  0x40 },
	{ CCI_REG8(0xfd),  0x00 },
	{ CCI_REG8(0x20),  0x1b },
	{ CCI_REG8(0xfd),  0x04 },
	{ CCI_REG8(0x19),  0x60 },
	{ CCI_REG8(0xfd),  0x02 },
	{ CCI_REG8(0x75),  0x05 },
	{ CCI_REG8(0x7f),  0x06 },
	{ CCI_REG8(0x9a),  0x03 },
	{ CCI_REG8(0xa2),  0x07 },
	{ CCI_REG8(0xa3),  0x10 },
	{ CCI_REG8(0xa5),  0x02 },
	{ CCI_REG8(0xa6),  0x0b },
	{ CCI_REG8(0xa7),  0x48 },
	{ CCI_REG8(0xfd),  0x07 },
	{ CCI_REG8(0x42),  0x00 },
	{ CCI_REG8(0x43),  0x80 },
	{ CCI_REG8(0x44),  0x00 },
	{ CCI_REG8(0x45),  0x80 },
	{ CCI_REG8(0x46),  0x00 },
	{ CCI_REG8(0x47),  0x80 },
	{ CCI_REG8(0x48),  0x00 },
	{ CCI_REG8(0x49),  0x80 },
	{ CCI_REG8(0x00),  0xf7 },
	{ CCI_REG8(0xfd),  0x00 },
	{ CCI_REG8(0xe7),  0x03 },
	{ CCI_REG8(0xe7),  0x00 },
	{ CCI_REG8(0xfd),  0x00 },
	{ CCI_REG8(0x93),  0x18 },
	{ CCI_REG8(0x94),  0xff },
	{ CCI_REG8(0x95),  0xbd },
	{ CCI_REG8(0x96),  0x1a },
	{ CCI_REG8(0x98),  0x04 },
	{ CCI_REG8(0x99),  0x08 },
	{ CCI_REG8(0x9b),  0x10 },
	{ CCI_REG8(0x9c),  0x3f },
	{ CCI_REG8(0xa1),  0x05 },
	{ CCI_REG8(0xa4),  0x2f },
	{ CCI_REG8(0xc0),  0x0c },
	{ CCI_REG8(0xc1),  0x08 },
	{ CCI_REG8(0xc2),  0x00 },
	{ CCI_REG8(0xb6),  0x20 },
	{ CCI_REG8(0xbb),  0x80 },
	{ CCI_REG8(0xfd),  0x00 },
	{ CCI_REG8(0xa0),  0x01 },
	{ CCI_REG8(0xfd),  0x01 },
};

static const struct cci_reg_sequence mode_OV05C_stream_on_regs[] = {
	{ CCI_REG8(0xfd), 0x01 },
	{ CCI_REG8(0x33), 0x03 },
	{ CCI_REG8(0x01), 0x02 },
	{ CCI_REG8(0xfd), 0x00 },
	{ CCI_REG8(0x20), 0x1f },
	{ CCI_REG8(0xfd), 0x01 },
};

static const struct cci_reg_sequence mode_OV05C_stream_off_regs[] = {
	{ CCI_REG8(0xfd), 0x00 },
	{ CCI_REG8(0x20), 0x5b },
	{ CCI_REG8(0xfd), 0x01 },
	{ CCI_REG8(0x33), 0x02 },
	{ CCI_REG8(0x01), 0x02 },
};

static const char * const ov05_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

/* Configurations for supported link frequencies */
#define OV05_LINK_FREQ_24MHZ	24000000ULL

#define OV05_DATA_LANES		2

/*
 * pixel_rate = link_freq * data-rate * nr_of_lanes / bits_per_sample
 * data rate => double data rate; number of lanes => 2; bits per pixel => 10
 */
static u64 link_freq_to_pixel_rate(u64 f)
{
	f *= 2 * OV05_DATA_LANES;
	do_div(f, 10);

	return f;
}

/* Menu items for LINK_FREQ V4L2 control */
static const s64 link_freq_menu_items[] = {
	OV05_LINK_FREQ_24MHZ,
};

/* Mode configs */
static const struct ov05_mode supported_modes[] = {
	{
		.width = 2888,
		.height = 1808,
		.vts_def = OV05_VTS_30FPS,
		.vts_min = OV05_VTS_30FPS,
		.hts = 640,
		.lanes = 2,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_2888_1808_30fps_1800mbps_2lane_24mhz_regs),
			.regs = mode_2888_1808_30fps_1800mbps_2lane_24mhz_regs,
		},
		.link_freq_index = OV05_LINK_FREQ_24MHZ_INDEX,
	},
	{
		.width = 2888,
		.height = 1808,
		.vts_def = OV05_VTS_30FPS,
		.vts_min = OV05_VTS_30FPS,
		.hts = 640,
		.lanes = 2,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_2888_1808_hdr_30fps_1800mbps_2lane_24mhz_regs),
			.regs = mode_2888_1808_hdr_30fps_1800mbps_2lane_24mhz_regs,
		},
		.link_freq_index = OV05_LINK_FREQ_24MHZ_INDEX,
	},
};

struct ov05 {
	struct v4l2_subdev sd;
	struct media_pad pads[NUM_OF_PADS];

	struct v4l2_ctrl_handler ctrl_handler;
	/* V4L2 Controls */
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *exposure;

	/* Current mode */
	const struct ov05_mode *cur_mode;

	struct regmap *regmap;

	/* Mutex for serialized access */
	struct mutex mutex;

	/* gpio descriptor */
	struct gpio_desc *enable_gpio;
};

#define to_ov05(_sd)	container_of(_sd, struct ov05, sd)

static int ov05_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	const struct ov05_mode *default_mode = &supported_modes[0];
	struct ov05 *ov05c = to_ov05(sd);
	struct v4l2_mbus_framefmt *try_fmt =
		v4l2_subdev_state_get_format(fh->state, 0);

	mutex_lock(&ov05c->mutex);

	/* Initialize try_fmt */
	try_fmt->width = default_mode->width;
	try_fmt->height = default_mode->height;
	try_fmt->code = MEDIA_BUS_FMT_SGRBG10_1X10;
	try_fmt->field = V4L2_FIELD_NONE;

	/* No crop or compose */
	mutex_unlock(&ov05c->mutex);

	return 0;
}

static int ov05_update_cid_vblank(struct ov05 *ov05c,
				  u32 vblank)
{
	int ret = 0;

	cci_write(ov05c->regmap, OV05_REG_PAGE_CTL,
		  BIT(0), &ret);
	if (ret)
		return ret;

	cci_write(ov05c->regmap, OV05_REG_VTS,
		  ov05c->cur_mode->height
		  + vblank, &ret);
	if (ret)
		return ret;

	cci_write(ov05c->regmap, OV05_REG_TRIGGER,
		  BIT(0), &ret);

	return ret;
}

static int ov05_update_cid_exposure(struct ov05 *ov05c,
				    u32 exposure)
{
	int ret = 0;

	cci_write(ov05c->regmap, OV05_REG_PAGE_CTL,
		  BIT(0), &ret);
	if (ret)
		return ret;

	cci_write(ov05c->regmap, OV05_REG_EXPOSURE,
		  exposure, &ret);
	if (ret)
		return ret;

	cci_write(ov05c->regmap, OV05_REG_TRIGGER,
		  BIT(0), &ret);

	return ret;
}

static int ov05_update_analog_gain(struct ov05 *ov05c,
				   u32 a_gain)
{
	int ret = 0;

	cci_write(ov05c->regmap, OV05_REG_PAGE_CTL,
		  BIT(0), &ret);
	if (ret)
		return ret;

	cci_write(ov05c->regmap, OV05_REG_ANALOG_GAIN,
		  a_gain, &ret);
	if (ret)
		return ret;

	cci_write(ov05c->regmap, OV05_REG_TRIGGER,
		  BIT(0), &ret);

	return ret;
}

static int ov05_update_digital_gain(struct ov05 *ov05c,
				    u32 d_gain)
{
	int ret = 0;
	u64 val;

	/*
	 * 0x21[15:8], 0x22[7:0]
	 */

	cci_write(ov05c->regmap, OV05_REG_PAGE_CTL,
		  BIT(0), &ret);
	if (ret)
		return ret;

	val = d_gain & OV05_DGTL_GAIN_L_MASK;
	cci_write(ov05c->regmap, OV05_REG_DGTL_GAIN_L,
		  val, &ret);
	if (ret)
		return ret;

	val = (d_gain & OV05_DGTL_GAIN_H_MASK) >> OV05_DGTL_GAIN_H_SHIFT;

	cci_write(ov05c->regmap, OV05_REG_DGTL_GAIN_H,
		  val, &ret);

	cci_write(ov05c->regmap, OV05_REG_TRIGGER,
		  BIT(0), &ret);
	if (ret)
		return ret;

	return ret;
}

static int ov05_enable_test_pattern(struct ov05 *ov05c,
				    u32 pattern)
{
	int ret;
	u64 val;

	cci_write(ov05c->regmap, OV05_REG_PAGE_CTL,
		  BIT(2), &ret);
	if (ret)
		return ret;

	ret = cci_read(ov05c->regmap, OV05_REG_TEST_PATTERN, &val, NULL);
	if (ret)
		return ret;

	if (pattern) {
		ret = cci_read(ov05c->regmap, OV05_REG_TEST_PATTERN_CTL, &val, NULL);
		if (ret)
			return ret;

		cci_write(ov05c->regmap, OV05_REG_TEST_PATTERN_CTL, val | BIT(1), &ret);
		if (ret)
			return ret;

		ret = cci_read(ov05c->regmap, OV05_REG_TEST_PATTERN, &val, NULL);
		if (ret)
			return ret;

		val &= OV05_TEST_PATTERN_ENABLE;
	} else {
		val &= ~OV05_TEST_PATTERN_ENABLE;
	}

	cci_write(ov05c->regmap, OV05_REG_TEST_PATTERN, val, &ret);
	if (ret)
		return ret;

	cci_write(ov05c->regmap, OV05_REG_TRIGGER,
		  BIT(0), &ret);

	return ret;
}

static int ov05_set_ctrl_hflip(struct ov05 *ov05c, u32 ctrl_val)
{
	int ret;
	u64 val;

	ret = cci_read(ov05c->regmap, OV05_REG_FLIP, &val, NULL);
	if (ret)
		return ret;

	cci_write(ov05c->regmap, OV05_REG_FLIP,
		  ctrl_val ? val | BIT(0) : val & ~BIT(0), &ret);
	return ret;
}

static int ov05_set_ctrl_vflip(struct ov05 *ov05c, u32 ctrl_val)
{
	int ret;
	u64 val;

	ret = cci_read(ov05c->regmap, OV05_REG_FLIP, &val, NULL);
	if (ret)
		return ret;

	cci_write(ov05c->regmap, OV05_REG_FLIP,
		  ctrl_val ? val | BIT(1) : val & ~BIT(1), &ret);
	return ret;
}

static int ov05_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov05 *ov05c = container_of(ctrl->handler,
					  struct ov05, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&ov05c->sd);
	s64 max;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = ov05c->cur_mode->height + ctrl->val - OV05_EXPOSURE_MAX_MARGIN;
		__v4l2_ctrl_modify_range(ov05c->exposure,
					 ov05c->exposure->minimum,
					 max, ov05c->exposure->step, max);
		break;
	}

	/*
	 * Applying V4L2 control value only happens
	 * when power is up for streaming
	 */
	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		ret = ov05_update_analog_gain(ov05c, ctrl->val);
		break;
	case V4L2_CID_DIGITAL_GAIN:
		ret = ov05_update_digital_gain(ov05c, ctrl->val);
		break;
	case V4L2_CID_EXPOSURE:
		ret = ov05_update_cid_exposure(ov05c, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		ret = ov05_update_cid_vblank(ov05c, ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = ov05_enable_test_pattern(ov05c, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		ov05_set_ctrl_hflip(ov05c, ctrl->val);
		break;
	case V4L2_CID_VFLIP:
		ov05_set_ctrl_vflip(ov05c, ctrl->val);
		break;
	default:
		dev_info(&client->dev,
			 "ctrl(id:0x%x,val:0x%x) is not handled\n",
			 ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops ov05_ctrl_ops = {
	.s_ctrl = ov05_set_ctrl,
};

static int ov05_enum_mbus_code(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *sd_state,
			       struct v4l2_subdev_mbus_code_enum *code)
{
	/* Only one bayer order(GRBG) is supported */
	if (code->index > 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SGRBG10_1X10;

	return 0;
}

static int ov05_enum_frame_size(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != MEDIA_BUS_FMT_SGRBG10_1X10)
		return -EINVAL;

	fse->min_width = supported_modes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = supported_modes[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static void ov05_update_pad_format(const struct ov05_mode *mode,
				   struct v4l2_subdev_format *fmt)
{
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.code = MEDIA_BUS_FMT_SGRBG10_1X10;
	fmt->format.field = V4L2_FIELD_NONE;
}

static int ov05_do_get_pad_format(struct ov05 *ov05c,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		framefmt = v4l2_subdev_state_get_format(sd_state, fmt->pad);
		fmt->format = *framefmt;
	} else {
		ov05_update_pad_format(ov05c->cur_mode, fmt);
	}

	return 0;
}

static int ov05_get_pad_format(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *sd_state,
			       struct v4l2_subdev_format *fmt)
{
	struct ov05 *ov05c = to_ov05(sd);
	int ret;

	mutex_lock(&ov05c->mutex);
	ret = ov05_do_get_pad_format(ov05c, sd_state, fmt);
	mutex_unlock(&ov05c->mutex);

	return ret;
}

static int
ov05_set_pad_format(struct v4l2_subdev *sd,
		    struct v4l2_subdev_state *sd_state,
		    struct v4l2_subdev_format *fmt)
{
	struct ov05 *ov05c = to_ov05(sd);
	const struct ov05_mode *mode;
	struct v4l2_mbus_framefmt *framefmt;
	s32 vblank_def;
	s32 vblank_min;
	s64 h_blank;
	s64 pixel_rate;
	s64 link_freq;

	mutex_lock(&ov05c->mutex);

	/* Only one raw bayer(GRBG) order is supported */
	if (fmt->format.code != MEDIA_BUS_FMT_SGRBG10_1X10)
		fmt->format.code = MEDIA_BUS_FMT_SGRBG10_1X10;

	mode = v4l2_find_nearest_size(supported_modes,
				      ARRAY_SIZE(supported_modes),
				      width, height,
				      fmt->format.width, fmt->format.height);
	ov05_update_pad_format(mode, fmt);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		framefmt = v4l2_subdev_state_get_format(sd_state, fmt->pad);
		*framefmt = fmt->format;
	} else {
		ov05c->cur_mode = mode;
		__v4l2_ctrl_s_ctrl(ov05c->link_freq, mode->link_freq_index);
		link_freq = link_freq_menu_items[mode->link_freq_index];
		pixel_rate = link_freq_to_pixel_rate(link_freq);
		__v4l2_ctrl_s_ctrl_int64(ov05c->pixel_rate, pixel_rate);

		/* Update limits and set FPS to default */
		vblank_def = ov05c->cur_mode->vts_def -
			     ov05c->cur_mode->height;
		vblank_min = ov05c->cur_mode->vts_min -
			     ov05c->cur_mode->height;
		__v4l2_ctrl_modify_range(ov05c->vblank, vblank_min,
					 OV05_VTS_MAX
					 - ov05c->cur_mode->height,
					 1,
					 vblank_def);
		__v4l2_ctrl_s_ctrl(ov05c->vblank, vblank_def);
		h_blank = ov05c->cur_mode->hts;
		__v4l2_ctrl_modify_range(ov05c->hblank, h_blank,
					 h_blank, 1, h_blank);
	}

	mutex_unlock(&ov05c->mutex);

	return 0;
}

static int ov05_start_streaming(struct ov05 *ov05c)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov05c->sd);
	const struct ov05_reg_list *reg_list;
	int ret = 0;

	/* Apply default values of current mode */
	reg_list = &ov05c->cur_mode->reg_list;
	cci_multi_reg_write(ov05c->regmap, reg_list->regs, reg_list->num_of_regs,
			    &ret);
	if (ret) {
		dev_err(&client->dev, "%s failed to set mode, ret: %d\n", __func__, ret);
		return ret;
	}

	/* Apply customized values from user */
	ret =  __v4l2_ctrl_handler_setup(ov05c->sd.ctrl_handler);
	if (ret)
		return ret;

	cci_multi_reg_write(ov05c->regmap, mode_OV05C_stream_on_regs,
			    ARRAY_SIZE(mode_OV05C_stream_on_regs), &ret);
	if (ret)
		dev_err(&client->dev, "%s failed to start the streaming\n", __func__);

	return ret;
}

/* Stop streaming */
static int ov05_stop_streaming(struct ov05 *ov05c)
{
	int ret = 0;
	struct i2c_client *client = v4l2_get_subdevdata(&ov05c->sd);

	cci_multi_reg_write(ov05c->regmap, mode_OV05C_stream_off_regs,
			    ARRAY_SIZE(mode_OV05C_stream_off_regs), &ret);
	if (ret)
		dev_err(&client->dev, "%s failed to stop the streaming\n", __func__);

	return ret;
}

static void ov05_sensor_enable(struct ov05 *ov05c, bool enable)
{
	if (enable) {
		gpiod_set_value(ov05c->enable_gpio, 0);
		usleep_range(10, 20);

		gpiod_set_value(ov05c->enable_gpio, 1);
		/*  The delay is to make sure the sensor is completely turned on */
		usleep_range(1000, 2000);
	} else {
		gpiod_set_value(ov05c->enable_gpio, 0);
		usleep_range(10, 20);
	}
}

static int ov05_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct ov05 *ov05c = to_ov05(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	mutex_lock(&ov05c->mutex);

	if (enable) {
		ov05_sensor_enable(ov05c, true);

		ret = pm_runtime_resume_and_get(&client->dev);
		if (ret < 0)
			goto err_unlock;

		/*
		 * Apply default & customized values
		 * and then start streaming.
		 */
		ret = ov05_start_streaming(ov05c);
		if (ret)
			goto err_rpm_put;
	} else {
		ov05_stop_streaming(ov05c);
		pm_runtime_put(&client->dev);

		ov05_sensor_enable(ov05c, false);
	}

	mutex_unlock(&ov05c->mutex);

	return ret;

err_rpm_put:
	pm_runtime_put(&client->dev);
err_unlock:
	mutex_unlock(&ov05c->mutex);

	return ret;
}

static const struct v4l2_subdev_video_ops ov05_video_ops = {
	.s_stream = ov05_set_stream,
};

static const struct v4l2_subdev_pad_ops ov05_pad_ops = {
	.enum_mbus_code = ov05_enum_mbus_code,
	.get_fmt = ov05_get_pad_format,
	.set_fmt = ov05_set_pad_format,
	.enum_frame_size = ov05_enum_frame_size,
};

static const struct v4l2_subdev_ops ov05_subdev_ops = {
	.video = &ov05_video_ops,
	.pad = &ov05_pad_ops,
};

static const struct media_entity_operations ov05_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_internal_ops ov05_internal_ops = {
	.open = ov05_open,
};

static void ov05_free_controls(struct ov05 *ov05c)
{
	v4l2_ctrl_handler_free(ov05c->sd.ctrl_handler);
	mutex_destroy(&ov05c->mutex);
}

static int ov05_probe(struct i2c_client *client)
{
	struct ov05 *ov05c;
	int i, ret;

	ov05c = devm_kzalloc(&client->dev, sizeof(*ov05c), GFP_KERNEL);
	if (!ov05c)
		return -ENOMEM;

	/* create sensor enable gpio control */
	ov05c->enable_gpio = gpio_to_desc(IO_ENABLE_SENSOR + IO_OFFSET);
	if (IS_ERR_OR_NULL(ov05c->enable_gpio))
		return PTR_ERR(ov05c->enable_gpio);
	gpiod_direction_output(ov05c->enable_gpio, 0);

	/* Initialize subdev - OK */
	v4l2_i2c_subdev_init(&ov05c->sd, client, &ov05_subdev_ops);

	ov05c->regmap = devm_cci_regmap_init_i2c(client, 8);
	if (IS_ERR(ov05c->regmap)) {
		dev_err(&client->dev, "Failed to initialize CCI\n");
		return PTR_ERR(ov05c->regmap);
	}

	/* Set default mode to max resolution */
	ov05c->cur_mode = &supported_modes[0];

	/* Initialize subdev */
	ov05c->sd.internal_ops = &ov05_internal_ops;
	ov05c->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	ov05c->sd.entity.ops = &ov05_subdev_entity_ops;
	ov05c->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ov05c->sd.entity.name = "OV05C";

	/* Initialize source pad */
	for (i = 0; i < NUM_OF_PADS; i++)
		ov05c->pads[i].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&ov05c->sd.entity, NUM_OF_PADS, ov05c->pads);
	if (ret) {
		dev_err(&client->dev, "%s failed:%d\n", __func__, ret);
		goto error_handler_free;
	}

	ret = v4l2_async_register_subdev_sensor(&ov05c->sd);
	if (ret < 0)
		goto error_media_entity;
	/*
	 * Device is already turned on by i2c-core with ACPI domain PM.
	 * Enable runtime PM and turn off the device.
	 */
	pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);
	pm_runtime_idle(&client->dev);

	return 0;

error_media_entity:
	media_entity_cleanup(&ov05c->sd.entity);

error_handler_free:
	ov05_free_controls(ov05c);

	return ret;
}

static void ov05_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov05 *ov05c = to_ov05(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	ov05_free_controls(ov05c);

	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);
}

static const struct i2c_device_id ov05_id[] = {
	{"ov05", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, ov05_id);

static struct i2c_driver ov05_i2c_driver = {
	.driver = {
		.name = "ov05",
	},
	.id_table = ov05_id,
	.probe = ov05_probe,
	.remove = ov05_remove,
};

module_i2c_driver(ov05_i2c_driver);

MODULE_ALIAS("ov05");
MODULE_DESCRIPTION("OmniVision OV05 sensor driver");
MODULE_LICENSE("GPL and additional rights");
