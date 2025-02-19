# SPDX-License-Identifier: GPL-2.0
#
#Copyright (C) 2024-2025 Advanced Micro Devices, Inc. All rights reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
# OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
# ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
# OTHER DEALINGS IN THE SOFTWARE.

obj-$(CONFIG_AMD_ISP4) += amd_capture.o
amd_capture-objs := 	isp_core.o \
			isp_hwa.o \
			isp_debug.o \
			isp_phy.o \
			isp_fw_ctrl.o \
			isp_events.o \
			isp_module_intf.o \
			isp_mc_addr_mgr.o \
			isp_queue.o \
			isp_utils.o \
			isp_pwr.o \

ccflags-y += -I$(srctree)/drivers/media/platform/amd/isp4
ccflags-y += -I$(srctree)/drivers/gpu/drm/amd/include
ccflags-y += -I$(srctree)/drivers/gpu/drm/amd/amdgpu
ccflags-y += -I$(srctree)/drivers/gpu/drm/amd/pm/inc
ccflags-y += -I$(srctree)/include/drm
ccflags-y += -I$(srctree)/include
ccflags-y += -I$(srctree)/include/uapi/drm
ccflags-y += -I$(srctree)/drivers/gpu/drm/amd/scheduler
ccflags-y += -I$(srctree)/drivers/gpu/drm/amd/powerplay/inc
ccflags-y += -I$(srctree)/drivers/gpu/drm/amd/acp/include
ccflags-y += -I$(srctree)/drivers/gpu/drm/amd/display
ccflags-y += -I$(srctree)/drivers/gpu/drm/amd/display/include
ccflags-y += -I$(srctree)/drivers/gpu/drm/amd/display/modules/inc
ccflags-y += -I$(srctree)/drivers/gpu/drm/amd/display/dc
ccflags-y += -I$(srctree)/drivers/gpu/drm/amd/display/amdgpu_dm
ccflags-y += -I$(srctree)/../external/libdrm/amdgpu

ifneq ($(call cc-option, -mpreferred-stack-boundary=4),)
	cc_stack_align := -mpreferred-stack-boundary=4
endif

ifneq ($(CONFIG_AMD_PRINT_FLOAT_INFO),)
CFLAGS_isp_core.o += -mhard-float -msse -DPRINT_FLOAT_INFO
endif

ccflags-y += $(cc_stack_align)
ccflags-y += -DCONFIG_COMPAT
