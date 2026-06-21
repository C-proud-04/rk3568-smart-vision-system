/*
 * v4l2_sdl_demo.c
 *
 * 本程序使用V4L2接口从USB摄像头实时采集视频帧，并通过SDL2显示。
 * 支持 YUYV 和 MJPEG 两种常见USB摄像头格式。
 *
 * 编译方式:
 *   本地编译:  gcc -o v4l2_sdl_demo v4l2_sdl_demo.c -lSDL2 -ljpeg
 *   交叉编译:  参考 build.sh 脚本
 *
 * 使用方法: ./v4l2_sdl_demo [设备名] [宽度] [高度]
 *   默认设备: /dev/video9
 *   默认分辨率: 640x480
 *
 * 按键: ESC 或 关闭窗口 退出
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <errno.h>
#include <linux/videodev2.h>
#include <SDL2/SDL.h>
/* MJPEG 支持需要 libjpeg，如果目标系统没有可注释掉 */
/* #define ENABLE_MJPEG */
#ifdef ENABLE_MJPEG
#include <jpeglib.h>
#include <setjmp.h>
#endif

/* ========== 默认配置 ========== */
#define DEFAULT_DEVICE   "/dev/video9"
#define DEFAULT_WIDTH    640
#define DEFAULT_HEIGHT   480
#define BUFFER_COUNT     4

/* ========== V4L2 缓冲区结构 ========== */
typedef struct {
    void   *start;
    size_t  length;
} V4L2Buffer;

/* ========== 全局变量 ========== */
static int cam_fd = -1;
static V4L2Buffer *buffers = NULL;
static int buf_count = 0;
static int width  = DEFAULT_WIDTH;
static int height = DEFAULT_HEIGHT;
static unsigned int pixelformat = 0;

/* ========== V4L2 函数 ========== */

/* 打开摄像头设备 */
static int v4l2_open(const char *device)
{
    cam_fd = open(device, O_RDWR | O_NONBLOCK, 0);
    if (cam_fd < 0) {
        perror("打开摄像头失败");
        return -1;
    }

    /* 查询设备能力 */
    struct v4l2_capability cap;
    if (ioctl(cam_fd, VIDIOC_QUERYCAP, &cap) < 0) {
        perror("VIDIOC_QUERYCAP");
        close(cam_fd);
        return -1;
    }
    printf("[INFO] 摄像头: %s\n", cap.card);
    printf("[INFO] 驱动:   %s\n", cap.driver);
    printf("[INFO] 总线:   %s\n", cap.bus_info);

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "[ERROR] 设备不支持视频捕获\n");
        close(cam_fd);
        return -1;
    }
    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "[ERROR] 设备不支持流式I/O\n");
        close(cam_fd);
        return -1;
    }
    return 0;
}

/* 列出支持的格式 */
static void v4l2_list_formats(void)
{
    struct v4l2_fmtdesc fmtdesc;
    memset(&fmtdesc, 0, sizeof(fmtdesc));
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    printf("[INFO] 支持的格式:\n");
    for (fmtdesc.index = 0; ioctl(cam_fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0; fmtdesc.index++) {
        printf("       %d. %s (0x%08X)\n", fmtdesc.index + 1,
               fmtdesc.description, fmtdesc.pixelformat);
    }
}

/* 设置视频格式，优先 YUYV，其次 MJPEG */
static int v4l2_set_format(int try_mjpeg)
{
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width  = width;
    fmt.fmt.pix.height = height;

    if (try_mjpeg) {
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
        fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;
    } else {
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
        fmt.fmt.pix.field = V4L2_FIELD_NONE;
    }

    if (ioctl(cam_fd, VIDIOC_S_FMT, &fmt) < 0) {
        if (!try_mjpeg) {
            /* YUYV 失败，尝试 MJPEG */
            printf("[WARN] YUYV 格式设置失败，尝试 MJPEG...\n");
            return v4l2_set_format(1);
        }
        perror("VIDIOC_S_FMT");
        return -1;
    }

    /* 读取实际格式 */
    if (ioctl(cam_fd, VIDIOC_G_FMT, &fmt) < 0) {
        perror("VIDIOC_G_FMT");
        return -1;
    }

    width       = fmt.fmt.pix.width;
    height      = fmt.fmt.pix.height;
    pixelformat = fmt.fmt.pix.pixelformat;

    printf("[INFO] 实际分辨率: %dx%d\n", width, height);
    printf("[INFO] 像素格式:   %c%c%c%c (0x%08X)\n",
           (pixelformat >> 0)  & 0xFF,
           (pixelformat >> 8)  & 0xFF,
           (pixelformat >> 16) & 0xFF,
           (pixelformat >> 24) & 0xFF,
           pixelformat);
    printf("[INFO] 每帧大小:   %d bytes\n", fmt.fmt.pix.sizeimage);

    return 0;
}

/* 请求并映射缓冲区 */
static int v4l2_request_buffers(void)
{
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count  = BUFFER_COUNT;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(cam_fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("VIDIOC_REQBUFS");
        return -1;
    }

    buf_count = req.count;
    printf("[INFO] 申请了 %d 个缓冲区\n", buf_count);

    buffers = (V4L2Buffer *)calloc(buf_count, sizeof(V4L2Buffer));
    if (!buffers) {
        perror("calloc");
        return -1;
    }

    for (int i = 0; i < buf_count; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;

        if (ioctl(cam_fd, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("VIDIOC_QUERYBUF");
            return -1;
        }

        buffers[i].length = buf.length;
        buffers[i].start = mmap(NULL, buf.length,
                                PROT_READ | PROT_WRITE,
                                MAP_SHARED, cam_fd, buf.m.offset);
        if (buffers[i].start == MAP_FAILED) {
            perror("mmap");
            return -1;
        }

        /* 将缓冲区放入输入队列 */
        if (ioctl(cam_fd, VIDIOC_QBUF, &buf) < 0) {
            perror("VIDIOC_QBUF");
            return -1;
        }

        printf("[INFO] 缓冲区[%d]: 地址=0x%lx, 长度=%d\n",
               i, (unsigned long)buffers[i].start, (int)buffers[i].length);
    }
    return 0;
}

/* 启动视频流 */
static int v4l2_start_stream(void)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(cam_fd, VIDIOC_STREAMON, &type) < 0) {
        perror("VIDIOC_STREAMON");
        return -1;
    }
    printf("[INFO] 视频流已启动\n");
    return 0;
}

/* 停止视频流 */
static void v4l2_stop_stream(void)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(cam_fd, VIDIOC_STREAMOFF, &type);
    printf("[INFO] 视频流已停止\n");
}

/* 释放缓冲区 */
static void v4l2_free_buffers(void)
{
    if (buffers) {
        for (int i = 0; i < buf_count; i++) {
            if (buffers[i].start) {
                munmap(buffers[i].start, buffers[i].length);
            }
        }
        free(buffers);
        buffers = NULL;
    }
}

/* 关闭摄像头 */
static void v4l2_close(void)
{
    if (cam_fd >= 0) {
        close(cam_fd);
        cam_fd = -1;
    }
}

/* 获取一帧数据（非阻塞，带超时） */
static int v4l2_get_frame(void **data, size_t *size)
{
    fd_set fds;
    struct timeval tv;
    FD_ZERO(&fds);
    FD_SET(cam_fd, &fds);

    tv.tv_sec  = 2;
    tv.tv_usec = 0;

    int ret = select(cam_fd + 1, &fds, NULL, NULL, &tv);
    if (ret < 0) {
        perror("select");
        return -1;
    }
    if (ret == 0) {
        return 1; /* 超时，无数据 */
    }

    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(cam_fd, VIDIOC_DQBUF, &buf) < 0) {
        if (errno == EAGAIN) return 1;
        perror("VIDIOC_DQBUF");
        return -1;
    }

    *data = buffers[buf.index].start;
    *size = buf.bytesused;

    /* 重新入队 */
    if (ioctl(cam_fd, VIDIOC_QBUF, &buf) < 0) {
        perror("VIDIOC_QBUF");
        return -1;
    }

    return 0;
}

/* ========== YUYV -> RGB 转换 ========== */

/*
 * YUYV (YUY2) 数据打包方式: Y0 U0 Y1 V0  Y2 U1 Y3 V1 ...
 * 每4字节 = 2个像素
 */
static void yuyv_to_rgb888(const unsigned char *yuyv, unsigned char *rgb,
                            int w, int h)
{
    int num_pixels = w * h;
    for (int i = 0; i < num_pixels; i += 2) {
        int base = (i / 2) * 4;  /* YUYV 中每4字节对应2像素 */
        int y0 = yuyv[base + 0];
        int u  = yuyv[base + 1] - 128;
        int y1 = yuyv[base + 2];
        int v  = yuyv[base + 3] - 128;

        int r, g, b;

        /* 像素0 */
        r = (y0 + ((359 * v) >> 8));
        g = (y0 - (( 88 * u) >> 8) - ((183 * v) >> 8));
        b = (y0 + ((454 * u) >> 8));
        r = r < 0 ? 0 : (r > 255 ? 255 : r);
        g = g < 0 ? 0 : (g > 255 ? 255 : g);
        b = b < 0 ? 0 : (b > 255 ? 255 : b);
        rgb[i * 3 + 0] = (unsigned char)r;
        rgb[i * 3 + 1] = (unsigned char)g;
        rgb[i * 3 + 2] = (unsigned char)b;

        /* 像素1 */
        r = (y1 + ((359 * v) >> 8));
        g = (y1 - (( 88 * u) >> 8) - ((183 * v) >> 8));
        b = (y1 + ((454 * u) >> 8));
        r = r < 0 ? 0 : (r > 255 ? 255 : r);
        g = g < 0 ? 0 : (g > 255 ? 255 : g);
        b = b < 0 ? 0 : (b > 255 ? 255 : b);
        rgb[(i + 1) * 3 + 0] = (unsigned char)r;
        rgb[(i + 1) * 3 + 1] = (unsigned char)g;
        rgb[(i + 1) * 3 + 2] = (unsigned char)b;
    }
}

/* ========== MJPEG 解码 (简易libjpeg封装) ========== */
#ifdef ENABLE_MJPEG

struct jpeg_error_mgr_wrapper {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
};

static void jpeg_error_exit(j_common_ptr cinfo)
{
    struct jpeg_error_mgr_wrapper *myerr = (struct jpeg_error_mgr_wrapper *)cinfo->err;
    (*cinfo->err->output_message)(cinfo);
    longjmp(myerr->setjmp_buffer, 1);
}

static int mjpeg_to_rgb888(const unsigned char *jpeg_data, size_t jpeg_size,
                            unsigned char *rgb, int w, int h)
{
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr_wrapper jerr;

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpeg_error_exit;

    if (setjmp(jerr.setjmp_buffer)) {
        jpeg_destroy_decompress(&cinfo);
        return -1;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, jpeg_data, jpeg_size);
    jpeg_read_header(&cinfo, TRUE);

    /* 强制输出 RGB (3字节/像素) */
    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);

    int row_stride = cinfo.output_width * cinfo.output_components;
    while (cinfo.output_scanline < cinfo.output_height) {
        unsigned char *row_ptr = rgb + cinfo.output_scanline * row_stride;
        jpeg_read_scanlines(&cinfo, &row_ptr, 1);
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    return 0;
}

#else
/* 无 libjpeg 时：MJPEG 不支持，打印警告 */
static int mjpeg_to_rgb888(const unsigned char *jpeg_data, size_t jpeg_size,
                            unsigned char *rgb, int w, int h)
{
    (void)jpeg_data; (void)jpeg_size; (void)rgb; (void)w; (void)h;
    fprintf(stderr, "[WARN] MJPEG 格式需要 libjpeg 支持，请编译时定义 ENABLE_MJPEG 并链接 -ljpeg\n");
    return -1;
}
#endif

/* ========== SDL2 显示 ========== */

static SDL_Window   *sdl_window   = NULL;
static SDL_Renderer *sdl_renderer = NULL;
static SDL_Texture  *sdl_texture  = NULL;
static unsigned char *rgb_buffer  = NULL;

static int sdl_init(int w, int h)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) {
        fprintf(stderr, "[ERROR] SDL初始化失败: %s\n", SDL_GetError());
        return -1;
    }

    sdl_window = SDL_CreateWindow(
        "V4L2 + SDL2 摄像头实时预览 (RK3568)",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        w, h,
        SDL_WINDOW_SHOWN);
    if (!sdl_window) {
        fprintf(stderr, "[ERROR] SDL窗口创建失败: %s\n", SDL_GetError());
        return -1;
    }

    sdl_renderer = SDL_CreateRenderer(sdl_window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!sdl_renderer) {
        /* 尝试软件渲染 */
        sdl_renderer = SDL_CreateRenderer(sdl_window, -1,
            SDL_RENDERER_SOFTWARE);
    }
    if (!sdl_renderer) {
        fprintf(stderr, "[ERROR] SDL渲染器创建失败: %s\n", SDL_GetError());
        return -1;
    }

    sdl_texture = SDL_CreateTexture(sdl_renderer,
        SDL_PIXELFORMAT_RGB24,
        SDL_TEXTUREACCESS_STREAMING,
        w, h);
    if (!sdl_texture) {
        fprintf(stderr, "[ERROR] SDL纹理创建失败: %s\n", SDL_GetError());
        return -1;
    }

    rgb_buffer = (unsigned char *)malloc(w * h * 3);
    if (!rgb_buffer) {
        fprintf(stderr, "[ERROR] RGB缓冲区分配失败\n");
        return -1;
    }

    printf("[INFO] SDL2 初始化成功: %dx%d\n", w, h);
    printf("[INFO] 渲染器: %s\n", SDL_GetCurrentVideoDriver());
    return 0;
}

static void sdl_display_frame(const unsigned char *rgb, int w, int h)
{
    SDL_UpdateTexture(sdl_texture, NULL, rgb, w * 3);
    SDL_RenderClear(sdl_renderer);
    SDL_RenderCopy(sdl_renderer, sdl_texture, NULL, NULL);
    SDL_RenderPresent(sdl_renderer);
}

static int sdl_handle_events(void)
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            return 0;
        }
        if (event.type == SDL_KEYDOWN) {
            if (event.key.keysym.sym == SDLK_ESCAPE ||
                event.key.keysym.sym == SDLK_q) {
                return 0;
            }
        }
    }
    return 1;
}

static void sdl_cleanup(void)
{
    if (rgb_buffer)   { free(rgb_buffer); rgb_buffer = NULL; }
    if (sdl_texture)  { SDL_DestroyTexture(sdl_texture); sdl_texture = NULL; }
    if (sdl_renderer) { SDL_DestroyRenderer(sdl_renderer); sdl_renderer = NULL; }
    if (sdl_window)   { SDL_DestroyWindow(sdl_window); sdl_window = NULL; }
    SDL_Quit();
    printf("[INFO] SDL2 已清理\n");
}

/* ========== 主函数 ========== */

int main(int argc, char *argv[])
{
    const char *device = DEFAULT_DEVICE;
    int req_width  = DEFAULT_WIDTH;
    int req_height = DEFAULT_HEIGHT;

    /* 解析命令行参数 */
    if (argc > 1) device = argv[1];
    if (argc > 2) req_width  = atoi(argv[2]);
    if (argc > 3) req_height = atoi(argv[3]);

    width  = req_width;
    height = req_height;

    printf("========================================\n");
    printf("  V4L2 + SDL2 摄像头实时预览 Demo\n");
    printf("  RK3568 平台\n");
    printf("========================================\n");
    printf("[INFO] 设备: %s\n", device);
    printf("[INFO] 目标分辨率: %dx%d\n", width, height);

    /* 1. 打开摄像头 */
    if (v4l2_open(device) < 0) return 1;

    /* 2. 列出支持的格式 */
    v4l2_list_formats();

    /* 3. 设置视频格式 */
    if (v4l2_set_format(0) < 0) {
        v4l2_close();
        return 1;
    }

    /* 4. 请求并映射缓冲区 */
    if (v4l2_request_buffers() < 0) {
        v4l2_close();
        return 1;
    }

    /* 5. 启动视频流 */
    if (v4l2_start_stream() < 0) {
        v4l2_free_buffers();
        v4l2_close();
        return 1;
    }

    /* 6. 初始化 SDL2 */
    if (sdl_init(width, height) < 0) {
        v4l2_stop_stream();
        v4l2_free_buffers();
        v4l2_close();
        return 1;
    }

    /* 7. 主循环：采集 -> 转换 -> 显示 */
    printf("\n[INFO] 开始实时预览，按 ESC 或 Q 退出...\n");

    int frame_count = 0;
    int running = 1;

    while (running) {
        /* 处理 SDL 事件 */
        running = sdl_handle_events();
        if (!running) break;

        /* 获取一帧 */
        void *frame_data = NULL;
        size_t frame_size = 0;
        int ret = v4l2_get_frame(&frame_data, &frame_size);

        if (ret == 0 && frame_data && frame_size > 0) {
            /* 根据格式转换为 RGB */
            if (pixelformat == V4L2_PIX_FMT_MJPEG) {
                if (mjpeg_to_rgb888((unsigned char *)frame_data, frame_size,
                                    rgb_buffer, width, height) < 0) {
                    fprintf(stderr, "[WARN] MJPEG 解码失败\n");
                }
            } else {
                /* YUYV 或其他 YUV 格式 */
                yuyv_to_rgb888((unsigned char *)frame_data, rgb_buffer,
                               width, height);
            }

            /* SDL 显示 */
            sdl_display_frame(rgb_buffer, width, height);
            frame_count++;

            if (frame_count % 100 == 0) {
                printf("[INFO] 已显示 %d 帧, 分辨率: %dx%d, 格式: %c%c%c%c\n",
                       frame_count, width, height,
                       (pixelformat >> 0)  & 0xFF,
                       (pixelformat >> 8)  & 0xFF,
                       (pixelformat >> 16) & 0xFF,
                       (pixelformat >> 24) & 0xFF);
            }
        } else if (ret == 1) {
            /* 超时，继续 */
            SDL_Delay(1);
        } else {
            fprintf(stderr, "[ERROR] 获取帧失败\n");
            break;
        }
    }

    printf("\n[INFO] 退出，共显示 %d 帧\n", frame_count);

    /* 8. 清理 */
    sdl_cleanup();
    v4l2_stop_stream();
    v4l2_free_buffers();
    v4l2_close();

    printf("[INFO] 程序正常退出\n");
    return 0;
}
