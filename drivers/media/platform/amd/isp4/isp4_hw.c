// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2025 Advanced Micro Devices, Inc.
 */

#include <linux/io.h>
#include <linux/types.h>

#include "isp4_hw.h"
#include "isp4_hw_reg.h"

#define RMMIO_SIZE 524288

u32 isp4hw_rreg(void __iomem *base, u32 reg)
{
	void __iomem *reg_addr;

	if (reg >= RMMIO_SIZE)
		return RREG_FAILED_VAL;

	if (reg < ISP_MIPI_PHY0_REG0)
		reg_addr = base + reg;
	else if (reg <= ISP_MIPI_PHY0_REG0 + ISP_MIPI_PHY0_SIZE)
		reg_addr = base + (reg - ISP_MIPI_PHY0_REG0);
	else
		return RREG_FAILED_VAL;

	return readl(reg_addr);
};

void isp4hw_wreg(void __iomem *base, u32 reg, u32 val)
{
	void __iomem *reg_addr;

	if (reg >= RMMIO_SIZE)
		return;

	if (reg < ISP_MIPI_PHY0_REG0)
		reg_addr = base + reg;
	else if (reg <= ISP_MIPI_PHY0_REG0 + ISP_MIPI_PHY0_SIZE)
		reg_addr = base + (reg - ISP_MIPI_PHY0_REG0);
	else
		return;

	writel(val, reg_addr);
};
