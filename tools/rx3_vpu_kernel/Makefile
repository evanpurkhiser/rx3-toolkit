# Build the RX3 1.19 vendor VPU driver as an external module.

obj-m += mxc_vpu.o
ccflags-y += -fno-pic -fno-pie

ifeq ($(CONFIG_MXC_VPU_DEBUG),y)
EXTRA_CFLAGS += -DDEBUG
endif
