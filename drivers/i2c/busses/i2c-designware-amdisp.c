// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Based on Synopsys DesignWare I2C adapter driver.
 *
 * Copyright (C) 2024-2025 Advanced Micro Devices, Inc. All rights reserved.
 *
 */
#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dmi.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <linux/units.h>

#include "i2c-designware-core.h"
#include "i2c-designware-amdisp.h"

#define AMD_ISP_I2C_INPUT_CLK			100 //100 Mhz

#define to_amd_isp_i2c_dev(dev) \
	((struct amd_isp_i2c_dev *)container_of(dev, struct amd_isp_i2c_dev, dw_dev))

struct amd_isp_i2c_dev {
	struct i2c_client	*sensor0;
	struct dw_i2c_dev	dw_dev;
};

static struct i2c_board_info ov05_info = {
	.dev_name = "ov05",
	I2C_BOARD_INFO("ov05", 0x10),
};

static void amd_isp_add_i2c_device(struct amd_isp_i2c_dev *isp_i2c_dev)
{
	struct i2c_adapter *adap;

	adap = &isp_i2c_dev->dw_dev.adapter;

	isp_i2c_dev->sensor0 = i2c_new_client_device(adap, &ov05_info);
	if (IS_ERR(isp_i2c_dev->sensor0)) {
		dev_err(isp_i2c_dev->dw_dev.dev,
			"Failed to add the OV05 sensor device to the ISP I2C bus\n");
	} else {
		dev_info(isp_i2c_dev->dw_dev.dev,
			 "The OV05 sensor device is added to the ISP I2C bus\n");
	}
}

static void amd_isp_remove_i2c_device(struct amd_isp_i2c_dev *isp_i2c_dev)
{
	if (isp_i2c_dev->sensor0)
		i2c_unregister_device(isp_i2c_dev->sensor0);
}

static void dw_i2c_plat_pm_cleanup(struct dw_i2c_dev *dev)
{
	pm_runtime_disable(dev->dev);

	if (dev->shared_with_punit)
		pm_runtime_put_noidle(dev->dev);
}

static u32 amd_isp_dw_i2c_get_clk_rate(struct dw_i2c_dev *dev)
{
	return AMD_ISP_I2C_INPUT_CLK * 1000;
}

static int dw_i2c_plat_probe(struct platform_device *pdev)
{
	struct i2c_adapter *adap;
	struct amd_isp_i2c_dev *isp_i2c_dev;
	struct dw_i2c_dev *dev;
	int ret;

	isp_i2c_dev = devm_kzalloc(&pdev->dev, sizeof(struct amd_isp_i2c_dev),
				   GFP_KERNEL);
	if (!isp_i2c_dev)
		return -ENOMEM;

	dev = &isp_i2c_dev->dw_dev;
	dev->dev = &pdev->dev;

	/**
	 * Use the polling mode to send/receive the data, because
	 * no IRQ connection from ISP I2C
	 */
	dev->flags |= ACCESS_POLLING;
	platform_set_drvdata(pdev, dev);

	dev->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(dev->base))
		return PTR_ERR(dev->base);

	ret = isp_power_set(true);
	if (ret) {
		dev_err(dev->dev, "unable to turn on the amdisp i2c power:%d\n", ret);
		return ret;
	}

	dev->get_clk_rate_khz = amd_isp_dw_i2c_get_clk_rate;
	ret = i2c_dw_fw_parse_and_configure(dev);
	if (ret)
		goto exit;

	i2c_dw_configure(dev);

	adap = &dev->adapter;
	adap->owner = THIS_MODULE;
	ACPI_COMPANION_SET(&adap->dev, ACPI_COMPANION(&pdev->dev));
	adap->dev.of_node = pdev->dev.of_node;
	/* arbitrary large number to avoid any conflicts */
	adap->nr = 99;

	if (dev->flags & ACCESS_NO_IRQ_SUSPEND) {
		dev_pm_set_driver_flags(&pdev->dev,
					DPM_FLAG_SMART_PREPARE);
	} else {
		dev_pm_set_driver_flags(&pdev->dev,
					DPM_FLAG_SMART_PREPARE |
					DPM_FLAG_SMART_SUSPEND);
	}

	device_enable_async_suspend(&pdev->dev);

	/* The code below assumes runtime PM to be disabled. */
	WARN_ON(pm_runtime_enabled(&pdev->dev));

	pm_runtime_dont_use_autosuspend(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);

	if (dev->shared_with_punit)
		pm_runtime_get_noresume(&pdev->dev);

	pm_runtime_enable(&pdev->dev);

	ret = i2c_dw_probe(dev);
	if (ret) {
		dev_err(dev->dev, "i2c_dw_probe failed %d\n", ret);
		goto exit_probe;
	}

	amd_isp_add_i2c_device(isp_i2c_dev);
	isp_power_set(false);
	return ret;

exit_probe:
	dw_i2c_plat_pm_cleanup(dev);
	isp_power_set(false);
exit:
	isp_power_set(false);
	return ret;
}

static void dw_i2c_plat_remove(struct platform_device *pdev)
{
	struct dw_i2c_dev *dev = platform_get_drvdata(pdev);
	struct amd_isp_i2c_dev *isp_i2c_dev = to_amd_isp_i2c_dev(dev);

	pm_runtime_get_sync(&pdev->dev);
	amd_isp_remove_i2c_device(isp_i2c_dev);

	i2c_del_adapter(&dev->adapter);

	i2c_dw_disable(dev);

	pm_runtime_dont_use_autosuspend(&pdev->dev);
	pm_runtime_put_sync(&pdev->dev);
	dw_i2c_plat_pm_cleanup(dev);

	reset_control_assert(dev->rst);
}

static int dw_i2c_plat_prepare(struct device *dev)
{
	/*
	 * If the ACPI companion device object is present for this device, it
	 * may be accessed during suspend and resume of other devices via I2C
	 * operation regions, so tell the PM core and middle layers to avoid
	 * skipping system suspend/resume callbacks for it in that case.
	 */
	return !has_acpi_companion(dev);
}

static int dw_i2c_plat_runtime_suspend(struct device *dev)
{
	struct dw_i2c_dev *i_dev = dev_get_drvdata(dev);

	if (i_dev->shared_with_punit)
		return 0;

	i2c_dw_disable(i_dev);
	i2c_dw_prepare_clk(i_dev, false);

	return 0;
}

static int dw_i2c_plat_suspend(struct device *dev)
{
	struct dw_i2c_dev *i_dev = dev_get_drvdata(dev);

	i2c_mark_adapter_suspended(&i_dev->adapter);

	return dw_i2c_plat_runtime_suspend(dev);
}

static int dw_i2c_plat_runtime_resume(struct device *dev)
{
	struct dw_i2c_dev *i_dev = dev_get_drvdata(dev);

	if (!i_dev->shared_with_punit)
		i2c_dw_prepare_clk(i_dev, true);

	i_dev->init(i_dev);

	return 0;
}

static int dw_i2c_plat_resume(struct device *dev)
{
	struct dw_i2c_dev *i_dev = dev_get_drvdata(dev);

	dw_i2c_plat_runtime_resume(dev);
	i2c_mark_adapter_resumed(&i_dev->adapter);

	return 0;
}

static const struct dev_pm_ops dw_i2c_dev_pm_ops = {
	.prepare = pm_sleep_ptr(dw_i2c_plat_prepare),
	LATE_SYSTEM_SLEEP_PM_OPS(dw_i2c_plat_suspend, dw_i2c_plat_resume)
	RUNTIME_PM_OPS(dw_i2c_plat_runtime_suspend, dw_i2c_plat_runtime_resume, NULL)
};

/* Work with hotplug and coldplug */
MODULE_ALIAS("platform:amd_isp_i2c_designware");

static struct platform_driver dw_i2c_driver = {
	.probe = dw_i2c_plat_probe,
	.remove = dw_i2c_plat_remove,
	.driver		= {
		.name	= "amd_isp_i2c_designware",
		.pm	= pm_ptr(&dw_i2c_dev_pm_ops),
	},
};

static int __init amd_isp_dw_i2c_init_driver(void)
{
	return platform_driver_register(&dw_i2c_driver);
}
subsys_initcall(amd_isp_dw_i2c_init_driver);

static void __exit amd_isp_dw_i2c_exit_driver(void)
{
	platform_driver_unregister(&dw_i2c_driver);
}
module_exit(amd_isp_dw_i2c_exit_driver);

MODULE_AUTHOR("Venkata Narendra Kumar Gutta <vengutta@amd.com>");
MODULE_DESCRIPTION("Synopsys DesignWare I2C bus adapter in AMD ISP");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("I2C_DW");
MODULE_IMPORT_NS("I2C_DW_COMMON");
