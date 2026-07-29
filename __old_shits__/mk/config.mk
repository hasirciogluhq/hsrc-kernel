# Paths and build layout

BUILD   := build
SRC     := src
INC     := include
TARGET  := $(BUILD)/kernel.bin
DRIVERS := $(BUILD)/drivers
USEROUT := $(BUILD)/user
INITRD  := $(DRIVERS)/initrd.img
PACKER  := $(BUILD)/pack_initrd
PACK_MKE := $(BUILD)/pack_mke
MKFATIMG := $(BUILD)/mkfatimg
DISKIMG := disk.img
DISK_MB := 64

# Parallel compile: use all cores by default.
# Override:  make JOBS=1   |   make -j4   |   make -j1
NPROC := $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
JOBS  ?= $(NPROC)
ifeq ($(filter -j% -j --jobserver-auth=% --jobserver-fds=%,$(MAKEFLAGS)),)
MAKEFLAGS += -j$(JOBS)
endif
