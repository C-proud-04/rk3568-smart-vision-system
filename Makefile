# Makefile - V4L2 + SDL2 摄像头实时预览 Demo
# 支持本地编译和 RK3568 交叉编译

# ==================== 交叉编译配置 ====================
# RK3568 交叉编译器前缀
CROSS_COMPILE ?= aarch64-rockchip1031-linux-gnu-
# RK3568 目标系统 sysroot (根据实际路径修改)
SYSROOT ?= /home/alientek/software/rknpu2-master/../rk3568_sdk/buildroot/output/rockchip_rk3568/host/aarch64-buildroot-linux-gnu/sysroot

# ==================== 本地编译配置 ====================
# 如果 CROSS_COMPILE 为空，表示本地编译
ifeq ($(CROSS_COMPILE),)
    CC  = gcc
    CXX = g++
else
    CC  = $(CROSS_COMPILE)gcc
    CXX = $(CROSS_COMPILE)g++
endif

# ==================== 编译选项 ====================
TARGET  = v4l2_sdl_demo
SRCS    = v4l2_sdl_demo.c
OBJS    = $(SRCS:.c=.o)

# 如果指定了 SYSROOT，添加 --sysroot
ifneq ($(SYSROOT),)
    SYSROOT_FLAGS = --sysroot=$(SYSROOT)
else
    SYSROOT_FLAGS =
endif

CFLAGS  = -Wall -O2 $(SYSROOT_FLAGS)
CFLAGS  += -I$(SYSROOT)/usr/include/SDL2 -I$(SYSROOT)/usr/include
LDFLAGS = -lSDL2 -lpthread -lm $(SYSROOT_FLAGS)
LDFLAGS += -L$(SYSROOT)/usr/lib -L$(SYSROOT)/lib

# 如果需要 MJPEG 支持，取消下面一行的注释
# CFLAGS += -DENABLE_MJPEG
# LDFLAGS += -ljpeg

# ==================== 构建规则 ====================
.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)
	@echo "============================================"
	@echo "  编译完成: $(TARGET)"
	@echo "  编译器:   $(CC)"
	@echo "============================================"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)

info:
	@echo "CC:       $(CC)"
	@echo "SYSROOT:  $(SYSROOT)"
	@echo "CFLAGS:   $(CFLAGS)"
	@echo "LDFLAGS:  $(LDFLAGS)"
