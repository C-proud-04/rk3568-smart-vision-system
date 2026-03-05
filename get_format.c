// 枚举摄像头支持的视频格式示例程序
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <linux/videodev2.h>

int main(void) 
{
    // 打开摄像头设备（需根据实际设备号修改）
    int fd = open("/dev/video9", O_RDWR);
    if (fd < 0)
    {
        perror("打开设备失败");
        return -1;
    }

    // 定义格式描述结构体，用于存放枚举结果
    struct v4l2_fmtdesc vfmt;
    vfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; // 指定为视频采集类型

    int i = 0;
    while (1) // 使用while循环从0开始测试index索引值
    {
        vfmt.index = i; // 设置当前枚举的格式索引
        i++;
        // 枚举摄像头支持的像素格式
        // ioctl参数说明：
        //   fd：设备文件描述符
        //   VIDIOC_ENUM_FMT：枚举格式命令
        //   &vfmt：结果存放到vfmt结构体
        int ret = ioctl(fd, VIDIOC_ENUM_FMT, &vfmt);
        if (ret < 0)
        {
            // 当返回值小于0时，说明枚举结束或出错
            perror("获取失败");
            break;
        }
        // 输出当前格式的详细信息
        printf("index=%d\n", vfmt.index); // 格式索引
        printf("flags=%d\n", vfmt.flags); // 标志位
        printf("discription=%s\n", vfmt.description); // 格式描述
        // pixelformat是四字符编码（fourcc），如YUYV、MJPG等
        unsigned char *p = (unsigned char *)&vfmt.pixelformat;
        printf("pixelformat=%c%c%c%c\n", p[0], p[1], p[2], p[3]);
        printf("reserved=%d\n", vfmt.reserved[0]); // 保留字段
    }
    close(fd); // 关闭设备
    return 0;
}
