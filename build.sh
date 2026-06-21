#!/bin/bash
#
# build.sh - 一键编译 V4L2 + SDL2 摄像头实时预览 Demo
#
# 用法:
#   ./build.sh              # RK3568 交叉编译
#   ./build.sh local        # 本地编译 (x86_64 测试)
#   ./build.sh clean        # 清理
#

set -e

# RK3568 交叉编译工具链
GCC_COMPILER_PREFIX=aarch64-rockchip1031-linux-gnu

# Buildroot sysroot 路径 (根据实际 SDK 路径修改)
# 如果环境变量 RK_SYSROOT 已设置则使用，否则使用默认路径
if [ -z "$RK_SYSROOT" ]; then
    # 尝试查找 sysroot
    if [ -d "/home/alientek/software/rknpu2-master/../rk3568_sdk/buildroot/output/rockchip_rk3568/host/aarch64-buildroot-linux-gnu/sysroot" ]; then
        RK_SYSROOT="/home/alientek/software/rknpu2-master/../rk3568_sdk/buildroot/output/rockchip_rk3568/host/aarch64-buildroot-linux-gnu/sysroot"
    fi
fi

ROOT_PWD=$(cd "$(dirname "$0")" && pwd)
BUILD_DIR="$ROOT_PWD/build_v4l2_sdl"

MODE="${1:-cross}"

case "$MODE" in
    local)
        echo "============================================"
        echo "  本地编译 (x86_64)"
        echo "============================================"
        make -f "$ROOT_PWD/Makefile" CROSS_COMPILE= SYSROOT= clean all
        ;;

    clean)
        echo "清理编译产物..."
        make -f "$ROOT_PWD/Makefile" clean
        rm -rf "$BUILD_DIR"
        echo "清理完成"
        ;;

    cross|*)
        echo "============================================"
        echo "  RK3568 交叉编译"
        echo "  编译器: ${GCC_COMPILER_PREFIX}-gcc"
        echo "  Sysroot: ${RK_SYSROOT:-未设置}"
        echo "============================================"

        # 检查交叉编译器是否存在
        if ! command -v ${GCC_COMPILER_PREFIX}-gcc &>/dev/null; then
            echo ""
            echo "============================================"
            echo "  错误: 找不到交叉编译器 ${GCC_COMPILER_PREFIX}-gcc"
            echo "  请确保交叉工具链已安装并在 PATH 中"
            echo "============================================"
            exit 1
        fi

        make -f "$ROOT_PWD/Makefile" \
            CROSS_COMPILE="${GCC_COMPILER_PREFIX}-" \
            SYSROOT="${RK_SYSROOT}" \
            clean all

        echo ""
        echo "============================================"
        echo "  交叉编译完成!"
        echo "  可执行文件: $ROOT_PWD/v4l2_sdl_demo"
        echo ""
        echo "  部署到 RK3568:"
        echo "    scp $ROOT_PWD/v4l2_sdl_demo root@<rk3568_ip>:/usr/local/bin/"
        echo ""
        echo "  在 RK3568 上运行:"
        echo "    export DISPLAY=:0"
        echo "    ./v4l2_sdl_demo /dev/video9"
        echo "============================================"
        ;;
esac
