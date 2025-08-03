# SPDX-License-Identifier: GPL-2.0+
#
# Copyright (C) 2025 Advanced Micro Devices, Inc.

obj-$(CONFIG_AMD_ISP4) += amd_capture.o
amd_capture-objs := isp4_subdev.o \
			isp4_debug.o \
			isp4_phy.o \
			isp4_interface.o \
			isp4.o	\
			isp4_video.o \
			isp4_hw.o	\

ccflags-y += -Wunused-but-set-variable
ccflags-y += -Wmissing-include-dirs
ccflags-y += -Wunused-const-variable
ccflags-y += -Wmaybe-uninitialized
ccflags-y += -Wunused-value
