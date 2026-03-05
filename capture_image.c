/*
 * capture_image.c
 *
 * 这个程序使用V4L2接口从摄像头设备捕获一帧图像，并将其保存为JPEG文件。
 * 它演示了基本的摄像头操作，包括设备打开、格式设置、缓冲区管理、内存映射和数据采集。
 *
 * 使用方法: ./capture_image [设备名]
 * 默认设备: /dev/video9
 * 输出文件: frame.jpg
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

// 可根据实际情况修改
#define DEVICE "/dev/video9"
#define WIDTH 640
#define HEIGHT 480
#define IMAGE_FILE "frame.jpg"  // 保存为jpg

struct buffer {
    void   *start;
    size_t length;
};

int main(int argc, char *argv[])
{
    // 获取设备名，如果提供了命令行参数则使用，否则使用默认设备
    const char *dev_name = DEVICE;
    if (argc > 1) dev_name = argv[1];

    // 打开摄像头设备
    int fd = open(dev_name, O_RDWR);
    if (fd < 0) {
        perror("打开摄像头失败");
        return 1;
    }

    // 设置自动曝光（注释掉的部分）
    // struct v4l2_control ctrl;
    // memset(&ctrl, 0, sizeof(ctrl));
    // ctrl.id = V4L2_CID_EXPOSURE_AUTO;
    // ctrl.value = V4L2_EXPOSURE_AUTO;
    // if (ioctl(fd, VIDIOC_S_CTRL, &ctrl) < 0) {
    //     perror("设置自动曝光失败");
    // }

    

    // 设置视频捕获格式
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = WIDTH;
    fmt.fmt.pix.height = HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG; // 采集MJPEG格式
    fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("设置MJPEG格式失败，摄像头可能不支持MJPEG");
        close(fd);
        return 1;
    }
    if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG) {
        fprintf(stderr, "摄像头实际像素格式不是MJPEG，无法直接保存为jpg！\n");
        close(fd);
        return 1;
    }

    // 请求视频缓冲区
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 1;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP; // 内存映射
    // 实际请求缓冲区
    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("请求缓冲区失败");
        close(fd);
        return 1;
    }

    // 查询并映射缓冲区到用户空间
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = 0;
    // 向内核查询指定索引的缓冲区信息，成功后，buf结构体会被填充相关信息，如缓冲区长度和偏移地址等
    if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
        perror("查询缓冲区失败");
        close(fd);
        return 1;
    }
    struct buffer buffer;
    buffer.length = buf.length;
    buffer.start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
    if (buffer.start == MAP_FAILED) {
        perror("mmap失败");
        close(fd);
        return 1;
    }

    // 将缓冲区入队，准备捕获数据
    if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
        perror("入队失败");
        munmap(buffer.start, buffer.length);
        close(fd);
        return 1;
    }

    // 启动视频流
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        perror("启动采集失败");
        munmap(buffer.start, buffer.length);
        close(fd);
        return 1;
    }

    // 从队列中取出缓冲区，获取捕获的数据
    if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
        perror("出队失败");
        ioctl(fd, VIDIOC_STREAMOFF, &type);
        munmap(buffer.start, buffer.length);
        close(fd);
        return 1;
    }

    // 将捕获的数据保存为JPEG文件
    FILE *fp = fopen(IMAGE_FILE, "wb");
    if (!fp) {
        perror("打开文件失败");
    } else {
        fwrite(buffer.start, buf.bytesused, 1, fp);
        fclose(fp);
        printf("已保存一帧到 %s (MJPEG, 可直接用图片查看器打开)\n", IMAGE_FILE);
    }

    // 停止视频流并清理资源
    ioctl(fd, VIDIOC_STREAMOFF, &type);
    munmap(buffer.start, buffer.length);
    close(fd);
    return 0;
}
