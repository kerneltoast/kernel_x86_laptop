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
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <linux/kobject.h>
#include <linux/kdev_t.h>
#include <linux/uaccess.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-event.h>
#include <media/videobuf2-memops.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-vmalloc.h>
#include <drm/ttm/ttm_tt.h>
#include <linux/page_ref.h>
#include <linux/random.h>

#include "isp_module_intf.h"
#include "isp_core.h"
#include "isp_param.h"
#include "isp_events.h"
#include "isp_hwa.h"
#include "isp_fw_ctrl.h"
#include "isp_debug.h"
#include "isp_common.h"
#include "isp_module_intf.h"
#include "isp_phy.h"
#include "isp_utils.h"

#include "amdgpu_object.h"

#define VIDEO_BUF_NUM 5

#define SEC_TO_NANO_SEC(NUM) ((NUM) * 1000000000)

static bool use_embedded_sensor;
module_param(use_embedded_sensor, bool, 0444);
MODULE_PARM_DESC(use_embedded_sensor,
		 "Pass this value as true to use the embedded sensor");

#define PHY_DEVICE_ID 0
#define PHY_BIT_RATE 1800
#define PHY_NUM_OF_LANES 2

/* interrupt num */
static const u32 ringbuf_interrupt_num[] = {
	0, /* ISP_4_1__SRCID__ISP_RINGBUFFER_WPT9 */
	1, /* ISP_4_1__SRCID__ISP_RINGBUFFER_WPT10 */
	3, /* ISP_4_1__SRCID__ISP_RINGBUFFER_WPT11 */
	4, /* ISP_4_1__SRCID__ISP_RINGBUFFER_WPT12 */
};

static int vb2_amdisp_mmap(void *buf_priv, struct vm_area_struct *vma);

static void vb2_amdisp_put(void *buf_priv);

#define PREVIEW_VDEV_NAME "Preview"

static const char * const isp_video_dev_name[] = {
	PREVIEW_VDEV_NAME,
};

/* Sizes must be in increasing order */
static const struct v4l2_frmsize_discrete isp_frmsize[] = {
	{640, 360},
	{640, 480},
	{1280, 720},
	{1280, 960},
	{1920, 1080},
	{1920, 1440},
	{2560, 1440},
	{2880, 1620},
	{2880, 1624},
	{2888, 1808},
};

static const u32 formats[] = {
	V4L2_PIX_FMT_NV12,
	V4L2_PIX_FMT_YUYV
};

/* timeperframe list */
static const struct v4l2_fract tpfs[] = {
	{.numerator = 1, .denominator = MAX_PHOTO_SEQUENCE_FPS}
};

/* timeperframe default */
static const struct v4l2_fract tpf_default = tpfs[0];

static void *isp4_handle_frame_done(struct isp4_video_dev *ctx,
				    const struct sys_img_buf_info *img_buf)
{
	struct device *dev = &ctx->cam->pdev->dev;
	struct isp4_capture_buffer *isp4_buf;
	void *vbuf;

	spin_lock(&ctx->qlock);

	/* Get the first entry of the list */
	isp4_buf = list_first_entry_or_null(&ctx->buf_list, typeof(*isp4_buf),
					    list);
	if (!isp4_buf) {
		spin_unlock(&ctx->qlock);
		return ERR_PTR(-EAGAIN);
	}

	vbuf = vb2_plane_vaddr(&isp4_buf->vb2.vb2_buf, 0);

	if (vbuf != img_buf->planes[0].sys_addr) {
		dev_err(dev, "Invalid vbuf");
		spin_unlock(&ctx->qlock);
		return ERR_PTR(-EINVAL);
	}

	/* Remove this entry from the list */
	list_del(&isp4_buf->list);

	spin_unlock(&ctx->qlock);

	/* Fill the buffer */
	isp4_buf->vb2.vb2_buf.timestamp = ktime_get_ns();
	isp4_buf->vb2.sequence = ctx->sequence++;
	isp4_buf->vb2.field = V4L2_FIELD_ANY;

	/* at most 2 planes */
	vb2_set_plane_payload(&isp4_buf->vb2.vb2_buf, 0, ctx->format.sizeimage);

	vb2_buffer_done(&isp4_buf->vb2.vb2_buf, VB2_BUF_STATE_DONE);

	dev_dbg(dev, "call vb2_buffer_done(size=%u)", ctx->format.sizeimage);

	return NULL;
}

static s32 isp_module_notify_cb(void *ctx, enum cb_evt_id event, void *param)
{
	struct amd_cam *c = (struct amd_cam *)ctx;

	dev_dbg(&c->pdev->dev, "event=[%d]", event);

	switch (event) {
	case CB_EVT_ID_FRAME_DONE: {
		struct frame_done_cb_para *evt_param =
			(struct frame_done_cb_para *)param;

		if (evt_param->preview.status == BUF_DONE_STATUS_SUCCESS) {
			isp4_handle_frame_done(&c->isp_vdev[ISP4_VDEV_PREVIEW],
					       &evt_param->preview.buf);
		}
	}
	break;
	default:
		dev_err(&c->pdev->dev, "not supported event %d!", event);
		return -EINVAL;
	}

	return 0;
}

static unsigned int vb2_amdisp_num_users(void *buf_priv)
{
	struct vb2_amdisp_buf *buf = buf_priv;

	if (!buf) {
		pr_err("Invalid buf handle");
		return 0;
	}
	return refcount_read(&buf->refcount);
}

static int vb2_amdisp_mmap(void *buf_priv, struct vm_area_struct *vma)
{
	struct vb2_amdisp_buf *buf = buf_priv;
	int ret;

	if (!buf) {
		pr_err("No memory to map");
		return -EINVAL;
	}

	ret = remap_vmalloc_range(vma, buf->vaddr, 0);
	if (ret) {
		pr_err("Remapping vmalloc memory, error: %d", ret);
		return ret;
	}

	/*
	 * Make sure that vm_areas for 2 buffers won't be merged together
	 */
	vm_flags_set(vma, VM_DONTEXPAND);

	dev_dbg(buf->dev, "mmap isp user bo 0x%llx size %ld refcount %d",
		buf->gpu_addr, buf->size, buf->refcount.refs.counter);

	return 0;
}

static void *vb2_amdisp_vaddr(struct vb2_buffer *vb, void *buf_priv)
{
	struct vb2_amdisp_buf *buf = buf_priv;

	if (!buf) {
		pr_err("Invalid buf handle");
		return NULL;
	}

	if (!buf->vaddr) {
		dev_err(buf->dev, "Addr of an unallocated plane requested or cannot map user pointer");
		return NULL;
	}
	return buf->vaddr;
}

static void vb2_amdisp_detach_dmabuf(void *mem_priv)
{
	struct vb2_amdisp_buf *buf = mem_priv;

	if (!buf) {
		pr_err("Invalid buf handle");
		return;
	}

	struct iosys_map map = IOSYS_MAP_INIT_VADDR(buf->vaddr);

	dev_dbg(buf->dev, "detach dmabuf of isp user bo 0x%llx size %ld",
		buf->gpu_addr, buf->size);

	if (buf->vaddr)
		dma_buf_vunmap_unlocked(buf->dbuf, &map);

	// put dmabuf for exported ones
	dma_buf_put(buf->dbuf);

	kfree(buf);
}

static void *vb2_amdisp_attach_dmabuf(struct vb2_buffer *vb,
				      struct device *dev,
				      struct dma_buf *dbuf,
				      unsigned long size)
{
	struct vb2_amdisp_buf *buf;

	if (dbuf->size < size) {
		dev_err(dev, "Invalid dmabuf size %ld %ld", dbuf->size, size);
		return ERR_PTR(-EFAULT);
	}

	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	struct vb2_amdisp_buf *dbg_buf = (struct vb2_amdisp_buf *)dbuf->priv;

	buf->dev = dev;
	buf->dbuf = dbuf;
	buf->dma_dir = vb->vb2_queue->dma_dir;
	buf->size = size;

	dev_dbg(dev, "attach dmabuf of isp user bo 0x%llx size %ld",
		dbg_buf->gpu_addr, dbg_buf->size);

	return buf;
}

static void vb2_amdisp_unmap_dmabuf(void *mem_priv)
{
	struct vb2_amdisp_buf *buf = mem_priv;

	if (!buf) {
		pr_err("Invalid buf handle");
		return;
	}

	struct iosys_map map = IOSYS_MAP_INIT_VADDR(buf->vaddr);

	dev_dbg(buf->dev, "unmap dmabuf of isp user bo 0x%llx size %ld",
		buf->gpu_addr, buf->size);

	dma_buf_vunmap_unlocked(buf->dbuf, &map);
	buf->vaddr = NULL;
}

static int vb2_amdisp_map_dmabuf(void *mem_priv)
{
	struct vb2_amdisp_buf *buf = mem_priv;
	struct iosys_map map;
	int ret;
	struct vb2_amdisp_buf *mmap_buf = NULL;

	memset(&map, 0x0, sizeof(map));

	if (!buf) {
		pr_err("Invalid buf handle");
		return -EINVAL;
	}

	ret = dma_buf_vmap_unlocked(buf->dbuf, &map);
	if (ret) {
		dev_err(buf->dev, "vmap_unlocked failed");
		return -EFAULT;
	}
	buf->vaddr = map.vaddr;

	mmap_buf = (struct vb2_amdisp_buf *)buf->dbuf->priv;
	buf->gpu_addr = mmap_buf->gpu_addr;

	dev_dbg(buf->dev, "map dmabuf of isp user bo 0x%llx size %ld",
		buf->gpu_addr, buf->size);

	return 0;
}

#ifdef CONFIG_HAS_DMA
struct vb2_amdgpu_attachment {
	struct sg_table sgt;
	enum dma_data_direction dma_dir;
};

static int vb2_amdisp_dmabuf_ops_attach(struct dma_buf *dbuf,
					struct dma_buf_attachment *dbuf_attach)
{
	struct vb2_amdgpu_attachment *attach;
	struct vb2_amdisp_buf *buf = dbuf->priv;
	int num_pages = PAGE_ALIGN(buf->size) / PAGE_SIZE;
	struct sg_table *sgt;
	struct scatterlist *sg;
	void *vaddr = buf->vaddr;
	int ret;
	int i;

	attach = kzalloc(sizeof(*attach), GFP_KERNEL);
	if (!attach)
		return -ENOMEM;

	sgt = &attach->sgt;
	ret = sg_alloc_table(sgt, num_pages, GFP_KERNEL);
	if (ret) {
		kfree(attach);
		return ret;
	}
	for_each_sgtable_sg(sgt, sg, i) {
		struct page *page = vmalloc_to_page(vaddr);

		if (!page) {
			sg_free_table(sgt);
			kfree(attach);
			return -ENOMEM;
		}
		sg_set_page(sg, page, PAGE_SIZE, 0);
		vaddr = ((char *)vaddr) + PAGE_SIZE;
	}

	attach->dma_dir = DMA_NONE;
	dbuf_attach->priv = attach;

	return 0;
}

static void vb2_amdisp_dmabuf_ops_detach(struct dma_buf *dbuf,
					 struct dma_buf_attachment *dbuf_attach)
{
	struct vb2_amdgpu_attachment *attach = dbuf_attach->priv;
	struct sg_table *sgt;

	if (!attach) {
		pr_err("invalid attach handler");
		return;
	}

	sgt = &attach->sgt;

	/* release the scatterlist cache */
	if (attach->dma_dir != DMA_NONE)
		dma_unmap_sgtable(dbuf_attach->dev, sgt, attach->dma_dir, 0);
	sg_free_table(sgt);
	kfree(attach);
	dbuf_attach->priv = NULL;
}

static struct sg_table
*vb2_amdisp_dmabuf_ops_map(struct dma_buf_attachment *dbuf_attach,
			   enum dma_data_direction dma_dir)
{
	struct vb2_amdgpu_attachment *attach = dbuf_attach->priv;
	struct sg_table *sgt;

	sgt = &attach->sgt;
	/* return previously mapped sg table */
	if (attach->dma_dir == dma_dir)
		return sgt;

	/* release any previous cache */
	if (attach->dma_dir != DMA_NONE) {
		dma_unmap_sgtable(dbuf_attach->dev, sgt, attach->dma_dir, 0);
		attach->dma_dir = DMA_NONE;
	}

	/* mapping to the client with new direction */
	if (dma_map_sgtable(dbuf_attach->dev, sgt, dma_dir, 0)) {
		dev_err(dbuf_attach->dev, "failed to map scatterlist");
		return ERR_PTR(-EIO);
	}

	attach->dma_dir = dma_dir;

	return sgt;
}

static void vb2_amdisp_dmabuf_ops_unmap(struct dma_buf_attachment *dbuf_attach,
					struct sg_table *sgt,
					enum dma_data_direction dma_dir)
{
	/* nothing to be done here */
}

static int vb2_amdisp_dmabuf_ops_vmap(struct dma_buf *dbuf,
				      struct iosys_map *map)
{
	struct vb2_amdisp_buf *buf = dbuf->priv;

	iosys_map_set_vaddr(map, buf->vaddr);

	return 0;
}

static int vb2_amdisp_dmabuf_ops_mmap(struct dma_buf *dbuf,
				      struct vm_area_struct *vma)
{
	return vb2_amdisp_mmap(dbuf->priv, vma);
}

static void vb2_amdisp_dmabuf_ops_release(struct dma_buf *dbuf)
{
	struct vb2_amdisp_buf *buf = dbuf->priv;

	/* drop reference obtained in vb2_amdisp_get_dmabuf */
	if (buf->is_expbuf)
		vb2_amdisp_put(dbuf->priv);
	else
		dev_dbg(buf->dev, "ignore buf release for implicit case");
}

static const struct dma_buf_ops vb2_amdisp_dmabuf_ops = {
	.attach = vb2_amdisp_dmabuf_ops_attach,
	.detach = vb2_amdisp_dmabuf_ops_detach,
	.map_dma_buf = vb2_amdisp_dmabuf_ops_map,
	.unmap_dma_buf = vb2_amdisp_dmabuf_ops_unmap,
	.vmap = vb2_amdisp_dmabuf_ops_vmap,
	.mmap = vb2_amdisp_dmabuf_ops_mmap,
	.release = vb2_amdisp_dmabuf_ops_release,
};

static struct dma_buf *get_dmabuf(struct vb2_buffer *vb,
				  void *buf_priv,
				  unsigned long flags)
{
	struct vb2_amdisp_buf *buf = buf_priv;
	struct dma_buf *dbuf;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);

	if (!buf) {
		pr_err("Invalid buf handle");
		return ERR_PTR(-EINVAL);
	}

	exp_info.ops = &vb2_amdisp_dmabuf_ops;
	exp_info.size = buf->size;
	exp_info.flags = flags;
	exp_info.priv = buf;

	if (WARN_ON(!buf->vaddr))
		return NULL;

	dbuf = dma_buf_export(&exp_info);
	if (IS_ERR(dbuf))
		return NULL;

	return dbuf;
}

static struct dma_buf *vb2_amdisp_get_dmabuf(struct vb2_buffer *vb,
					     void *buf_priv,
					     unsigned long flags)
{
	struct vb2_amdisp_buf *buf = buf_priv;
	struct dma_buf *dbuf;

	if (buf->dbuf) {
		dev_dbg(buf->dev, "dbuf already created, reuse implicit dbuf");
		dbuf = buf->dbuf;
	} else {
		dbuf = get_dmabuf(vb, buf_priv, flags);
		dev_dbg(buf->dev, "created new dbuf");
	}
	buf->is_expbuf = true;
	refcount_inc(&buf->refcount);

	dev_dbg(buf->dev, "buf exported, refcount %d", buf->refcount.refs.counter);

	return dbuf;
}

#endif

static void vb2_amdisp_put_userptr(void *buf_priv)
{
	struct vb2_amdisp_buf *buf = buf_priv;

	if (!buf->vec->is_pfns) {
		unsigned long vaddr = (unsigned long)buf->vaddr & PAGE_MASK;
		unsigned int n_pages;

		n_pages = frame_vector_count(buf->vec);
		if (vaddr)
			vm_unmap_ram((void *)vaddr, n_pages);
		if (buf->dma_dir == DMA_FROM_DEVICE ||
		    buf->dma_dir == DMA_BIDIRECTIONAL) {
			struct page **pages;

			pages = frame_vector_pages(buf->vec);
			if (!WARN_ON_ONCE(IS_ERR(pages))) {
				unsigned int i;

				for (i = 0; i < n_pages; i++)
					set_page_dirty_lock(pages[i]);
			}
		}
	} else {
		iounmap((__force void __iomem *)buf->vaddr);
	}
	vb2_destroy_framevec(buf->vec);
	kfree(buf);
}

static void *vb2_amdisp_get_userptr(struct vb2_buffer *vb, struct device *dev,
				    unsigned long vaddr, unsigned long size)
{
	struct vb2_amdisp_buf *buf;
	struct frame_vector *vec;
	int n_pages, offset, i;
	int ret = -ENOMEM;

	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	buf->dev = dev;
	buf->dma_dir = vb->vb2_queue->dma_dir;
	offset = vaddr & ~PAGE_MASK;
	buf->size = size;
	vec = vb2_create_framevec(vaddr, size,
				  buf->dma_dir == DMA_FROM_DEVICE ||
				  buf->dma_dir == DMA_BIDIRECTIONAL);
	if (IS_ERR(vec)) {
		ret = PTR_ERR(vec);
		goto fail_pfnvec_create;
	}
	buf->vec = vec;
	n_pages = frame_vector_count(vec);
	if (frame_vector_to_pages(vec) < 0) {
		unsigned long *nums = frame_vector_pfns(vec);

		/*
		 * We cannot get page pointers for these pfns. Check memory is
		 * physically contiguous and use direct mapping.
		 */
		for (i = 1; i < n_pages; i++)
			if (nums[i - 1] + 1 != nums[i])
				goto fail_map;
		buf->vaddr = (__force void *)
			     ioremap(__pfn_to_phys(nums[0]), size + offset);
	} else {
		buf->vaddr = vm_map_ram(frame_vector_pages(vec), n_pages, -1);
	}

	if (!buf->vaddr)
		goto fail_map;

	buf->vaddr = ((char *)buf->vaddr) + offset;
	return buf;

fail_map:
	vb2_destroy_framevec(vec);
fail_pfnvec_create:
	kfree(buf);

	return ERR_PTR(ret);
}

static void vb2_amdisp_put(void *buf_priv)
{
	struct vb2_amdisp_buf *buf = (struct vb2_amdisp_buf *)buf_priv;
	struct amdgpu_bo *bo = (struct amdgpu_bo *)buf->bo;

	dev_dbg(buf->dev, "release isp user bo 0x%llx size %ld refcount %d is_expbuf %d",
		buf->gpu_addr, buf->size,
		buf->refcount.refs.counter, buf->is_expbuf);

	if (refcount_dec_and_test(&buf->refcount)) {
		amdgpu_bo_free_isp_user(bo);

		// put implicit dmabuf here, detach_dmabuf will not be called
		if (!buf->is_expbuf)
			dma_buf_put(buf->dbuf);

		vfree(buf->vaddr);
		kfree(buf);
		buf = NULL;
	} else {
		dev_warn(buf->dev, "ignore buffer free, refcount %u > 0",
			 refcount_read(&buf->refcount));
	}
}

static void *vb2_amdisp_alloc(struct vb2_buffer *vb, struct device *dev,
			      unsigned long size)
{
	struct vb2_amdisp_buf *buf = NULL;
	struct amdgpu_bo *bo;
	u64 gpu_addr;
	u32 ret;
	struct amd_cam *cam = dev_get_drvdata(dev);

	buf = kzalloc(sizeof(*buf), GFP_KERNEL | vb->vb2_queue->gfp_flags);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	if (!cam) {
		dev_err(dev, "Invalid cam handle");
		return ERR_PTR(-EINVAL);
	}

	buf->dev = dev;
	buf->size = size;
	buf->vaddr = vmalloc_user(buf->size);
	if (!buf->vaddr) {
		kfree(buf);
		return ERR_PTR(-ENOMEM);
	}

	buf->dma_dir = vb->vb2_queue->dma_dir;
	buf->handler.refcount = &buf->refcount;
	buf->handler.put = vb2_amdisp_put;
	buf->handler.arg = buf;

	// get implicit dmabuf
	buf->dbuf = get_dmabuf(vb, buf, 0);
	if (!buf->dbuf) {
		dev_err(dev, "Failed to get dmabuf");
		return ERR_PTR(-EINVAL);
	}

	// create isp user BO and obtain gpu_addr
	ret = amdgpu_bo_create_isp_user(cam->pltf_data->adev, buf->dbuf,
					AMDGPU_GEM_DOMAIN_GTT, &bo, &gpu_addr);
	if (ret) {
		dev_err(dev, "Failed to create BO");
		return ERR_PTR(-EINVAL);
	}

	buf->bo = (void *)bo;
	buf->gpu_addr = gpu_addr;

	refcount_set(&buf->refcount, 1);

	dev_dbg(dev, "allocated isp user bo 0x%llx size %ld refcount %d",
		buf->gpu_addr, buf->size, buf->refcount.refs.counter);

	return buf;
}

const struct vb2_mem_ops vb2_amdisp_memops = {
	.alloc		= vb2_amdisp_alloc,
	.put		= vb2_amdisp_put,
	.get_userptr	= vb2_amdisp_get_userptr,
	.put_userptr	= vb2_amdisp_put_userptr,
#ifdef CONFIG_HAS_DMA
	.get_dmabuf	= vb2_amdisp_get_dmabuf,
#endif
	.map_dmabuf	= vb2_amdisp_map_dmabuf,
	.unmap_dmabuf	= vb2_amdisp_unmap_dmabuf,
	.attach_dmabuf	= vb2_amdisp_attach_dmabuf,
	.detach_dmabuf	= vb2_amdisp_detach_dmabuf,
	.vaddr		= vb2_amdisp_vaddr,
	.mmap		= vb2_amdisp_mmap,
	.num_users	= vb2_amdisp_num_users,
};

static const struct v4l2_pix_format fmt_default = {
	.width = 1920,
	.height = 1080,
	.pixelformat = V4L2_PIX_FMT_NV12,
	.field = V4L2_FIELD_NONE,
	.colorspace = V4L2_COLORSPACE_SRGB,
};

static void isp4_capture_return_all_buffers(struct isp4_video_dev *ctx,
					    enum vb2_buffer_state state)
{
	struct isp4_capture_buffer *vbuf, *node;
	struct device *dev = &ctx->cam->pdev->dev;

	spin_lock(&ctx->qlock);

	list_for_each_entry_safe(vbuf, node, &ctx->buf_list, list) {
		list_del(&vbuf->list);
		vb2_buffer_done(&vbuf->vb2.vb2_buf, state);
	}
	spin_unlock(&ctx->qlock);

	dev_dbg(dev, "call vb2_buffer_done(%d)", state);
}

static int isp4_vdev_link_validate(struct media_link *link)
{
	return 0;
}

static const struct media_entity_operations isp_vdev_ent_ops = {
	.link_validate = isp4_vdev_link_validate,
};

static int isp4_subdev_link_validate(struct media_link *link)
{
	return 0;
}

static const struct media_entity_operations isp4_subdev_ent_ops = {
	.link_validate = isp4_subdev_link_validate,
};

static long isp4_subdev_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	return -ENOIOCTLCMD;
}

static int isp4_subdev_s_stream(struct v4l2_subdev *sd, int enable)
{
	return 0;
}

static int isp4_subdev_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	return 0;
}

static const struct v4l2_subdev_core_ops isp4_subdev_core_ops = {
	.ioctl = isp4_subdev_ioctl,
};

static const struct v4l2_subdev_video_ops isp4_subdev_video_ops = {
	.s_stream = isp4_subdev_s_stream,
};

static const struct v4l2_subdev_ops isp4_subdev_ops = {
	.core = &isp4_subdev_core_ops,
	.video = &isp4_subdev_video_ops,
};

static const struct v4l2_subdev_internal_ops isp4_subdev_internal_ops = {
	.open = isp4_subdev_open,
};

static int isp4_fop_open(struct file *file)
{
	return v4l2_fh_open(file);
}

static int isp4_fop_release(struct file *file)
{
	return vb2_fop_release(file);
}

static ssize_t isp4_fop_read(struct file *file, char __user *buf, size_t count,
			     loff_t *ppos)
{
	return vb2_fop_read(file, buf, count, ppos);
}

static __poll_t isp4_fop_poll(struct file *file, poll_table *wait)
{
	return vb2_fop_poll(file, wait);
}

static long isp4_fop_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
	return video_ioctl2(file, cmd, arg);
}

static int isp4_fop_mmap(struct file *file, struct vm_area_struct *vma)
{
	return vb2_fop_mmap(file, vma);
}

static const struct v4l2_file_operations isp4_vdev_fops = {
	.owner = THIS_MODULE,
	.open = isp4_fop_open,
	.release = isp4_fop_release,
	.read = isp4_fop_read,
	.poll = isp4_fop_poll,
	.unlocked_ioctl = isp4_fop_ioctl,
	.mmap = isp4_fop_mmap,
};

static int isp4_ioctl_querycap(struct file *file, void *fh,
			       struct v4l2_capability *cap)
{
	struct isp4_video_dev *ctx = video_drvdata(file);
	struct device *dev = &ctx->cam->pdev->dev;

	strscpy(cap->driver, ISP_DRV_NAME, sizeof(cap->driver));
	snprintf(cap->card, sizeof(cap->card), "%s", ISP_DRV_NAME);
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%s", ISP_DRV_NAME);

	cap->capabilities |= (V4L2_CAP_STREAMING |
			      V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_IO_MC);

	dev_dbg(dev, "%s|capabilities=0x%X", ctx->vdev.name, cap->capabilities);

	return 0;
}

static int isp4_ioctl_reqbufs(struct file *file, void *fh,
			      struct v4l2_requestbuffers *b)
{
	return vb2_ioctl_reqbufs(file, fh, b);
}

static int isp4_ioctl_querybuf(struct file *file, void *fh,
			       struct v4l2_buffer *b)
{
	return vb2_ioctl_querybuf(file, fh, b);
}

static int isp4_ioctl_qbuf(struct file *file, void *fh, struct v4l2_buffer *b)
{
	return vb2_ioctl_qbuf(file, fh, b);
}

static int isp4_ioctl_expbuf(struct file *file, void *fh,
			     struct v4l2_exportbuffer *e)
{
	return vb2_ioctl_expbuf(file, fh, e);
}

static int isp4_ioctl_dqbuf(struct file *file, void *fh, struct v4l2_buffer *b)
{
	return vb2_ioctl_dqbuf(file, fh, b);
}

static int isp4_ioctl_create_bufs(struct file *file, void *fh,
				  struct v4l2_create_buffers *b)
{
	return vb2_ioctl_create_bufs(file, fh, b);
}

static int isp4_ioctl_prepare_buf(struct file *file, void *fh,
				  struct v4l2_buffer *b)
{
	return vb2_ioctl_prepare_buf(file, fh, b);
}

static int isp4_ioctl_streamon(struct file *file, void *fh,
			       enum v4l2_buf_type i)
{
	return vb2_ioctl_streamon(file, fh, i);
}

static int isp4_ioctl_streamoff(struct file *file, void *fh,
				enum v4l2_buf_type i)
{
	return vb2_ioctl_streamoff(file, fh, i);
}

static int isp4_g_fmt_vid_cap(struct file *file, void *priv,
			      struct v4l2_format *f)
{
	struct isp4_video_dev *ctx = video_drvdata(file);

	f->fmt.pix = ctx->format;

	return 0;
}

static int isp4_try_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct isp4_video_dev *ctx = video_drvdata(file);
	struct device *dev = &ctx->cam->pdev->dev;
	struct v4l2_pix_format *format = &f->fmt.pix;

	switch (format->pixelformat) {
	case V4L2_PIX_FMT_NV12: {
		const struct v4l2_frmsize_discrete *fsz =
			v4l2_find_nearest_size(isp_frmsize,
					       ARRAY_SIZE(isp_frmsize),
					       width, height,
					       format->width, format->height);

		format->width = fsz->width;
		format->height = fsz->height;

		format->bytesperline = format->width;
		format->sizeimage = format->bytesperline *
				    format->height * 3 / 2;
	}
	break;
	case V4L2_PIX_FMT_YUYV: {
		const struct v4l2_frmsize_discrete *fsz =
			v4l2_find_nearest_size(isp_frmsize,
					       ARRAY_SIZE(isp_frmsize),
					       width, height,
					       format->width, format->height);

		format->width = fsz->width;
		format->height = fsz->height;

		format->bytesperline = format->width * 2;
		format->sizeimage = format->bytesperline * format->height;
	}
	break;
	default:
		dev_err(dev, "%s|unsupported fmt=%u",
			ctx->vdev.name, format->pixelformat);
		return -EINVAL;
	}

	if (format->field == V4L2_FIELD_ANY)
		format->field = fmt_default.field;

	if (format->colorspace == V4L2_COLORSPACE_DEFAULT)
		format->colorspace = fmt_default.colorspace;

	return 0;
}

static int isp4_s_fmt_vid_cap(struct file *file, void *priv,
			      struct v4l2_format *f)
{
	int ret;
	struct isp4_video_dev *ctx = video_drvdata(file);
	enum stream_id stream_id = get_vdev_stream_id(ctx);
	struct device *dev = &ctx->cam->pdev->dev;

	/* Do not change the format while stream is on */
	if (vb2_is_busy(&ctx->vbq))
		return -EBUSY;

	ret = isp4_try_fmt_vid_cap(file, priv, f);
	if (ret)
		return ret;

	dev_dbg(dev, "%s|width height:%ux%u->%ux%u",
		ctx->vdev.name,
		ctx->format.width, ctx->format.height,
		f->fmt.pix.width, f->fmt.pix.height);
	dev_dbg(dev, "%s|pixelformat:0x%x-0x%x",
		ctx->vdev.name, ctx->format.pixelformat,
		f->fmt.pix.pixelformat);
	dev_dbg(dev, "%s|bytesperline:%u->%u",
		ctx->vdev.name, ctx->format.bytesperline,
		f->fmt.pix.bytesperline);
	dev_dbg(dev, "%s|sizeimage:%u->%u",
		ctx->vdev.name, ctx->format.sizeimage,
		f->fmt.pix.sizeimage);

	ctx->format = f->fmt.pix;

	enum pvt_img_fmt isp_fmt;
	struct pvt_img_res_fps_pitch isp_res_fps_pitch = {
		.width = ctx->format.width,
		.height = ctx->format.height,
		.fps = ctx->timeperframe.denominator / ctx->timeperframe.numerator,
	};

	switch (ctx->format.pixelformat) {
	case V4L2_PIX_FMT_NV12: {
		isp_fmt = PVT_IMG_FMT_NV12;
		isp_res_fps_pitch.luma_pitch = isp_res_fps_pitch.width;
		isp_res_fps_pitch.chroma_pitch = isp_res_fps_pitch.width / 2;
	}
	break;
	case V4L2_PIX_FMT_YUYV: {
		isp_fmt = PVT_IMG_FMT_YUV422_INTERLEAVED;
		isp_res_fps_pitch.luma_pitch = isp_res_fps_pitch.width * 2;
		isp_res_fps_pitch.chroma_pitch = 0;
	}
	break;
	default:
		dev_err(dev, "%s|unsupported fmt=%u",
			ctx->vdev.name, ctx->format.pixelformat);
		return -EINVAL;
	}

	isp_intf_set_stream_param(&ctx->cam->ispctx, CAMERA_PORT_0,
				  stream_id,
				  PARA_ID_DATA_FORMAT,
				  &isp_fmt);

	isp_intf_set_stream_param(&ctx->cam->ispctx, CAMERA_PORT_0,
				  stream_id,
				  PARA_ID_DATA_RES_FPS_PITCH,
				  &isp_res_fps_pitch);

	return 0;
}

static int isp4_enum_fmt_vid_cap(struct file *file, void *priv,
				 struct v4l2_fmtdesc *f)
{
	struct isp4_video_dev *ctx = video_drvdata(file);
	struct device *dev = &ctx->cam->pdev->dev;

	switch (f->index) {
	case 0:
		f->pixelformat = V4L2_PIX_FMT_NV12;
		break;
	case 1:
		f->pixelformat = V4L2_PIX_FMT_YUYV;
		break;
	default:
		return -EINVAL;
	}

	dev_dbg(dev, "%s|index=%d, pixelformat=0x%X", ctx->vdev.name,
		f->index, f->pixelformat);

	return 0;
}

static int isp4_enum_framesizes(struct file *file, void *fh,
				struct v4l2_frmsizeenum *fsize)
{
	struct isp4_video_dev *ctx = video_drvdata(file);
	struct device *dev = &ctx->cam->pdev->dev;

	if (fsize->index < ARRAY_SIZE(isp_frmsize)) {
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		fsize->discrete = isp_frmsize[fsize->index];

		dev_dbg(dev, "%s|size[%d]=%dx%d", ctx->vdev.name, fsize->index,
			fsize->discrete.width, fsize->discrete.height);
	} else {
		return -EINVAL;
	}

	return 0;
}

static int isp4_ioctl_enum_frameintervals(struct file *file, void *priv,
					  struct v4l2_frmivalenum *fival)
{
	struct isp4_video_dev *ctx = video_drvdata(file);
	struct device *dev = &ctx->cam->pdev->dev;
	int i;

	if (fival->index >= ARRAY_SIZE(tpfs))
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(formats); i++)
		if (formats[i] == fival->pixel_format)
			break;
	if (i == ARRAY_SIZE(formats))
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(isp_frmsize); i++)
		if (isp_frmsize[i].width == fival->width &&
		    isp_frmsize[i].height == fival->height)
			break;
	if (i == ARRAY_SIZE(isp_frmsize))
		return -EINVAL;

	fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	fival->discrete = tpfs[fival->index];
	v4l2_simplify_fraction(&fival->discrete.numerator,
			       &fival->discrete.denominator, 8, 333);

	dev_dbg(dev, "%s|interval[%d]=%d/%d", ctx->vdev.name, fival->index,
		fival->discrete.numerator,
		fival->discrete.denominator);

	return 0;
}

static int isp4_ioctl_g_parm(struct file *file, void *priv,
			     struct v4l2_streamparm *parm)
{
	struct isp4_video_dev *ctx = video_drvdata(file);
	struct device *dev = &ctx->cam->pdev->dev;

	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	parm->parm.capture.capability   = V4L2_CAP_TIMEPERFRAME;
	parm->parm.capture.timeperframe = ctx->timeperframe;
	parm->parm.capture.readbuffers  = ctx->vdev.queue->min_queued_buffers;

	dev_dbg(dev, "%s|timeperframe=%d/%d", ctx->vdev.name,
		parm->parm.capture.timeperframe.numerator,
		parm->parm.capture.timeperframe.denominator);
	return 0;
}

static int isp4_ioctl_s_parm(struct file *file, void *priv,
			     struct v4l2_streamparm *parm)
{
	struct isp4_video_dev *ctx = video_drvdata(file);
	enum stream_id stream_id = get_vdev_stream_id(ctx);
	struct v4l2_fract tpf_parm = parm->parm.capture.timeperframe;
	int i;

	/* Do not change the param while the stream is on */
	if (vb2_is_busy(&ctx->vbq))
		return -EBUSY;

	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	if (!tpf_parm.numerator || !tpf_parm.denominator)
		return -EINVAL;

	v4l2_simplify_fraction(&tpf_parm.numerator,
			       &tpf_parm.denominator, 8, 333);

	if (tpf_parm.numerator == ctx->timeperframe.numerator ||
	    tpf_parm.denominator == ctx->timeperframe.denominator) {
		return isp4_ioctl_g_parm(file, priv, parm);
	}

	for (i = 0; i < ARRAY_SIZE(tpfs); i++) {
		struct v4l2_fract tpf = tpfs[i];

		v4l2_simplify_fraction(&tpf.numerator,
				       &tpf.denominator, 8, 333);
		if (tpf.denominator == tpf_parm.denominator &&
		    tpf.numerator == tpf_parm.numerator)
			break;
	}

	if (i == ARRAY_SIZE(tpfs))
		return isp4_ioctl_g_parm(file, priv, parm);

	ctx->timeperframe = tpf_parm;

	struct pvt_img_res_fps_pitch isp_res_fps_pitch = {
		.width = ctx->format.width,
		.height = ctx->format.height,
		.fps = ctx->timeperframe.denominator / ctx->timeperframe.numerator,
	};

	isp_intf_set_stream_param(&ctx->cam->ispctx, CAMERA_PORT_0,
				  stream_id,
				  PARA_ID_DATA_RES_FPS_PITCH,
				  &isp_res_fps_pitch);

	return isp4_ioctl_g_parm(file, priv, parm);
}

static const struct v4l2_ioctl_ops isp4_vdev_ioctl_ops = {
	/* VIDIOC_QUERYCAP handler */
	.vidioc_querycap = isp4_ioctl_querycap,

	/* VIDIOC_ENUM_FMT handlers */
	.vidioc_enum_fmt_vid_cap = isp4_enum_fmt_vid_cap,

	/* VIDIOC_G_FMT handlers */
	.vidioc_g_fmt_vid_cap = isp4_g_fmt_vid_cap,

	/* VIDIOC_S_FMT handlers */
	.vidioc_s_fmt_vid_cap = isp4_s_fmt_vid_cap,

	/* VIDIOC_TRY_FMT handlers */
	.vidioc_try_fmt_vid_cap = isp4_try_fmt_vid_cap,

	/* Buffer handlers */
	.vidioc_reqbufs = isp4_ioctl_reqbufs,
	.vidioc_querybuf = isp4_ioctl_querybuf,
	.vidioc_qbuf = isp4_ioctl_qbuf,
	.vidioc_expbuf = isp4_ioctl_expbuf,
	.vidioc_dqbuf = isp4_ioctl_dqbuf,
	.vidioc_create_bufs = isp4_ioctl_create_bufs,
	.vidioc_prepare_buf = isp4_ioctl_prepare_buf,

	/* Stream on/off */
	.vidioc_streamon = isp4_ioctl_streamon,
	.vidioc_streamoff = isp4_ioctl_streamoff,

	/* Stream type-dependent parameter ioctls */
	.vidioc_g_parm        = isp4_ioctl_g_parm,
	.vidioc_s_parm        = isp4_ioctl_s_parm,

	/* Debugging ioctls */
	.vidioc_enum_framesizes = isp4_enum_framesizes,
	.vidioc_enum_frameintervals = isp4_ioctl_enum_frameintervals,

};

static int isp4_qops_queue_setup(struct vb2_queue *vq, unsigned int *nbuffers,
				 unsigned int *nplanes, unsigned int sizes[],
				 struct device *alloc_devs[])
{
	struct isp4_video_dev *ctx = vb2_get_drv_priv(vq);
	unsigned int q_num_bufs = vb2_get_num_buffers(vq);
	struct device *dev = &ctx->cam->pdev->dev;

	if (q_num_bufs + *nbuffers < VIDEO_BUF_NUM)
		*nbuffers = VIDEO_BUF_NUM - q_num_bufs;

	switch (ctx->format.pixelformat) {
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_YUYV: {
		*nplanes = 1;
		sizes[0] = ctx->format.sizeimage;
	}
	break;
	default:
		dev_err(dev, "%s|unsupported fmt=%u",
			ctx->vdev.name, ctx->format.pixelformat);
		return -EINVAL;
	}

	dev_dbg(dev, "%s|*nbuffers=%u *nplanes=%u sizes[0]=%u", ctx->vdev.name,
		*nbuffers, *nplanes, sizes[0]);

	return 0;
}

static int isp4_qops_buffer_init(struct vb2_buffer *vb)
{
	return 0;
}

static int isp4_qops_buffer_prepare(struct vb2_buffer *vb)
{
	return 0;
}

static void isp4_qops_buffer_queue(struct vb2_buffer *vb)
{
	struct isp4_video_dev *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct isp4_capture_buffer *buf =
		container_of(vb, struct isp4_capture_buffer, vb2.vb2_buf);
	enum stream_id stream_id = get_vdev_stream_id(ctx);
	struct device *dev = &ctx->cam->pdev->dev;

	dev_dbg(dev, "%s|index=%u", ctx->vdev.name, vb->index);

	if (ctx->fw_run) {
		/* get vb2_amdisp_buf */
		struct vb2_amdisp_buf *priv_buf = vb->planes[0].mem_priv;
		struct sys_img_buf_info img_buf;

		dev_dbg(dev, "queue isp user bo 0x%llx size=%lu",
			priv_buf->gpu_addr,
			priv_buf->size);

		switch (ctx->format.pixelformat) {
		case V4L2_PIX_FMT_NV12: {
			u32 y_size = ctx->format.sizeimage / 3 * 2;
			u32 uv_size = ctx->format.sizeimage / 3;

			img_buf.planes[0].len = y_size;
			img_buf.planes[0].sys_addr = priv_buf->vaddr;
			img_buf.planes[0].mc_addr = priv_buf->gpu_addr;

			dev_dbg(dev, "img_buf[0]: mc=0x%llx size=%u",
				img_buf.planes[0].mc_addr,
				img_buf.planes[0].len);

			img_buf.planes[1].len = uv_size;
			img_buf.planes[1].sys_addr =
				(void *)((u64)priv_buf->vaddr + y_size);
			img_buf.planes[1].mc_addr = priv_buf->gpu_addr + y_size;

			dev_dbg(dev, "img_buf[1]: mc=0x%llx size=%u",
				img_buf.planes[1].mc_addr,
				img_buf.planes[1].len);

			img_buf.planes[2].len = 0;
		}
		break;
		case V4L2_PIX_FMT_YUYV: {
			img_buf.planes[0].len = ctx->format.sizeimage;
			img_buf.planes[0].sys_addr = priv_buf->vaddr;
			img_buf.planes[0].mc_addr = priv_buf->gpu_addr;

			dev_dbg(dev, "img_buf[0]: mc=0x%llx size=%u",
				img_buf.planes[0].mc_addr,
				img_buf.planes[0].len);

			img_buf.planes[1].len = 0;
			img_buf.planes[2].len = 0;
		}
		break;
		default:
			dev_err(dev, "%s|unsupported fmt=%u",
				ctx->vdev.name, ctx->format.pixelformat);
			return;
		}

		isp_intf_set_stream_buf(&ctx->cam->ispctx, CAMERA_PORT_0,
					stream_id, &img_buf);
	}

	spin_lock(&ctx->qlock);
	list_add_tail(&buf->list, &ctx->buf_list);
	spin_unlock(&ctx->qlock);
}

static void isp4_qops_buffer_finish(struct vb2_buffer *vb)
{
}

static void isp4_qops_buffer_cleanup(struct vb2_buffer *vb)
{
	struct isp4_video_dev *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_amdisp_buf *buf = vb->planes[0].mem_priv;
	struct device *dev = &ctx->cam->pdev->dev;

	dev_dbg(dev, "%s|index=%u vb->memory %u", ctx->vdev.name,
		vb->index, vb->memory);

	if (!buf) {
		dev_err(dev, "Invalid buf handle");
		return;
	}

	// release implicit dmabuf reference here for vb2 buffer
	// of type MMAP and is exported
	if (vb->memory == VB2_MEMORY_MMAP && buf->is_expbuf) {
		dma_buf_put(buf->dbuf);
		dev_dbg(dev, "put dmabuf for vb->memory %d expbuf %d",
			vb->memory, buf->is_expbuf);
	}
}

static int isp4_qops_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	int ret;
	struct isp4_video_dev *ctx = vb2_get_drv_priv(vq);
	enum stream_id stream_id = get_vdev_stream_id(ctx);
	struct device *dev = &ctx->cam->pdev->dev;

	if (ctx->fw_run & (1 << stream_id)) {
		dev_info(dev, "%s(fw_run:%u)|start_streaming, do none for already",
			 ctx->vdev.name, ctx->fw_run);
		return 0;
	}

	dev_info(dev, "%s(fw_run:%u)|start_streaming", ctx->vdev.name, ctx->fw_run);

	if (!ctx->fw_run) {
		isp_intf_reg_notify_cb(&ctx->cam->ispctx, CAMERA_PORT_0,
				       isp_module_notify_cb, ctx->cam);
		isp_intf_open_camera(&ctx->cam->ispctx, CAMERA_PORT_0, 0, 0);
		ctx->sequence = 0;
	}

	if (!use_embedded_sensor) {
		dev_info(dev, "starting Phy");
		ret = isp_mipi_phy_start(&ctx->cam->ispctx, PHY_DEVICE_ID,
					 PHY_BIT_RATE, PHY_NUM_OF_LANES);
		if (ret) {
			dev_err(dev, "failed to start the Phy:%d", ret);
			return ret;
		}

		if (ctx->cam->sensor) {
			dev_info(dev, "starting camera sensor");
			ret = v4l2_subdev_call(ctx->cam->sensor, video, s_stream, 1);
			if (ret) {
				dev_err(dev, "camera sensor start streaming failed:%d", ret);
				return ret;
			}
		}
	}

	ctx->fw_run |= (1 << stream_id);
	isp_intf_start_stream(&ctx->cam->ispctx, CAMERA_PORT_0, stream_id);

	struct isp4_capture_buffer *isp_buf, *tmp;
	struct vb2_amdisp_buf *priv_buf;
	struct sys_img_buf_info img_buf;

	switch (ctx->format.pixelformat) {
	case V4L2_PIX_FMT_NV12: {
		u32 y_size = ctx->format.sizeimage / 3 * 2;
		u32 uv_size = ctx->format.sizeimage / 3;

		list_for_each_entry_safe(isp_buf, tmp, &ctx->buf_list, list) {
			/* get vb2_amdisp_buf */
			priv_buf = isp_buf->vb2.vb2_buf.planes[0].mem_priv;

			dev_dbg(dev, "isp user bo 0x%llx size=%lu",
				priv_buf->gpu_addr, priv_buf->size);

			img_buf.planes[0].len = y_size;
			img_buf.planes[0].sys_addr = priv_buf->vaddr;
			img_buf.planes[0].mc_addr = priv_buf->gpu_addr;

			dev_dbg(dev, "img_buf[0]: mc=0x%llx size=%u",
				img_buf.planes[0].mc_addr,
				img_buf.planes[0].len);

			img_buf.planes[1].len = uv_size;
			img_buf.planes[1].sys_addr = (void *)((u64)priv_buf->vaddr + y_size);
			img_buf.planes[1].mc_addr = priv_buf->gpu_addr + y_size;

			dev_dbg(dev, "img_buf[1]: mc=0x%llx size=%u",
				img_buf.planes[1].mc_addr,
				img_buf.planes[1].len);

			img_buf.planes[2].len = 0;

			isp_intf_set_stream_buf(&ctx->cam->ispctx, CAMERA_PORT_0,
						stream_id, &img_buf);
		}
	}
	break;
	case V4L2_PIX_FMT_YUYV: {
		list_for_each_entry_safe(isp_buf, tmp, &ctx->buf_list, list) {
			/* get vb2_amdisp_buf */
			priv_buf = isp_buf->vb2.vb2_buf.planes[0].mem_priv;

			dev_dbg(dev, "isp user bo 0x%llx size=%lu",
				priv_buf->gpu_addr, priv_buf->size);

			img_buf.planes[0].len = ctx->format.sizeimage;
			img_buf.planes[0].sys_addr = priv_buf->vaddr;
			img_buf.planes[0].mc_addr = priv_buf->gpu_addr;

			dev_dbg(dev, "img_buf[0]: mc=0x%llx size=%u",
				img_buf.planes[0].mc_addr,
				img_buf.planes[0].len);

			img_buf.planes[1].len = 0;
			img_buf.planes[2].len = 0;

			isp_intf_set_stream_buf(&ctx->cam->ispctx, CAMERA_PORT_0,
						stream_id, &img_buf);
		}
	}
	break;
	default:
		dev_err(dev, "%s|unsupported fmt=0x%x",
			ctx->vdev.name, ctx->format.pixelformat);
		return -EINVAL;
	}

	/* Start the media pipeline */
	ret = video_device_pipeline_start(&ctx->vdev, &ctx->pipe);
	if (ret) {
		dev_err(dev, "video_device_pipeline_start failed:%d", ret);
		isp4_capture_return_all_buffers(ctx, VB2_BUF_STATE_QUEUED);
		return ret;
	}

	return 0;
}

static void isp4_qops_stop_streaming(struct vb2_queue *vq)
{
	struct isp4_video_dev *ctx = vb2_get_drv_priv(vq);
	enum stream_id stream_id = get_vdev_stream_id(ctx);
	struct device *dev = &ctx->cam->pdev->dev;

	if (!(ctx->fw_run & (1 << stream_id))) {
		dev_info(dev, "%s(fw_run:%u)|stop_streaming, do none for not run",
			 ctx->vdev.name, ctx->fw_run);
		return;
	}
	dev_info(dev, "%s(fw_run:%u)|stop_streaming", ctx->vdev.name, ctx->fw_run);

	if (!use_embedded_sensor) {
		int ret;

		if (ctx->cam->sensor) {
			dev_info(dev, "stopping camera sensor");
			ret = v4l2_subdev_call(ctx->cam->sensor, video, s_stream, 0);
			if (ret)
				dev_err(dev, "camera sensor stop streaming failed:%d", ret);
		}

		dev_info(dev, "stopping Phy");
		ret = isp_mipi_phy_stop(&ctx->cam->ispctx, PHY_DEVICE_ID);
		if (ret)
			dev_err(dev, "failed to stop the Phy:%d", ret);
	}

	isp_intf_stop_stream(&ctx->cam->ispctx, CAMERA_PORT_0, stream_id);
	ctx->fw_run &= ~(1 << stream_id);

	if (!ctx->fw_run) {
		isp_intf_unreg_notify_cb(&ctx->cam->ispctx, CAMERA_PORT_0);
		isp_intf_close_camera(&ctx->cam->ispctx, CAMERA_PORT_0);
	}

	/* Stop the media pipeline */
	video_device_pipeline_stop(&ctx->vdev);

	/* Release all active buffers */
	isp4_capture_return_all_buffers(ctx, VB2_BUF_STATE_ERROR);
}

static void isp4_qops_wait_prepare(struct vb2_queue *vq)
{
	vb2_ops_wait_prepare(vq);
}

static void isp4_qops_wait_finish(struct vb2_queue *vq)
{
	vb2_ops_wait_finish(vq);
}

static const struct vb2_ops isp4_qops = {
	.queue_setup = isp4_qops_queue_setup,
	.buf_init = isp4_qops_buffer_init,
	.buf_prepare = isp4_qops_buffer_prepare,
	.buf_finish = isp4_qops_buffer_finish,
	.buf_cleanup = isp4_qops_buffer_cleanup,
	.buf_queue = isp4_qops_buffer_queue,
	.start_streaming = isp4_qops_start_streaming,
	.stop_streaming = isp4_qops_stop_streaming,
	.wait_prepare = isp4_qops_wait_prepare,
	.wait_finish = isp4_qops_wait_finish,
};

enum stream_id get_vdev_stream_id(struct isp4_video_dev *vdev)
{
	return STREAM_ID_PREVIEW;
}

#define to_amd_cam(dev) \
	((struct amd_cam *)container_of(dev, struct amd_cam, v4l2_dev))

static int isp4_create_links(struct amd_cam *ctx, struct v4l2_subdev *sdev)
{
	struct device *dev = &ctx->pdev->dev;
	int ret;
	int i;

	for (i = 0; i < ISP4_VDEV_NUM; i++) {
		ret = media_create_pad_link(&sdev->entity,
					    i, &ctx->isp_vdev[i].vdev.entity, 0,
					    MEDIA_LNK_FL_ENABLED |
					    MEDIA_LNK_FL_IMMUTABLE);
		if (ret) {
			dev_err(dev, "media_create_pad_link failed:%d", ret);
			goto out;
		}
	}
out:
	return ret;
}

static int isp4_register_subdev_and_create_links(struct amd_cam *ctx,
						 struct v4l2_subdev *sdev)
{
	struct device *dev = &ctx->pdev->dev;
	int ret;

	ret = isp4_create_links(ctx, sdev);
	if (ret)
		dev_err(dev, "%s failed create isp link:%d", __func__, ret);

	ret = v4l2_device_register_subdev_nodes(&ctx->v4l2_dev);
	if (ret != 0) {
		dev_warn(dev, "register subdev as nodes failed:%d", ret);
		ret = 0;
	}

	return ret;
}

static int isp4_add_subdevs(struct amd_cam *ctx)
{
	int ret;
	int i;
	struct v4l2_subdev *sdev;
	struct device *dev = &ctx->pdev->dev;

	/* Initialize the v4l2_subdev struct */
	sdev = &ctx->sdev;
	v4l2_subdev_init(sdev, &isp4_subdev_ops);
	sdev->flags = V4L2_SUBDEV_FL_HAS_DEVNODE;
	sdev->owner = THIS_MODULE;
	snprintf(sdev->name, sizeof(sdev->name), "%s", "AMD-ISP4");

	sdev->entity.name = "AMD-ISP4";
	sdev->entity.function = MEDIA_ENT_F_PROC_VIDEO_ISP;

	for (i = 0; i < ISP4_VDEV_NUM; i++)
		ctx->sdev_pad[i].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&sdev->entity, ISP4_VDEV_NUM,
				     ctx->sdev_pad);
	if (ret) {
		dev_err(dev, "media_entity_pads_init failed:%d", ret);
		goto fail;
	}
	sdev->internal_ops = &isp4_subdev_internal_ops;
	sdev->entity.ops = &isp4_subdev_ent_ops;

	ret = v4l2_device_register_subdev(&ctx->v4l2_dev, sdev);
	if (ret) {
		dev_err(dev, "v4l2_device_register_subdev error:%d", ret);
		goto subdev_fail;
	}

	ret = isp4_register_subdev_and_create_links(ctx, sdev);
	if (ret) {
		dev_err(dev, "isp4_register_subdev_and_create_links failed:%d", ret);
		goto links_fail;
	}

	return ret;
links_fail:
	/* unregister subdev */
subdev_fail:
	/* clean up media pads*/
fail:
	return ret;
}

static int isp4_init_videodev(struct amd_cam *ctx)
{
	int ret;
	int i;
	const char *vdev_name;
	struct vb2_queue *q;
	struct isp4_video_dev *isp_vdev;
	struct video_device *vdev;
	struct device *dev = &ctx->pdev->dev;

	for (i = 0; i < ISP4_VDEV_NUM; i++) {
		isp_vdev = &ctx->isp_vdev[i];
		isp_vdev->cam = ctx;

		vdev_name = isp_video_dev_name[i];

		/* Initialize the vb2_queue struct */
		mutex_init(&isp_vdev->vbq_lock);
		q = &isp_vdev->vbq;
		q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		q->io_modes = VB2_MMAP | VB2_DMABUF;
		q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
		q->buf_struct_size = sizeof(struct isp4_capture_buffer);
		q->min_queued_buffers = 2;
		q->ops = &isp4_qops;
		q->drv_priv = isp_vdev;
		q->mem_ops = &vb2_amdisp_memops;
		q->lock = &isp_vdev->vbq_lock;
		q->dev = ctx->v4l2_dev.dev;
		ret = vb2_queue_init(q);
		if (ret) {
			dev_err(dev, "vb2_queue_init error:%d", ret);
			goto OUT;
		}
		/* Initialize buffer list and its lock */
		INIT_LIST_HEAD(&isp_vdev->buf_list);
		spin_lock_init(&isp_vdev->qlock);

		/* Set default frame format */
		isp_vdev->format = fmt_default;
		isp_vdev->format.bytesperline = isp_vdev->format.width;
		isp_vdev->format.sizeimage = isp_vdev->format.bytesperline *
					     isp_vdev->format.height * 3 / 2;
		isp_vdev->timeperframe = tpf_default;
		v4l2_simplify_fraction(&isp_vdev->timeperframe.numerator,
				       &isp_vdev->timeperframe.denominator, 8, 333);

		/* Initialize the video_device struct */
		isp_vdev->vdev.entity.name = vdev_name;
		isp_vdev->vdev.entity.function = MEDIA_ENT_F_IO_V4L;
		isp_vdev->vdev_pad.flags = MEDIA_PAD_FL_SINK;
		ret = media_entity_pads_init(&isp_vdev->vdev.entity, 1,
					     &isp_vdev->vdev_pad);

		if (ret) {
			dev_err(dev, "media_entity_pads_init error:%d", ret);
			goto OUT;
		}

		vdev = &isp_vdev->vdev;
		vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE |
				    V4L2_CAP_STREAMING | V4L2_CAP_IO_MC;
		vdev->entity.ops = &isp_vdev_ent_ops;
		vdev->release = video_device_release_empty;
		vdev->fops = &isp4_vdev_fops;
		vdev->ioctl_ops = &isp4_vdev_ioctl_ops;
		vdev->lock = NULL;
		vdev->queue = q;
		vdev->v4l2_dev = &ctx->v4l2_dev;
		vdev->vfl_dir = VFL_DIR_RX;
		strscpy(vdev->name, vdev_name, sizeof(vdev->name));
		video_set_drvdata(vdev, isp_vdev);

		ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
		if (ret) {
			dev_err(dev, "video_register_device error:%d", ret);
			goto OUT;
		}
	}

OUT:
	return ret;
}

static int isp_camera_sensor_bound(struct v4l2_async_notifier *notifier,
				   struct v4l2_subdev *sdev,
				   struct v4l2_async_connection *asd)
{
	int ret;
	struct v4l2_device *v4l2_dev = notifier->v4l2_dev;
	struct amd_cam *ctx = to_amd_cam(v4l2_dev);
	struct device *dev = &ctx->pdev->dev;

	ctx->sensor = sdev;

	ret = isp4_register_subdev_and_create_links(ctx, sdev);
	if (ret)
		dev_err(dev, "isp4_register_subdev_and_create_links failed:%d", ret);

	dev_info(dev, "camera sensor subdevice is registered with the ISP");
	return ret;
}

static void isp_camera_sensor_unbind(struct v4l2_async_notifier *notifier,
				     struct v4l2_subdev *subdev,
				     struct v4l2_async_connection *asd)
{
	struct v4l2_device *v4l2_dev = notifier->v4l2_dev;
	struct amd_cam *ctx = to_amd_cam(v4l2_dev);

	ctx->sensor = NULL;
}

static const struct v4l2_async_notifier_operations isp_camera_sensor_ops = {
	.bound = isp_camera_sensor_bound,
	.unbind = isp_camera_sensor_unbind,
};

static int isp4_add_sensor_subdevs(struct amd_cam *ctx)
{
	int ret;
	int adapter_id = 99;
	unsigned short sensor_address = 0x0010;
	struct v4l2_async_connection *asd;
	struct device *dev = &ctx->pdev->dev;

	ctx->notifier.ops = &isp_camera_sensor_ops;

	asd = v4l2_async_nf_add_i2c(&ctx->notifier, adapter_id, sensor_address,
				    struct v4l2_async_connection);
	if (IS_ERR(asd)) {
		dev_err(dev, "v4l2_async_nf_add_i2c failed:%ld", PTR_ERR(asd));
		return PTR_ERR(asd);
	}

	ret = v4l2_async_nf_register(&ctx->notifier);
	if (ret)
		dev_err(dev, "v4l2_async_nf_register failed:%d", ret);

	return ret;
}

static void resp_interrupt_notify(struct isp_context *isp, u32 intr_status)
{
	u32 intr_ack = 0;

	/* global response */
	if (intr_status & ISP_SYS_INT0_STATUS__SYS_INT_RINGBUFFER_WPT12_INT_MASK) {
		wake_up_resp_thread(isp, 0);
		intr_ack |= ISP_SYS_INT0_ACK__SYS_INT_RINGBUFFER_WPT12_ACK_MASK;
	}

	/* stream 1 response */
	if (intr_status & ISP_SYS_INT0_STATUS__SYS_INT_RINGBUFFER_WPT9_INT_MASK) {
		wake_up_resp_thread(isp, 1);
		intr_ack |= ISP_SYS_INT0_ACK__SYS_INT_RINGBUFFER_WPT9_ACK_MASK;
	}

	/* stream 2 response */
	if (intr_status & ISP_SYS_INT0_STATUS__SYS_INT_RINGBUFFER_WPT10_INT_MASK) {
		wake_up_resp_thread(isp, 2);
		intr_ack |= ISP_SYS_INT0_ACK__SYS_INT_RINGBUFFER_WPT10_ACK_MASK;
	}

	/* stream 3 response */
	if (intr_status & ISP_SYS_INT0_STATUS__SYS_INT_RINGBUFFER_WPT11_INT_MASK) {
		wake_up_resp_thread(isp, 3);
		intr_ack |= ISP_SYS_INT0_ACK__SYS_INT_RINGBUFFER_WPT11_ACK_MASK;
	}

	/* clear ISP_SYS interrupts */
	isp_hwa_wreg(isp, ISP_SYS_INT0_ACK, intr_ack);
}

static irqreturn_t isp_irq_handler(int irq, void *arg)
{
	struct device *dev = arg;
	struct amd_cam *cam = dev_get_drvdata(dev);
	struct isp_context *isp_ctx = &cam->ispctx;

	u32 isp_sys_irq_status = 0x0;
	u32 r1;

	/* check ISP_SYS interrupts status */
	r1 = isp_hwa_rreg(isp_ctx, ISP_SYS_INT0_STATUS);

	isp_sys_irq_status = r1 & FW_RESP_RB_IRQ_STATUS_MASK;

	resp_interrupt_notify(isp_ctx, isp_sys_irq_status);

	return IRQ_HANDLED;
}

/*
 * amd capture module
 */
static int amd_capture_probe(struct platform_device *pdev)
{
	int i, irq, ret;
	struct amd_cam *cam;
	struct device *dev = &pdev->dev;

	cam = devm_kzalloc(&pdev->dev, sizeof(*cam), GFP_KERNEL);
	if (!cam)
		return -ENOMEM;

	cam->pdev = pdev;

	cam->isp_mmio = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(cam->isp_mmio)) {
		dev_err(dev, "isp ioremap failed!!!");
		return PTR_ERR(cam->isp_mmio);
	}

	cam->isp_phy_mmio = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(cam->isp_phy_mmio)) {
		pr_err("isp phy mmio ioremap failed!!!");
		return PTR_ERR(cam->isp_phy_mmio);
	}

	for (i = 0; i < ARRAY_SIZE(ringbuf_interrupt_num); i++) {
		irq = platform_get_irq(pdev, ringbuf_interrupt_num[i]);
		if (irq < 0) {
			dev_err(dev, "failed to get irq, num:%d!!", ringbuf_interrupt_num[i]);
			return -ENODEV;
		}
		ret = devm_request_irq(&pdev->dev, irq, isp_irq_handler, 0,
				       "ISP_IRQ", &pdev->dev);
		if (ret) {
			dev_err(dev, "isp irq %d request failed", irq);
			return ret;
		}
	}

	cam->pltf_data = (struct amdisp_platform_data *)pdev->dev.platform_data;

	dev_dbg(dev, "isp irq registration successful");

	ret = isp_intf_init(&cam->ispctx, cam);
	if (ret) {
		dev_err(dev, "%s failed %d by isp_intf_init", __func__, ret);
		return ret;
	}

	ret = isp_hwa_init(&cam->ispctx);
	if (ret) {
		dev_err(dev, "%s failed %d by isp_hwa_init", __func__, ret);
		return ret;
	}

	/* register v4l2 device */
	snprintf(cam->v4l2_dev.name, sizeof(cam->v4l2_dev.name),
		 "AMD-V4L2-ROOT");
	if (v4l2_device_register(&pdev->dev, &cam->v4l2_dev)) {
		dev_err(dev, "failed to register v4l2 device");
		goto free_dev;
	}

	dev_info(dev, "AMD ISP v4l2 device registered");
	dev_dbg(dev, "DRI:%s | FW:%s", DRI_VERSION_STRING, FW_VERSION_STRING);

	/* Link the media device within the v4l2_device */
	cam->v4l2_dev.mdev = &cam->mdev;

	/* Initialize media device */
	strscpy(cam->mdev.model, "amd_isp41_mdev", sizeof(cam->mdev.model));
	snprintf(cam->mdev.bus_info, sizeof(cam->mdev.bus_info),
		 "platform:%s", ISP_DRV_NAME);
	cam->mdev.dev = &pdev->dev;
	media_device_init(&cam->mdev);

	ret = isp4_init_videodev(cam);
	if (ret != 0)
		goto free_dev;

	if (use_embedded_sensor) {
		ret = isp4_add_subdevs(cam);
		if (ret != 0)
			goto free_dev;
	} else {
		v4l2_async_nf_init(&cam->notifier, &cam->v4l2_dev);
		ret = isp4_add_sensor_subdevs(cam);
		if (ret != 0)
			goto free_dev;
	}

	ret = media_device_register(&cam->mdev);
	if (ret != 0) {
		dev_err(dev, "failed to register media device:%d", ret);
		goto free_dev;
	}

	platform_set_drvdata(pdev, cam);

	isp_debugfs_create(cam);

	return 0;

free_dev:
	for (i = 0; i < ISP4_VDEV_NUM; i++)
		vb2_video_unregister_device(&cam->isp_vdev[i].vdev);

	media_device_unregister(&cam->mdev);
	v4l2_device_unregister(&cam->v4l2_dev);

	isp_intf_fini(&cam->ispctx);

	return ret;
}

static void amd_capture_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct amd_cam *cam = platform_get_drvdata(pdev);
	int i = 0;

	isp_debugfs_remove(cam);

	for (i = 0; i < ISP4_VDEV_NUM; i++)
		vb2_video_unregister_device(&cam->isp_vdev[i].vdev);

	media_device_unregister(&cam->mdev);
	v4l2_device_unregister(&cam->v4l2_dev);
	dev_info(dev, "AMD ISP v4l2 device unregistered");

	isp_intf_fini(&cam->ispctx);
}

#ifdef REGISTER_ISP_DEV
static void amd_pdev_release(struct device __maybe_unused *dev)
{
}

static struct platform_device amd_capture_dev = {
	.name = ISP_DRV_NAME,
	.dev.release = amd_pdev_release,
};
#endif

static struct platform_driver amd_capture_drv = {
	.probe = amd_capture_probe,
	.remove = amd_capture_remove,
	.driver = {
		.name = ISP_DRV_NAME,
		.owner = THIS_MODULE,
	}
};

static int __init amd_capture_init(void)
{
	int ret;

	/* isp dev will be registered in amdgpu isp */
	ret = platform_driver_register(&amd_capture_drv);
	if (ret)
		pr_err("register platform driver fail!");

	return ret;
}

static void __exit amd_capture_exit(void)
{
	platform_driver_unregister(&amd_capture_drv);

	/* follow up: should not call directly, remove it later */
	amd_capture_remove(NULL);
}

module_init(amd_capture_init);
module_exit(amd_capture_exit);

MODULE_ALIAS("platform:" ISP_DRV_NAME);
MODULE_IMPORT_NS("DMA_BUF");

MODULE_AUTHOR("bin du <bin.du@amd.com>");
MODULE_DESCRIPTION("AMD ISP4 Driver");
MODULE_LICENSE("GPL v2");
