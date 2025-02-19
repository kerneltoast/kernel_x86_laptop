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
#include "isp_core.h"
#include "isp_common.h"
#include "isp_mc_addr_mgr.h"

static void isp_fw_pl_list_destroy(struct isp_fw_cmd_pay_load_buf *head)
{
	if (!head)
		return;

	while (head) {
		struct isp_fw_cmd_pay_load_buf *temp;

		temp = head;
		head = head->next;
		kfree(temp);
	}
}

static void isp_fw_buf_calculate_base(struct isp_context *isp,
				      enum fw_cmd_resp_stream_id id,
				      u32 base_idx,
				      u64 *sys_addr, u64 *mc_addr, u32 *len)
{
	struct device *dev;
	u32 offset;
	u32 idx;
	u64 mc_base;
	u8 *sys_base;
	u32 aligned_rb_chunck_size;

	dev = &isp->amd_cam->pdev->dev;

	mc_base = isp->fw_cmd_resp_buf->gpu_mc_addr;
	sys_base = (u8 *)isp->fw_cmd_resp_buf->sys_addr;

	/* cmd/resp rb base address should be 64Bytes aligned; */
	aligned_rb_chunck_size = RB_PMBMAP_MEM_CHUNK & 0xffffffc0;

	switch (id) {
	case FW_CMD_RESP_STREAM_ID_GLOBAL:
		idx = 3;
		break;
	case FW_CMD_RESP_STREAM_ID_1:
		idx = 0;
		break;
	case FW_CMD_RESP_STREAM_ID_2:
		idx = 1;
		break;
	case FW_CMD_RESP_STREAM_ID_3:
		idx = 2;
		break;
	default:
		dev_err(dev, "fail, bad stream id %d", id);
		if (len)
			*len = 0;
		return;
	}

	offset = aligned_rb_chunck_size * (idx + base_idx);

	if (len)
		*len = ISP_FW_CMD_BUF_SIZE;
	if (sys_addr)
		*sys_addr = (u64)(sys_base + offset);
	if (mc_addr)
		*mc_addr = mc_base + offset;
};

void isp_fw_indirect_cmd_pl_buf_init(struct isp_context *isp,
				     struct isp_fw_work_buf_mgr *mgr,
				     u64 sys_addr,
				     u64 mc_addr,
				     u32 len)
{
	struct device *dev = &isp->amd_cam->pdev->dev;
	u32 pkg_size;
	u64 base_sys;
	u64 base_mc;
	u64 next_sys;
	u64 next_mc;
	u32 payload_cnt;
	struct isp_fw_cmd_pay_load_buf *new_buffer;
	struct isp_fw_cmd_pay_load_buf *tail_buffer;

	if (!mgr || !sys_addr || !mc_addr || !len) {
		dev_err(dev, "%s fail param %llx %llx %u",
			__func__, sys_addr, mc_addr, len);
	}

	memset(mgr, 0, sizeof(*mgr));
	mgr->sys_base = sys_addr;
	mgr->mc_base = mc_addr;
	mgr->pay_load_pkg_size = isp_get_cmd_pl_size();

	mutex_init(&mgr->mutex);
	dev_dbg(dev, "%s, sys 0x%llx,mc 0x%llx,len %u", __func__,
		sys_addr, mc_addr, len);
	pkg_size = mgr->pay_load_pkg_size;

	base_sys = sys_addr;
	base_mc = mc_addr;
	next_mc = mc_addr;

	payload_cnt = 0;
	while (true) {
		new_buffer = kmalloc(sizeof(*new_buffer), GFP_KERNEL);
		if (IS_ERR_OR_NULL(new_buffer)) {
			dev_err(dev, "%s fail to alloc %uth pl buf", __func__, payload_cnt);
			break;
		};

		next_mc = ISP_ADDR_ALIGN_UP(next_mc, ISP_FW_CMD_PAY_LOAD_BUF_ALIGN);
		if ((next_mc + pkg_size - base_mc) > len) {
			kfree(new_buffer);
			break;
		}

		payload_cnt++;
		next_sys = base_sys + (next_mc - base_mc);
		new_buffer->mc_addr = next_mc;
		new_buffer->sys_addr = next_sys;
		new_buffer->next = NULL;
		if (!mgr->free_cmd_pl_list) {
			mgr->free_cmd_pl_list = new_buffer;
		} else {
			tail_buffer = mgr->free_cmd_pl_list;
			while (tail_buffer->next)
				tail_buffer = tail_buffer->next;
			tail_buffer->next = new_buffer;
		}
		next_mc += pkg_size;
	}
	mgr->pay_load_num = payload_cnt;
};

void isp_fw_indirect_cmd_pl_buf_uninit(struct isp_fw_work_buf_mgr *mgr)
{
	if (!mgr)
		return;

	isp_fw_pl_list_destroy(mgr->free_cmd_pl_list);
	mgr->free_cmd_pl_list = NULL;
	isp_fw_pl_list_destroy(mgr->used_cmd_pl_list);
	mgr->used_cmd_pl_list = NULL;
}

int isp_fw_get_nxt_indirect_cmd_pl(struct isp_context *isp,
				   struct isp_fw_work_buf_mgr *mgr,
				   u64 *sys_addr,
				   u64 *mc_addr, u32 *len)
{
	struct device *dev = &isp->amd_cam->pdev->dev;
	struct isp_fw_cmd_pay_load_buf *temp;
	struct isp_fw_cmd_pay_load_buf *tail;
	int ret;

	if (!mgr) {
		dev_err(dev, "%s null mgr", __func__);
		return -EINVAL;
	}

	mutex_lock(&mgr->mutex);

	if (!mgr->free_cmd_pl_list) {
		dev_err(dev, "%s null free cmd list", __func__);
		ret = -EINVAL;
	} else {
		temp = mgr->free_cmd_pl_list;
		mgr->free_cmd_pl_list = temp->next;
		temp->next = NULL;
		if (sys_addr)
			*sys_addr = temp->sys_addr;
		if (mc_addr)
			*mc_addr = temp->mc_addr;
		if (len)
			*len = mgr->pay_load_pkg_size;
		if (!mgr->used_cmd_pl_list) {
			mgr->used_cmd_pl_list = temp;
		} else {
			tail = mgr->used_cmd_pl_list;
			while (tail->next)
				tail = tail->next;
			tail->next = temp;
		};
		ret = 0;
		dev_dbg(dev, "%s, sys:0x%llx(%u), mc:0x%llx", __func__,
			temp->sys_addr,
			mgr->pay_load_pkg_size, temp->mc_addr);
	}

	mutex_unlock(&mgr->mutex);
	return ret;
}

int isp_fw_ret_indirect_cmd_pl(struct isp_context *isp,
			       struct isp_fw_work_buf_mgr *mgr,
			       u64 mc_addr)
{
	struct device *dev = &isp->amd_cam->pdev->dev;
	struct isp_fw_cmd_pay_load_buf *temp = NULL;
	struct isp_fw_cmd_pay_load_buf *tail;
	int ret;

	if (!mgr) {
		dev_err(dev, "%s null mgr", __func__);
		return -EINVAL;
	}
	if (!mgr->used_cmd_pl_list) {
		dev_err(dev, "%s null used cmd list", __func__);
		return -EINVAL;
	}

	mutex_lock(&mgr->mutex);
	if (mgr->used_cmd_pl_list->mc_addr == mc_addr) {
		temp = mgr->used_cmd_pl_list;
		mgr->used_cmd_pl_list = temp->next;
	} else {
		tail = mgr->used_cmd_pl_list;
		while (tail->next) {
			if (tail->next->mc_addr != mc_addr) {
				tail = tail->next;
			} else {
				temp = tail->next;
				tail->next = temp->next;
				break;
			};
		};
		if (!temp) {
			dev_err(dev, "%s no matching mcaddr", __func__);
			ret = -EINVAL;
			goto quit;
		}
	};
	temp->next = mgr->free_cmd_pl_list;
	mgr->free_cmd_pl_list = temp;
	ret = 0;
quit:
	mutex_unlock(&mgr->mutex);
	return ret;
}

void isp_fw_buf_get_cmd_base(struct isp_context *isp,
			     enum fw_cmd_resp_stream_id id,
			     u64 *sys_addr, u64 *mc_addr, u32 *len)
{
	struct device *dev;

	if (!isp) {
		pr_err("%s null isp context", __func__);
		return;
	}

	dev = &isp->amd_cam->pdev->dev;

	if (!isp->fw_cmd_resp_buf) {
		dev_err(dev, "%s null fw_cmd_resp_buf", __func__);
		return;
	}

	/* calculate cmd base, base index is 0 */
	isp_fw_buf_calculate_base(isp, id, 0, sys_addr, mc_addr, len);
};

void isp_fw_buf_get_resp_base(struct isp_context *isp,
			      enum fw_cmd_resp_stream_id id,
			      u64 *sys_addr, u64 *mc_addr, u32 *len)
{
	struct device *dev;

	if (!isp) {
		pr_err("%s null isp context", __func__);
		return;
	}

	dev = &isp->amd_cam->pdev->dev;

	if (!isp->fw_cmd_resp_buf) {
		dev_err(dev, "%s null fw_cmd_resp_buf", __func__);
		return;
	}

	/* calculate resp base, base index is RESP_CHAN_TO_RB_OFFSET - 1 */
	isp_fw_buf_calculate_base(isp, id, RESP_CHAN_TO_RB_OFFSET - 1, sys_addr, mc_addr, len);
};
