/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2025 Advanced Micro Devices, Inc.
 */

#ifndef _ISP4_H_
#define _ISP4_H_

#include <linux/mutex.h>
#include <media/videobuf2-memops.h>
#include <media/videobuf2-vmalloc.h>
#include "isp4_subdev.h"

#define ISP4_GET_ISP_REG_BASE(isp4sd) (((isp4sd))->mmio)

struct isp4_platform_data {
	void *adev;
	void *bo;
	void *cpu_ptr;
	u64 gpu_addr;
	u32 size;
	u32 asic_type;
	resource_size_t base_rmmio_size;
};

struct isp4_device {
	struct v4l2_device v4l2_dev;
	struct isp4_subdev isp_sdev;
	struct media_device mdev;

	struct isp4_platform_data *pltf_data;
	struct platform_device *pdev;
	struct notifier_block i2c_nb;
	struct v4l2_async_notifier notifier;
};

#endif /* isp4.h */
