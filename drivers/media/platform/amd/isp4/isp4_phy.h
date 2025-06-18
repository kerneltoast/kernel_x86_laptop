/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2025 Advanced Micro Devices, Inc.
 */

#ifndef _ISP4_PHY_H_
#define _ISP4_PHY_H_

int isp4phy_start(struct device *dev,
		  void __iomem *base, u32 phy_id, u64 bit_rate,
		  u32 lane_num);
int isp4phy_stop(void __iomem *base, u32 phy_id);

#endif
