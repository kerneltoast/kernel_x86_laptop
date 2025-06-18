/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2025 Advanced Micro Devices, Inc.
 */

#ifndef _ISP4_HW_H_
#define _ISP4_HW_H_

#define RREG_FAILED_VAL 0xFFFFFFFF

u32 isp4hw_rreg(void __iomem *base, u32 reg);
void isp4hw_wreg(void __iomem *base, u32 reg, u32 val);

#endif
