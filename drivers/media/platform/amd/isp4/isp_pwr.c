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

#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include "isp_debug.h"
#include "isp_common.h"
#include "isp_hwa.h"
#include "isp_module_intf.h"
#include "isp_fw_ctrl.h"
#include "isp_core.h"
#include "isp_common.h"
#include "isp_mc_addr_mgr.h"
#include "isp_pwr.h"
#include "isp_param.h"

struct isp_dpm_value isp_v4_1_1_dpm_value[] = {
	{ISP_V4_1_1_DPM0_SOCCLK, ISP_V4_1_1_DPM0_ISPICLK, ISP_V4_1_1_DPM0_ISPXCLK},
	{ISP_V4_1_1_DPM1_SOCCLK, ISP_V4_1_1_DPM1_ISPICLK, ISP_V4_1_1_DPM1_ISPXCLK},
	{ISP_V4_1_1_DPM2_SOCCLK, ISP_V4_1_1_DPM2_ISPICLK, ISP_V4_1_1_DPM2_ISPXCLK},
	{ISP_V4_1_1_DPM3_SOCCLK, ISP_V4_1_1_DPM3_ISPICLK, ISP_V4_1_1_DPM3_ISPXCLK},
	{ISP_V4_1_1_DPM4_SOCCLK, ISP_V4_1_1_DPM4_ISPICLK, ISP_V4_1_1_DPM4_ISPXCLK},
	{ISP_V4_1_1_DPM5_SOCCLK, ISP_V4_1_1_DPM5_ISPICLK, ISP_V4_1_1_DPM5_ISPXCLK},
	{ISP_V4_1_1_DPM6_SOCCLK, ISP_V4_1_1_DPM6_ISPICLK, ISP_V4_1_1_DPM6_ISPXCLK},
	{ISP_V4_1_1_DPM7_SOCCLK, ISP_V4_1_1_DPM7_ISPICLK, ISP_V4_1_1_DPM7_ISPXCLK}
};

u32 g_drv_dpm_level = ISP_DPM_LEVEL_MAX;

int isp_clk_change(struct isp_context *isp, enum camera_port_id cid,
		   unsigned int index, int hdr_enable, int on)
{
	/* this part of code need to be added to set clock */
	return 0;
}

void isp_get_clks(struct isp_context *isp, enum camera_port_id cid,
		  u32 *xclk_mhz, u32 *iclk_mhz, u32 *sclk_mhz)
{
	struct device *dev = &isp->amd_cam->pdev->dev;
	u32 dpm;
	s32 pipeline;

	do {
		/* if the clock is set by module param, directly use it */
		if (g_drv_dpm_level < ISP_DPM_LEVEL_MAX) {
			dpm = g_drv_dpm_level;
			break;
		}
		/* in single camera, if sensor profile is less than 12M, DPM0 should be enough
		 * but in real test, CSTAT error is found which makes preview freeze
		 * so as quick WA, temporarily boost it to DPM1, will change back to DPM0
		 * when this issue is fixed
		 */
		dpm = ISP_DPM_LEVEL_1;
		pipeline = isp_get_pipeline_id(isp, cid);
		//If LME is enabled, need to boost the clocks to DPM3
		if (pipeline & LME_PIPELINE_ID)
			dpm = ISP_DPM_LEVEL_3;
	} while (false);

	if (xclk_mhz)
		*xclk_mhz = isp_v4_1_1_dpm_value[dpm].isp_xclk;
	if (iclk_mhz)
		*iclk_mhz = isp_v4_1_1_dpm_value[dpm].isp_iclk;
	if (sclk_mhz)
		*sclk_mhz = isp_v4_1_1_dpm_value[dpm].soc_clk;

	dev_dbg(dev, "dpm %u,xclk %u,iclk %u,soc_clk %u",
		dpm, isp_v4_1_1_dpm_value[dpm].isp_xclk,
		isp_v4_1_1_dpm_value[dpm].isp_iclk,
		isp_v4_1_1_dpm_value[dpm].soc_clk);
}

int isp_ip_pwr_on(struct isp_context *isp, enum camera_port_id cid,
		  u32 index, s32 hdr_enable)
{
	struct device *dev;
	int ret;
	struct isp_pwr_unit *pwr_unit;
	u32 xclk;
	u32 iclk;
	u32 sclk;

	if (!isp) {
		pr_err("fail for null isp");
		return -EINVAL;
	}

	dev = &isp->amd_cam->pdev->dev;
	pwr_unit = &isp->isp_pu_isp;

	dev_dbg(dev, "cid %u, idx %u, hdr %u", cid, index, hdr_enable);

	mutex_lock(&pwr_unit->pwr_status_mutex);

	if (pwr_unit->pwr_status == ISP_PWR_UNIT_STATUS_OFF) {
		u32 reg;

		isp->isp_semaphore_acq_cnt = 0;
		isp_power_set(true);
		/* ISPPG ISP Power Status */
		isp_hwa_wreg(isp, ISP_POWER_STATUS, 0x7FF);

		reg = isp_hwa_rreg(isp, ISP_VERSION);
		dev_dbg(dev, "hw ver 0x%x", reg);

		reg = isp_hwa_rreg(isp, ISP_STATUS);
		dev_dbg(dev, "ISP status  0x%x", reg);

		if (isp_start_resp_proc_threads(isp)) {
			dev_err(dev, "isp_start_resp_proc_threads fail");
			ret = -EINVAL;
			goto quit;
		} else {
			dev_dbg(dev, "create resp threads ok");
		}
	}

	isp_get_clks(isp, cid, &xclk, &iclk, &sclk);
	/* set clocks */
	isp_hwa_clock_set(isp, xclk, iclk, sclk);

	if (pwr_unit->pwr_status == ISP_PWR_UNIT_STATUS_OFF)
		pwr_unit->pwr_status = ISP_PWR_UNIT_STATUS_ON;

	if (ISP_GET_STATUS(isp) == ISP_STATUS_PWR_OFF) {
		/* commit it temporarily, it ISP can work, will remove it later */
		/* isp_hw_reset_all(isp); */
		/* should be 24M */
		if (isp->refclk != 24) {
			dev_err(dev, "fail isp->refclk %u should be 24",
				isp->refclk);
		}
		/* isp_i2c_init(isp->iclk); */
		/* change to following according to aidt */
		ISP_SET_STATUS(isp, ISP_STATUS_PWR_ON);
	}
	ret = 0;
	dev_dbg(dev, "ISP Power on");

quit:
	mutex_unlock(&pwr_unit->pwr_status_mutex);
	return ret;
};

int isp_ip_pwr_off(struct isp_context *isp)
{
	struct device *dev;
	struct isp_pwr_unit *pwr_unit;

	if (!isp) {
		pr_err("fail for null isp");
		return -EINVAL;
	}

	dev = &isp->amd_cam->pdev->dev;
	pwr_unit = &isp->isp_pu_isp;

	isp_stop_resp_proc_threads(isp);
	dev_dbg(dev, "isp stop resp proc streads suc");

	mutex_lock(&pwr_unit->pwr_status_mutex);
	if (pwr_unit->pwr_status == ISP_PWR_UNIT_STATUS_OFF) {
		dev_dbg(dev, "suc do none");
	} else {
		/* hold ccpu reset */
		isp_hwa_wreg(isp, ISP_SOFT_RESET, 0x0);

		isp_hwa_wreg(isp, ISP_POWER_STATUS, 0);

		dev_dbg(dev, "disable isp power tile");
		isp_power_set(false);

		pwr_unit->pwr_status = ISP_PWR_UNIT_STATUS_OFF;
		ISP_SET_STATUS(isp, ISP_STATUS_PWR_OFF);
		isp->sclk = 0;
		isp->iclk = 0;
		isp->xclk = 0;
		isp->refclk = 24; /* default value */
		dev_dbg(dev, "ISP Power off");
	}
	isp->clk_info_set_2_fw = 0;
	for (enum camera_port_id cam_port_id = 0; cam_port_id < CAMERA_PORT_MAX;
	     cam_port_id++)
		isp->snr_info_set_2_fw[cam_port_id] = 0;
	mutex_unlock(&pwr_unit->pwr_status_mutex);
	return 0;
};
