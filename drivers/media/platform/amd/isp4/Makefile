# SPDX-License-Identifier: GPL-2.0+
#
# Copyright (C) 2025 Advanced Micro Devices, Inc.

obj-$(CONFIG_AMD_ISP4) += amd_capture.o
amd_capture-objs := isp4.o	\
			isp4_hw.o	\

ccflags-y += -I$(srctree)/drivers/media/platform/amd/isp4
ccflags-y += -I$(srctree)/include

ifneq ($(call cc-option, -mpreferred-stack-boundary=4),)
	cc_stack_align := -mpreferred-stack-boundary=4
endif

ccflags-y += $(cc_stack_align)
ccflags-y += -DCONFIG_COMPAT
ccflags-y += -Wunused-but-set-variable
ccflags-y += -Wmissing-include-dirs
ccflags-y += -Wunused-const-variable
ccflags-y += -Wmaybe-uninitialized
ccflags-y += -Wunused-value
