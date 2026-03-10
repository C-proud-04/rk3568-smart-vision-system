# rk3568-smart-vision-system-
A smart vision edge computing system based on Rockchip RK3568
#### 获取摄像头设备号
```bash
ls /dev/vi*
```
![alt text](image.png)
根据拔插摄像头，获得设备号，当前摄像头设备号为：/dev/video9

#### v4l2设置摄像头的曝光
##### 手动设置
```bash
v4l2-ctl -d /dev/video9 -c exposure_auto=3          # exposure_auto设置为3为自动模式
v4l2-ctl -d /dev/video9 -c exposure_auto=1          # exposure_auto设置为1为手动模式
v4l2-ctl -d /dev/video9 -c exposure_absolute=2600   # 设置为手动模式后调整曝光度
```

#### 交叉编译其他库（SDL库为例）
- github下载SDL库源文件
- 交叉编译流程：
```bash
cd SDL-main
# 找交叉编译工具链的根目录
aarch64-rockchip1031-linux-gnu-gcc -print-sysroot
# 创建build文件夹
mkdir build && cd build
# 执行cmake
cmake .. \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
    -DCMAKE_C_COMPILER=aarch64-rockchip1031-linux-gnu-gcc \
    -DCMAKE_CXX_COMPILER=aarch64-rockchip1031-linux-gnu-g++ \
    -DCMAKE_INSTALL_PREFIX=/home/alientek/ATOMPI-CA1_SDK_v1.2/prebuilts/gcc/linux-x86/aarch64/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/../aarch64-none-linux-gnu/libc \
    -DCMAKE_FIND_ROOT_PATH=/home/alientek/ATOMPI-CA1_SDK_v1.2/prebuilts/gcc/linux-x86/aarch64/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/../aarch64-none-linux-gnu/libc \
    -DSDL_UNIX_CONSOLE_BUILD=ON # 仅是为了跳过X11/Wayland
make -j8
sudo make install
```