set -e

TARGET_SOC="rk356x"
GCC_COMPILER=aarch64-rockchip1031-linux-gnu

export LD_LIBRARY_PATH=${TOOL_CHAIN}/lib64:$LD_LIBRARY_PATH
export CC=${GCC_COMPILER}-gcc
export CXX=${GCC_COMPILER}-g++

ROOT_PWD=$( cd "$( dirname $0 )" && cd -P "$( dirname "$SOURCE" )" && pwd )

# SDKÂ·¾¶
SDK_RKNPU2_PATH=/home/alientek/software/rknpu2-master

# build
BUILD_DIR=${ROOT_PWD}/build/build_linux_aarch64

if [[ ! -d "${BUILD_DIR}" ]]; then
  mkdir -p ${BUILD_DIR}
fi

cd ${BUILD_DIR}
cmake ../.. \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DTARGET_SOC=${TARGET_SOC} \
    -DSDK_RKNPU2_PATH=${SDK_RKNPU2_PATH}
make -j4
make install
cd -
