// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2025 Advanced Micro Devices, Inc.
 */

#include <linux/pm_runtime.h>
#include <linux/vmalloc.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-ioctl.h>

#include "isp4.h"
#include "isp4_debug.h"
#include "isp4_hw.h"

#define ISP4_DRV_NAME "amd_isp_capture"
#define ISP4_FW_RESP_RB_IRQ_STATUS_MASK \
	(ISP_SYS_INT0_STATUS__SYS_INT_RINGBUFFER_WPT9_INT_MASK  | \
	 ISP_SYS_INT0_STATUS__SYS_INT_RINGBUFFER_WPT10_INT_MASK | \
	 ISP_SYS_INT0_STATUS__SYS_INT_RINGBUFFER_WPT11_INT_MASK | \
	 ISP_SYS_INT0_STATUS__SYS_INT_RINGBUFFER_WPT12_INT_MASK)

/* interrupt num */
static const u32 isp4_ringbuf_interrupt_num[] = {
	0, /* ISP_4_1__SRCID__ISP_RINGBUFFER_WPT9 */
	1, /* ISP_4_1__SRCID__ISP_RINGBUFFER_WPT10 */
	3, /* ISP_4_1__SRCID__ISP_RINGBUFFER_WPT11 */
	4, /* ISP_4_1__SRCID__ISP_RINGBUFFER_WPT12 */
};

#define to_isp4_device(dev) \
	((struct isp4_device *)container_of(dev, struct isp4_device, v4l2_dev))

static int isp4_create_links(struct isp4_device *isp4_dev,
			     struct v4l2_subdev *sensor_sdev)
{
	struct v4l2_subdev *isp4_sdev = &isp4_dev->isp_sdev.sdev;
	struct device *dev = &isp4_dev->pdev->dev;
	int ret;

	ret = media_create_pad_link(&sensor_sdev->entity,
				    0, &isp4_sdev->entity, 0,
				    MEDIA_LNK_FL_ENABLED |
				    MEDIA_LNK_FL_IMMUTABLE);
	if (ret)
		dev_err(dev, "create sensor to isp link fail:%d\n", ret);

	return ret;
}

static int isp4_register_subdev_and_create_links(struct isp4_device *isp_dev,
						 struct v4l2_subdev *sdev)
{
	struct device *dev = &isp_dev->pdev->dev;
	int ret;

	ret = isp4_create_links(isp_dev, sdev);
	if (ret)
		dev_err(dev, "fail create isp link:%d\n", ret);

	ret = v4l2_device_register_subdev_nodes(&isp_dev->v4l2_dev);
	if (ret != 0) {
		dev_warn(dev, "register subdev as nodes fail:%d\n", ret);
		ret = 0;
	}

	return ret;
}

static int isp4_camera_sensor_bound(struct v4l2_async_notifier *notifier,
				    struct v4l2_subdev *sensor_sdev,
				    struct v4l2_async_connection *asd)
{
	struct isp4_device *isp_dev = to_isp4_device(notifier->v4l2_dev);
	struct device *dev = &isp_dev->pdev->dev;
	int ret;

	ret = isp4_register_subdev_and_create_links(isp_dev, sensor_sdev);
	if (ret)
		dev_err(dev, "register sensor subdev fail:%d\n",
			ret);
	else
		dev_dbg(dev, "register sensor subdev suc\n");
	return ret;
}

static void isp4_camera_sensor_unbind(struct v4l2_async_notifier *notifier,
				      struct v4l2_subdev *sensor_sdev,
				      struct v4l2_async_connection *asd)
{
}

static const struct v4l2_async_notifier_operations isp4_camera_sensor_ops = {
	.bound = isp4_camera_sensor_bound,
	.unbind = isp4_camera_sensor_unbind,
};

static void isp4_wake_up_resp_thread(struct isp4_subdev *isp, u32 index)
{
	if (isp && index < ISP4SD_MAX_FW_RESP_STREAM_NUM) {
		struct isp4sd_thread_handler *thread_ctx =
				&isp->fw_resp_thread[index];

		thread_ctx->wq_cond = 1;
		wake_up_interruptible(&thread_ctx->waitq);
	}
}

static void isp4_resp_interrupt_notify(struct isp4_subdev *isp, u32 intr_status)
{
	bool wake = (isp->ispif.status == ISP4IF_STATUS_FW_RUNNING);

	u32 intr_ack = 0;

	/* global response */
	if (intr_status &
	    ISP_SYS_INT0_STATUS__SYS_INT_RINGBUFFER_WPT12_INT_MASK) {
		if (wake)
			isp4_wake_up_resp_thread(isp, 0);

		intr_ack |= ISP_SYS_INT0_ACK__SYS_INT_RINGBUFFER_WPT12_ACK_MASK;
	}

	/* stream 1 response */
	if (intr_status &
	    ISP_SYS_INT0_STATUS__SYS_INT_RINGBUFFER_WPT9_INT_MASK) {
		if (wake)
			isp4_wake_up_resp_thread(isp, 1);

		intr_ack |= ISP_SYS_INT0_ACK__SYS_INT_RINGBUFFER_WPT9_ACK_MASK;
	}

	/* stream 2 response */
	if (intr_status &
	    ISP_SYS_INT0_STATUS__SYS_INT_RINGBUFFER_WPT10_INT_MASK) {
		if (wake)
			isp4_wake_up_resp_thread(isp, 2);

		intr_ack |= ISP_SYS_INT0_ACK__SYS_INT_RINGBUFFER_WPT10_ACK_MASK;
	}

	/* stream 3 response */
	if (intr_status &
	    ISP_SYS_INT0_STATUS__SYS_INT_RINGBUFFER_WPT11_INT_MASK) {
		if (wake)
			isp4_wake_up_resp_thread(isp, 3);

		intr_ack |= ISP_SYS_INT0_ACK__SYS_INT_RINGBUFFER_WPT11_ACK_MASK;
	}

	/* clear ISP_SYS interrupts */
	isp4hw_wreg(ISP4_GET_ISP_REG_BASE(isp), ISP_SYS_INT0_ACK, intr_ack);
}

static irqreturn_t isp4_irq_handler(int irq, void *arg)
{
	struct isp4_device *isp_dev = dev_get_drvdata(arg);
	struct isp4_subdev *isp = NULL;
	u32 isp_sys_irq_status = 0x0;
	u32 r1;

	if (!isp_dev)
		return IRQ_HANDLED;

	isp = &isp_dev->isp_sdev;
	/* check ISP_SYS interrupts status */
	r1 = isp4hw_rreg(ISP4_GET_ISP_REG_BASE(isp), ISP_SYS_INT0_STATUS);

	isp_sys_irq_status = r1 & ISP4_FW_RESP_RB_IRQ_STATUS_MASK;

	isp4_resp_interrupt_notify(isp, isp_sys_irq_status);

	return IRQ_HANDLED;
}

static int isp4_parse_fwnode_init_async_nf(struct isp4_device *isp_dev)
{
	struct isp4_subdev *isp_sdev = &isp_dev->isp_sdev;
	struct device *dev = &isp_dev->pdev->dev;
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	struct fwnode_handle *remote_ep = NULL;
	struct fwnode_handle *local_ep = NULL;
	struct v4l2_async_connection *asd;
	struct fwnode_handle *fwnode;
	struct fwnode_endpoint fwep;
	int ret;

	fwnode = dev_fwnode(dev);

	local_ep = fwnode_graph_get_next_endpoint(fwnode, NULL);
	if (!local_ep) {
		ret = -ENXIO;
		goto err_fwnode;
	}

	remote_ep = fwnode_graph_get_remote_endpoint(local_ep);
	if (!remote_ep) {
		ret = -ENXIO;
		goto err_fwnode;
	}

	ret = fwnode_graph_parse_endpoint(remote_ep, &fwep);
	if (ret)
		goto err_fwnode;
	isp_sdev->phy_id = fwep.port;

	ret = v4l2_fwnode_endpoint_alloc_parse(remote_ep, &bus_cfg);
	if (ret)
		goto err_fwnode;

	if (!bus_cfg.nr_of_link_frequencies) {
		ret = -EINVAL;
		dev_err(dev, "fail invalid link freq number %u\n", bus_cfg.nr_of_link_frequencies);
		v4l2_fwnode_endpoint_free(&bus_cfg);
		goto err_fwnode;
	}
	isp_sdev->phy_link_freq = *bus_cfg.link_frequencies;
	v4l2_fwnode_endpoint_free(&bus_cfg);

	isp_sdev->phy_num_data_lanes =
		fwnode_property_count_u32(remote_ep, "data-lanes");

	v4l2_async_nf_init(&isp_dev->notifier, &isp_dev->v4l2_dev);

	asd = v4l2_async_nf_add_fwnode(&isp_dev->notifier, remote_ep,
				       struct v4l2_async_connection);
	if (IS_ERR(asd)) {
		ret = PTR_ERR(asd);
		goto err_async_nf_cleanup;
	}

	isp_dev->notifier.ops = &isp4_camera_sensor_ops;
	ret = v4l2_async_nf_register(&isp_dev->notifier);
	if (ret) {
		dev_err(dev, "v4l2_async_nf_register fail:%d", ret);
		goto err_async_nf_cleanup;
	}

	return 0;

err_async_nf_cleanup:
	v4l2_async_nf_cleanup(&isp_dev->notifier);
err_fwnode:
	if (remote_ep)
		fwnode_handle_put(remote_ep);
	if (local_ep)
		fwnode_handle_put(remote_ep);

	return ret;
}

/*
 * amd capture module
 */
static int isp4_capture_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct isp4_subdev *isp_sdev;
	struct isp4_device *isp_dev;

	int i, irq, ret;

	isp_dev = devm_kzalloc(&pdev->dev, sizeof(*isp_dev), GFP_KERNEL);
	if (!isp_dev)
		return -ENOMEM;

	isp_dev->pdev = pdev;
	dev->init_name = ISP4_DRV_NAME;

	isp_sdev = &isp_dev->isp_sdev;
	isp_sdev->mmio = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(isp_sdev->mmio))
		return dev_err_probe(dev, PTR_ERR(isp_sdev->mmio),
				     "isp ioremap fail\n");

	isp_sdev->isp_phy_mmio = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(isp_sdev->isp_phy_mmio))
		return dev_err_probe(dev, PTR_ERR(isp_sdev->isp_phy_mmio),
				     "isp phy mmio ioremap fail\n");

	for (i = 0; i < ARRAY_SIZE(isp4_ringbuf_interrupt_num); i++) {
		irq = platform_get_irq(pdev, isp4_ringbuf_interrupt_num[i]);
		if (irq < 0)
			return dev_err_probe(dev, -ENODEV,
					     "fail to get irq %d\n",
					     isp4_ringbuf_interrupt_num[i]);
		ret = devm_request_irq(&pdev->dev, irq, isp4_irq_handler, 0,
				       "ISP_IRQ", &pdev->dev);
		if (ret)
			return dev_err_probe(dev, ret, "fail to req irq %d\n",
					     irq);
	}

	dev_dbg(dev, "isp irq registration successful\n");

	/* Link the media device within the v4l2_device */
	isp_dev->v4l2_dev.mdev = &isp_dev->mdev;

	/* Initialize media device */
	strscpy(isp_dev->mdev.model, "amd_isp41_mdev",
		sizeof(isp_dev->mdev.model));
	snprintf(isp_dev->mdev.bus_info, sizeof(isp_dev->mdev.bus_info),
		 "platform:%s", ISP4_DRV_NAME);
	isp_dev->mdev.dev = &pdev->dev;
	media_device_init(&isp_dev->mdev);

	/* register v4l2 device */
	snprintf(isp_dev->v4l2_dev.name, sizeof(isp_dev->v4l2_dev.name),
		 "AMD-V4L2-ROOT");
	ret = v4l2_device_register(&pdev->dev, &isp_dev->v4l2_dev);
	if (ret)
		return dev_err_probe(dev, ret,
				     "fail register v4l2 device\n");

	dev_dbg(dev, "AMD ISP v4l2 device registered\n");

	ret = isp4sd_init(&isp_dev->isp_sdev, &isp_dev->v4l2_dev);
	if (ret) {
		dev_err(dev, "fail init isp4 sub dev %d\n", ret);
		goto err_unreg_v4l2;
	}

	ret = isp4_parse_fwnode_init_async_nf(isp_dev);
	if (ret) {
		dev_err(dev, "fail to parse fwnode %d\n", ret);
		goto err_unreg_v4l2;
	}

	ret = media_device_register(&isp_dev->mdev);
	if (ret) {
		dev_err(dev, "fail to register media device %d\n", ret);
		goto err_isp4_deinit;
	}

	ret = media_create_pad_link(&isp_dev->isp_sdev.sdev.entity,
				    1, &isp_dev->isp_sdev.isp_vdev.vdev.entity,
				    0,
				    MEDIA_LNK_FL_ENABLED |
				    MEDIA_LNK_FL_IMMUTABLE);
	if (ret) {
		dev_err(dev, "fail to create pad link %d\n", ret);
		goto err_unreg_video_dev_notifier;
	}

	platform_set_drvdata(pdev, isp_dev);

	pm_runtime_set_suspended(dev);
	pm_runtime_enable(dev);

	isp_debugfs_create(isp_dev);

	return 0;

err_unreg_video_dev_notifier:
	v4l2_async_nf_unregister(&isp_dev->notifier);
	v4l2_async_nf_cleanup(&isp_dev->notifier);
err_isp4_deinit:
	isp4sd_deinit(&isp_dev->isp_sdev);
err_unreg_v4l2:
	v4l2_device_unregister(&isp_dev->v4l2_dev);

	return dev_err_probe(dev, ret, "isp probe fail\n");
}

static void isp4_capture_remove(struct platform_device *pdev)
{
	struct isp4_device *isp_dev = platform_get_drvdata(pdev);

	isp_debugfs_remove(isp_dev);

	v4l2_async_nf_unregister(&isp_dev->notifier);
	v4l2_async_nf_cleanup(&isp_dev->notifier);
	v4l2_device_unregister_subdev(&isp_dev->isp_sdev.sdev);

	media_device_unregister(&isp_dev->mdev);
	media_entity_cleanup(&isp_dev->isp_sdev.sdev.entity);
	v4l2_device_unregister(&isp_dev->v4l2_dev);
	dev_dbg(&pdev->dev, "AMD ISP v4l2 device unregistered\n");

	isp4sd_deinit(&isp_dev->isp_sdev);
}

static struct platform_driver isp4_capture_drv = {
	.probe = isp4_capture_probe,
	.remove = isp4_capture_remove,
	.driver = {
		.name = ISP4_DRV_NAME,
		.owner = THIS_MODULE,
	}
};

module_platform_driver(isp4_capture_drv);

MODULE_ALIAS("platform:" ISP4_DRV_NAME);
MODULE_IMPORT_NS("DMA_BUF");

MODULE_DESCRIPTION("AMD ISP4 Driver");
MODULE_AUTHOR("Bin Du <bin.du@amd.com>");
MODULE_AUTHOR("Pratap Nirujogi <pratap.nirujogi@amd.com>");
MODULE_LICENSE("GPL");
