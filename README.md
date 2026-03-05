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