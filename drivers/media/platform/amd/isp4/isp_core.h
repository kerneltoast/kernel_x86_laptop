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

#ifndef _ISP_CORE_H_
#define _ISP_CORE_H_

#include <linux/mutex.h>
#include <media/videobuf2-memops.h>
#include <media/videobuf2-vmalloc.h>

#include "isp_common.h"
#include "isp_module_intf.h"
#include "isp_debug.h"

/* the frame duration of min FPS 15 */
#define MAX_FRAME_DURATION	67
#define TEAR_DOWN_TIMEOUT	msecs_to_jiffies(MAX_FRAME_DURATION * \
						 MAX_REQUEST_DEPTH)

#define SENSOR_SWITCH_DISABLE	0
#define SENSOR_SWITCH_ENABLE	1
#define CLOCK_SWITCH_DISABLE	0
#define CLOCK_SWITCH_ENABLE	1

/*
 * If sensor profile switch from 12M@30FPS/3M@60FPS to other profiles, need to
 * low clock after all the previous frames are returned; set SWITCH_LOW_CLK_IDX
 * to CLOCK_SWITCH_ENABLE for the first frame control of the new profile, so
 * when kernel receive it from ISP, which shows can low clock now.
 */
#define SWITCH_LOW_CLK_IDX	15

#define RMMIO_SIZE 524288

#define ISP_NBIF_GPU_PCIE_INDEX 0xE

#define ISP_NBIF_GPU_PCIE_DATA 0xF

#define ISP_DRV_NAME "amd_isp_capture"

#define RETRY_CNT 100

#define LOGRB_SIZE  (2 * 1024 * 1024)   /* 2MB for LOG Ring Buffer */

/* SMU Response Codes */
/* Message Response OK */
#define ISPSMC_Result_OK                    0x1
/* Message Response Failed */
#define ISPSMC_Result_Failed                0xFF
/* Message Response Unknown Command */
#define ISPSMC_Result_UnknownCmd            0xFE
/* Message Response Command Failed Prerequisite */
#define ISPSMC_Result_CmdRejectedPrereq     0xFD
/*
 * Message Response Command Rejected due to PMFW is busy.
 * @note Sender should retry sending this message
 */
#define ISPSMC_Result_CmdRejectedBusy       0xFC

#define ISP_ALL_SYS_INTS_MASK     0xFFFFFFFF
#define FW_RESP_RB_IRQ_EN_MASK \
	(ISP_SYS_INT0_EN__SYS_INT_RINGBUFFER_WPT9_EN_MASK |  \
	 ISP_SYS_INT0_EN__SYS_INT_RINGBUFFER_WPT10_EN_MASK | \
	 ISP_SYS_INT0_EN__SYS_INT_RINGBUFFER_WPT11_EN_MASK | \
	 ISP_SYS_INT0_EN__SYS_INT_RINGBUFFER_WPT12_EN_MASK)
#define FW_RESP_RB_IRQ_STATUS_MASK \
	(ISP_SYS_INT0_STATUS__SYS_INT_RINGBUFFER_WPT9_INT_MASK  | \
	 ISP_SYS_INT0_STATUS__SYS_INT_RINGBUFFER_WPT10_INT_MASK | \
	 ISP_SYS_INT0_STATUS__SYS_INT_RINGBUFFER_WPT11_INT_MASK | \
	 ISP_SYS_INT0_STATUS__SYS_INT_RINGBUFFER_WPT12_INT_MASK)

#define MAX_TEST_WPT_NUM 8

enum stream_buf_type {
	STREAM_BUF_VMALLOC = 0,
	STREAM_BUF_DMA,
};

struct amdisp_platform_data {
	void *adev;
	void *bo;
	void *cpu_ptr;
	u64 gpu_addr;
	u32 size;
	u32 asic_type;
	resource_size_t base_rmmio_size;
};

struct amd_dma_buf {
	struct device *dev;
	enum dma_data_direction dma_dir;
	struct dma_buf_attachment *dbuf_attach;
	u64 dma_fd;
	atomic_t refcount;
	size_t size;
};

/* amdisp buffer for vb2 operations */
struct vb2_amdisp_buf {
	struct device			*dev;
	void				*vaddr;
	struct frame_vector		*vec;
	enum dma_data_direction		dma_dir;
	unsigned long			size;
	refcount_t			refcount;
	struct dma_buf			*dbuf;
	void				*bo;
	u64				gpu_addr;
	struct vb2_vmarea_handler	handler;
	bool				is_expbuf;
};

#endif /* isp_core.h */
