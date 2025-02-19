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

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/kobject.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/uaccess.h>
#include <drm/ttm/ttm_tt.h>
#include <linux/page_ref.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include "amdgpu_object.h"
#include "isp_debug.h"
#include "isp_common.h"
#include "isp_hwa.h"
#include "isp_hw_reg.h"
#include "isp_core.h"

#define ISP_POWER_OFF_CMD         0x29
#define ISP_POWER_ON_CMD          0x2A
#define ISP_ALL_TILES             0x7FF
#define ISP_XCLK_CMD              0x2C
#define ISP_ICLK_CMD              0x2B

static struct isp_context *g_isp_context;

static int isp_query_pmfw_mbox_status(struct isp_context *isp, u32 *mbox_status)
{
	struct device *dev;
	u32 retry = 10;
	int ret = 0;

	dev = &isp->amd_cam->pdev->dev;
	if (mbox_status) {
		do {
			*mbox_status = isp_hwa_indirect_rreg(isp, HOST2PM_RESP_REG);
			usleep_range(5000, 10000);
		} while ((*mbox_status == 0) && (retry--));

		if (*mbox_status == 0) {
			dev_dbg(dev, "PMFW mbox not ready");
			ret = -ETIME;
		}
	} else {
		dev_err(dev, "Invalid mbox_status pointer");
		ret = -EINVAL;
	}
	return ret;
}

static int isp_hwa_set_xclk(struct isp_context *isp, u32 xclk_mhz)
{
	struct device *dev = &isp->amd_cam->pdev->dev;
	u32 mbox_status = 0;
	int ret;

	ret = isp_query_pmfw_mbox_status(isp, &mbox_status);
	if (ret)
		return ret;

	/* set xclk */
	isp_hwa_indirect_wreg(isp, HOST2PM_RESP_REG, 0);

	isp_hwa_indirect_wreg(isp, HOST2PM_ARG_REG, xclk_mhz);

	isp_hwa_indirect_wreg(isp, HOST2PM_MSG_REG, ISP_XCLK_CMD);

	ret = isp_query_pmfw_mbox_status(isp, &mbox_status);
	if (ret)
		return ret;

	if (mbox_status == ISPSMC_Result_OK) {
		dev_dbg(dev, "set xclk %d success, reg_val %d",
			xclk_mhz, isp_hwa_indirect_rreg(isp, HOST2PM_ARG_REG));
	} else {
		ret = -EINVAL;
		dev_err(dev, "failed, invalid pmfw response 0x%x", mbox_status);
	}
	return ret;
}

static int isp_hwa_set_iclk(struct isp_context *isp, u32 iclk_mhz)
{
	struct device *dev = &isp->amd_cam->pdev->dev;
	u32 mbox_status = 0;
	int ret;

	ret = isp_query_pmfw_mbox_status(isp, &mbox_status);
	if (ret)
		return ret;

	/* set iclk */
	isp_hwa_indirect_wreg(isp, HOST2PM_RESP_REG, 0);

	isp_hwa_indirect_wreg(isp, HOST2PM_ARG_REG, iclk_mhz);

	isp_hwa_indirect_wreg(isp, HOST2PM_MSG_REG, ISP_ICLK_CMD);

	ret = isp_query_pmfw_mbox_status(isp, &mbox_status);
	if (ret)
		return ret;

	if (mbox_status == ISPSMC_Result_OK) {
		dev_dbg(dev, "set iclk %d success, reg_val %d",
			iclk_mhz, isp_hwa_indirect_rreg(isp, HOST2PM_ARG_REG));
	} else {
		ret = -EINVAL;
		dev_err(dev, "failed, invalid pmfw response 0x%x", mbox_status);
	}
	return ret;
}

static int isp_hwa_req_pwr(struct isp_context *isp, int enable)
{
	struct device *dev;
	u32 mbox_status = 0;
	int ret;
	u8 *cmdstr;

	dev = &isp->amd_cam->pdev->dev;

	if (IS_ERR_OR_NULL(isp->amd_cam->isp_mmio)) {
		dev_err(dev, "%s failed, invalid iomem handle!", __func__);
		return -EINVAL;
	}

	cmdstr = enable ? "on" : "off";
	ret = isp_query_pmfw_mbox_status(isp, &mbox_status);
	if (ret)
		return ret;

	isp_hwa_indirect_wreg(isp, HOST2PM_RESP_REG, 0);

	isp_hwa_indirect_wreg(isp, HOST2PM_ARG_REG, ISP_ALL_TILES);

	if (enable)
		isp_hwa_indirect_wreg(isp, HOST2PM_MSG_REG, ISP_POWER_ON_CMD);
	else
		isp_hwa_indirect_wreg(isp, HOST2PM_MSG_REG, ISP_POWER_OFF_CMD);

	ret = isp_query_pmfw_mbox_status(isp, &mbox_status);
	if (ret)
		return ret;

	if (mbox_status == ISPSMC_Result_OK) {
		dev_dbg(dev, "%s %s completed 0x%x",
			__func__, cmdstr, isp_hwa_indirect_rreg(isp, HOST2PM_ARG_REG));
	} else {
		ret = -EINVAL;
		dev_err(dev, "%s %s failed, invalid pmfw response 0x%x",
			__func__, cmdstr, mbox_status);
	}

	return ret;
}

int isp_hwa_clock_set(struct isp_context *isp, u32 xclk_mhz, u32 iclk_mhz, u32 sclk_mhz)
{
	struct device *dev;

	dev = &isp->amd_cam->pdev->dev;

	if (IS_ERR_OR_NULL(isp->amd_cam->isp_mmio)) {
		dev_err(dev, "%s failed, invalid iomem handle!", __func__);
		return -EINVAL;
	}

	isp_hwa_set_xclk(isp, xclk_mhz);

	isp_hwa_set_iclk(isp, iclk_mhz);

	return 0;
}

u32 isp_hwa_rreg(struct isp_context *isp, u32 reg)
{
	struct device *dev;
	void __iomem *reg_addr;

	dev = &isp->amd_cam->pdev->dev;
	if (IS_ERR(isp->amd_cam->isp_mmio)) {
		dev_err(dev, "%s failed, invalid iomem handle!", __func__);
		return PTR_ERR(isp->amd_cam->isp_mmio);
	}

	if (reg >= RMMIO_SIZE) {
		dev_err(dev, "-><- %s failed bad offset %u", __func__, reg);
		return RREG_FAILED_VAL;
	}

	if (reg >= ISP_MIPI_PHY0_REG0 && reg <= ISP_MIPI_PHY0_REG0 + ISP_MIPI_PHY0_SIZE)
		reg_addr = isp->amd_cam->isp_phy_mmio + (reg - ISP_MIPI_PHY0_REG0);
	else
		reg_addr = isp->amd_cam->isp_mmio + reg;

	return readl(reg_addr);
};

void isp_hwa_wreg(struct isp_context *isp, u32 reg, u32 val)
{
	struct device *dev;
	void __iomem *reg_addr;

	dev = &isp->amd_cam->pdev->dev;
	if (IS_ERR(isp->amd_cam->isp_mmio)) {
		dev_err(dev, "%s failed, invalid iomem handle!", __func__);
		return;
	}

	if (reg >= RMMIO_SIZE) {
		dev_err(dev, "-><- %s failed bad offset %u", __func__, reg);
		return;
	}

	if (reg >= ISP_MIPI_PHY0_REG0 && reg <= ISP_MIPI_PHY0_REG0 + ISP_MIPI_PHY0_SIZE)
		reg_addr = isp->amd_cam->isp_phy_mmio + (reg - ISP_MIPI_PHY0_REG0);
	else
		reg_addr = isp->amd_cam->isp_mmio + reg;

	writel(val, reg_addr);
};

u32 isp_hwa_indirect_rreg(struct isp_context *isp, u32 reg)
{
	struct device *dev;
	void __iomem *pcie_index_offset;
	void __iomem *pcie_data_offset;

	dev = &isp->amd_cam->pdev->dev;
	if (IS_ERR(isp->amd_cam->isp_mmio)) {
		dev_err(dev, "%s failed, invalid iomem handle!", __func__);
		return PTR_ERR(isp->amd_cam->isp_mmio);
	}

	if (reg < RMMIO_SIZE) {
		dev_err(dev, "-><- %s failed bad offset %u", __func__, reg);
		return RREG_FAILED_VAL;
	}

	pcie_index_offset = isp->amd_cam->isp_mmio + ISP_NBIF_GPU_PCIE_INDEX * 4;
	pcie_data_offset = isp->amd_cam->isp_mmio + ISP_NBIF_GPU_PCIE_DATA * 4;
	writel(reg, pcie_index_offset);

	return readl(pcie_data_offset);
};

void isp_hwa_indirect_wreg(struct isp_context *isp, u32 reg, u32 val)
{
	struct device *dev;
	void __iomem *pcie_index_offset;
	void __iomem *pcie_data_offset;

	dev = &isp->amd_cam->pdev->dev;
	if (IS_ERR(isp->amd_cam->isp_mmio)) {
		dev_err(dev, "%s failed, invalid iomem handle!", __func__);
		return;
	}

	if (reg < RMMIO_SIZE) {
		dev_err(dev, "-><- %s failed bad offset %u", __func__, reg);
		return;
	}

	pcie_index_offset = isp->amd_cam->isp_mmio + ISP_NBIF_GPU_PCIE_INDEX * 4;
	pcie_data_offset = isp->amd_cam->isp_mmio + ISP_NBIF_GPU_PCIE_DATA * 4;
	writel(reg, pcie_index_offset);
	writel(val, pcie_data_offset);
};

struct isp_gpu_mem_info *isp_gpu_mem_alloc(struct isp_context *isp, u32 mem_size)
{
	struct device *dev;
	struct amdisp_platform_data *pltf_data;
	struct isp_gpu_mem_info *mem_info;
	struct amdgpu_device *adev;
	struct amdgpu_bo *bo = NULL;
	void *cpu_ptr;
	u64 gpu_addr;
	u32 ret;

	dev = &isp->amd_cam->pdev->dev;

	if (mem_size == 0) {
		dev_err(dev, "invalid mem size");
		return NULL;
	}

	mem_info = kzalloc(sizeof(*mem_info), GFP_KERNEL);
	if (!mem_info)
		return NULL;

	pltf_data = isp->amd_cam->pltf_data;
	adev = (struct amdgpu_device *)pltf_data->adev;
	mem_info->mem_size = mem_size;
	mem_info->mem_align = ISP_MC_ADDR_ALIGN;
	mem_info->mem_domain = AMDGPU_GEM_DOMAIN_GTT;

	ret = amdgpu_bo_create_kernel(adev,
				      mem_info->mem_size,
				      mem_info->mem_align,
				      mem_info->mem_domain,
				      &bo,
				      &gpu_addr,
				      &cpu_ptr);

	if (!cpu_ptr || ret) {
		dev_err(dev, "gpuvm buffer alloc failed, size %u", mem_size);
		kfree(mem_info);
		return NULL;
	}

	mem_info->sys_addr = cpu_ptr;
	mem_info->gpu_mc_addr = gpu_addr;
	mem_info->mem_handle = (void *)bo;

	return mem_info;
}

int isp_gpu_mem_free(struct isp_context *isp, struct isp_gpu_mem_info *mem_info)
{
	struct device *dev;
	struct amdgpu_bo *bo;

	dev = &isp->amd_cam->pdev->dev;

	if (!mem_info) {
		dev_err(dev, "invalid mem_info");
		return -EINVAL;
	}

	bo = (struct amdgpu_bo *)mem_info->mem_handle;

	amdgpu_bo_free_kernel(&bo, &mem_info->gpu_mc_addr, &mem_info->sys_addr);

	kfree(mem_info);

	return 0;
}

int isp_hwa_init(struct isp_context *isp)
{
	if (IS_ERR_OR_NULL(isp)) {
		pr_err("-><- %s invalid isp context", __func__);
		return PTR_ERR(isp);
	}

	g_isp_context = isp;

	return 0;
}

int isp_power_set(int enable)
{
	if (IS_ERR_OR_NULL(g_isp_context))
		pr_err("%s invalid isp_context", __func__);

	return isp_hwa_req_pwr(g_isp_context, enable);
}
EXPORT_SYMBOL(isp_power_set);
