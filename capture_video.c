#include <asm/types.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <linux/fb.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define CLEAN(x) (memset(&(x), 0, sizeof(x)))
#define WIDTH 640
#define HEIGHT 480

typedef struct BufferSt {
    void *start;
    unsigned int length;
} BufferSt;

int fd;
int out_fd;
static BufferSt *buffer = NULL;

static int query_set_format()
{
    // 查询设备信息
    struct v4l2_capability cap;

    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == -1) {
        perror("VIDIOC_QUERYCAP");
        return -1;
    }
    printf("DriverName:%s\nCard Name:%s\nBus info:%s\nDriverVersion:%u.%u.%u\n",
           cap.driver, cap.card, cap.bus_info, (cap.version >> 16) & 0xFF, (cap.version >> 8) & 0xFF, (cap.version) & 0xFF);
    // 查询帧格式
    struct v4l2_fmtdesc fmtdesc;
    fmtdesc.index = 0;
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    while (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) != -1) {
        printf("\t%d.%s\n", fmtdesc.index + 1, fmtdesc.description);
        fmtdesc.index++;
    }
    // 设置帧格式
    struct v4l2_format fmt;
    CLEAN(fmt);
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = WIDTH;
    fmt.fmt.pix.height = HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) == -1) {
        printf("VIDIOC_S_FMT IS ERROR! LINE:%d\n", __LINE__);
        //return -1;
    }
    // 上面设置帧格式可能失败，这里需要查看一下实际帧格式
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_G_FMT, &fmt) == -1) {
        printf("VIDIOC_G_FMT IS ERROR! LINE:%d\n", __LINE__);
        return -1;
    }
    printf("width:%d\nheight:%d\npixelformat:%c%c%c%c\n",
           fmt.fmt.pix.width, fmt.fmt.pix.height,
           fmt.fmt.pix.pixelformat & 0xFF,
           (fmt.fmt.pix.pixelformat >> 8) & 0xFF,
           (fmt.fmt.pix.pixelformat >> 16) & 0xFF,
           (fmt.fmt.pix.pixelformat >> 24) & 0xFF);
    return 0;
}

static int request_allocate_buffers()
{
    // 申请帧缓冲区
    struct v4l2_requestbuffers req;
    CLEAN(req);
    req.count = 4;
    req.memory = V4L2_MEMORY_MMAP; // 使用内存映射缓冲区
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    // 申请4个帧缓冲区，在内核空间中
    if (ioctl(fd, VIDIOC_REQBUFS, &req) == -1) {
        printf("VIDIOC_REQBUFS IS ERROR! LINE:%d\n", __LINE__);
        return -1;
    }
    // 获取每个帧信息，并映射到用户空间
    buffer = (BufferSt *)calloc(req.count, sizeof(BufferSt));
    if (buffer == NULL) {
        printf("calloc is error! LINE:%d\n", __LINE__);
        return -1;
    }

    struct v4l2_buffer buf;
    int buf_index = 0;
    for (buf_index = 0; buf_index < req.count; buf_index++) {
        CLEAN(buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.index = buf_index;
        buf.memory = V4L2_MEMORY_MMAP;
        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) == -1) // 获取每个帧缓冲区的信息 如length和offset
        {
            printf("VIDIOC_QUERYBUF IS ERROR! LINE:%d\n", __LINE__);
            return -1;
        }
        // 将内核空间中的帧缓冲区映射到用户空间
        buffer[buf_index].length = buf.length;
        buffer[buf_index].start = mmap(NULL,                   // 由内核分配映射的起始地址
                                       buf.length,             // 长度
                                       PROT_READ | PROT_WRITE, // 可读写
                                       MAP_SHARED,             // 可共享
                                       fd,
                                       buf.m.offset);
        if (buffer[buf_index].start == MAP_FAILED) {
            printf("MAP_FAILED LINE:%d\n", __LINE__);
            return -1;
        }
        // 将帧缓冲区放入视频输入队列
        if (ioctl(fd, VIDIOC_QBUF, &buf) == -1) {
            printf("VIDIOC_QBUF IS ERROR! LINE:%d\n", __LINE__);
            return -1;
        }
        printf("Frame buffer :%d   address :0x%x    length:%d\n", buf_index, (__u32)buffer[buf_index].start, buffer[buf_index].length);
    }
}

static void start_capture()
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) == -1) {
        printf("VIDIOC_STREAMON IS ERROR! LINE:%d\n", __LINE__);
        exit(1);
    }
}

static void end_capture()
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMOFF, &type) == -1) {
        printf("VIDIOC_STREAMOFF IS ERROR! LINE:%d\n", __LINE__);
        exit(1);
    }
}

static int read_frame()
{
    struct v4l2_buffer buf;
    int ret = 0;
    CLEAN(buf);
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd, VIDIOC_DQBUF, &buf) == -1) {
        printf("VIDIOC_DQBUF! LINEL:%d\n", __LINE__);
        return -1;
    }
    ret = write(out_fd, buffer[buf.index].start, buf.bytesused);
    if (ret == -1) {
        printf("write is error !\n");
        return -1;
    }
    if (ioctl(fd, VIDIOC_QBUF, &buf) == -1) {
        printf("VIDIOC_QBUF! LINE:%d\n", __LINE__);
        return -1;
    }
    return 0;
}

static void unmap_buffer()
{
    int i = 0;
    for (i = 0; i < 4; i++) {
        munmap(buffer[i].start, buffer[i].length);
    }
    free(buffer);
}
static int capture_frame()
{
    struct timeval tvptr;
    int ret;
    tvptr.tv_usec = 0;
    tvptr.tv_sec = 2;
    fd_set fdread;
    FD_ZERO(&fdread);
    FD_SET(fd, &fdread);
    ret = select(fd + 1, &fdread, NULL, NULL, &tvptr);
    if (ret == -1) {
        perror("select");
        exit(1);
    }
    if (ret == 0) {
        printf("timeout! \n");
        return -1;
    }
    read_frame();
}

int main(int argc, char *argv[])
{
    // 1、打开摄像头
    fd = open("/dev/video9", O_RDWR | O_NONBLOCK, 0);
    if (fd == -1) {
        printf("can not open '%s'\n", "/dev/video0");
        return -1;
    }
    out_fd = open("./out.yuv", O_RDWR | O_CREAT, 0777);
    if (out_fd == -1) {
        printf("open out file is error!\n");
        return -1;
    }
    // 2、查询设备、设置视频格式
    query_set_format();
    // 3、请求和分配缓冲区
    request_allocate_buffers();
    // 4 、开始捕获视频
    start_capture();
    // 5、获取和处理视频帧
    for (int i = 0; i < 100; i++) {
        capture_frame();
        printf("frame:%d\n", i);
    }
    // 6、停止捕获视频
    end_capture();
    // 7、解除缓冲区内存映射
    unmap_buffer();
    // 8、关闭摄像头
    close(fd);
    close(out_fd);
    return 0;
}
