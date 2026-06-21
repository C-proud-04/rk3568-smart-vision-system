#include "widget.h"
#include "ui_widget.h"

#include <QDebug>
#include <QPixmap>
#include <QPainter>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#define CAMERA_DEVICE   "/dev/video9"
#define CAMERA_WIDTH    800
#define CAMERA_HEIGHT   600
#define FRAME_INTERVAL  33

#define DEFAULT_MODEL_PATH  "/czh/model/yolov5s.rknn"

// 注册 detect_result_group_t 结构体
Q_DECLARE_METATYPE(detect_result_group_t)

// =================================================================
//  文件读取工具
// =================================================================
static unsigned char *load_data(FILE *fp, size_t ofst, size_t sz) {
    if (!fp) return nullptr;
    fseek(fp, ofst, SEEK_SET);
    auto *data = (unsigned char *)malloc(sz);
    if (data) fread(data, 1, sz, fp);
    return data;
}

static unsigned char *read_file_data(const char *filename, int *model_size) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) { qWarning("打开模型文件失败: %s", filename); return nullptr; }
    fseek(fp, 0, SEEK_END);
    int size = ftell(fp);
    auto *data = load_data(fp, 0, size);
    fclose(fp);
    *model_size = size;
    return data;
}

// =================================================================
//  InferenceWorker — 子线程：预处理 + NPU推理 + 后处理
// =================================================================
void InferenceWorker::init(rknn_context ctx, const rknn_input_output_num &ioNum,
                           int modelW, int modelH, int modelC,
                           const std::vector<float> &scales,
                           const std::vector<int32_t> &zps)
{
    m_ctx    = ctx;
    m_ioNum  = ioNum;
    m_modelW = modelW; m_modelH = modelH; m_modelC = modelC;
    m_scales = scales;
    m_zps    = zps;
    m_running.storeRelaxed(1);
}

void InferenceWorker::stop() { m_running.storeRelaxed(0); }

void InferenceWorker::doInference(QImage image)
{
    if (!m_running.loadRelaxed()) return;

    QElapsedTimer timer;
    timer.start();

    int origW = image.width(), origH = image.height();

    // ---- Letterbox 预处理 ----
    float scale = std::min((float)m_modelW / origW, (float)m_modelH / origH);
    int newW = (int)(origW * scale), newH = (int)(origH * scale);

    BOX_RECT pads;
    pads.left   = (m_modelW - newW) / 2;
    pads.top    = (m_modelH - newH) / 2;
    pads.right  = m_modelW - newW - pads.left;
    pads.bottom = m_modelH - newH - pads.top;

    QImage scaled = image.scaled(newW, newH,
                                 Qt::IgnoreAspectRatio, Qt::FastTransformation)
                        .convertToFormat(QImage::Format_RGB888);
    QImage letterbox(m_modelW, m_modelH, QImage::Format_RGB888);
    letterbox.fill(QColor(128, 128, 128));
    { QPainter p(&letterbox); p.drawImage(pads.left, pads.top, scaled); }

    // ---- RKNN 推理 ----
    rknn_input inputs[1];
    memset(inputs, 0, sizeof(inputs));
    inputs[0].index = 0;
    inputs[0].type  = RKNN_TENSOR_UINT8;
    inputs[0].size  = (uint32_t)(m_modelW * m_modelH * m_modelC);
    inputs[0].fmt   = RKNN_TENSOR_NHWC;
    inputs[0].buf   = (void *)letterbox.constBits();

    int ret = rknn_inputs_set(m_ctx, m_ioNum.n_input, inputs);
    if (ret < 0) { emit resultReady(detect_result_group_t(), 0); return; }

    rknn_output outputs[m_ioNum.n_output];
    memset(outputs, 0, sizeof(outputs));
    for (uint32_t i = 0; i < m_ioNum.n_output; i++)
        outputs[i].want_float = 0;

    ret = rknn_run(m_ctx, nullptr);
    if (ret < 0) { emit resultReady(detect_result_group_t(), 0); return; }

    ret = rknn_outputs_get(m_ctx, m_ioNum.n_output, outputs, nullptr);
    if (ret < 0) { emit resultReady(detect_result_group_t(), 0); return; }

    // ---- 后处理 ----
    detect_result_group_t result;
    memset(&result, 0, sizeof(result));
    post_process(
        (int8_t *)outputs[0].buf, (int8_t *)outputs[1].buf,
        (int8_t *)outputs[2].buf, m_modelH, m_modelW,
        BOX_THRESH, NMS_THRESH, pads, scale, scale,
        m_zps, m_scales, &result);

    rknn_outputs_release(m_ctx, m_ioNum.n_output, outputs);
    emit resultReady(result, (float)timer.elapsed());
}

// =================================================================
//  Widget 构造 / 析构
// =================================================================
Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
    , m_timer(nullptr)
    , m_v4l2Fd(-1)
    , m_isCameraOpen(false)
    , m_actualWidth(0), m_actualHeight(0)
    , m_buffers(nullptr), m_bufferLengths(nullptr), m_bufferCount(0)
    , m_rknnCtx(0)
    , m_inputAttrs(nullptr), m_outputAttrs(nullptr)
    , m_modelWidth(0), m_modelHeight(0), m_modelChannel(0)
    , m_isModelLoaded(false)
    , m_inferThread(nullptr), m_inferWorker(nullptr)
    , m_hasResult(false)
    , m_workerBusy(0)
{
    // 注册 detect_result_group_t 结构体，使其能在 signal-slot 中传递
    qRegisterMetaType<detect_result_group_t>("detect_result_group_t");

    ui->setupUi(this);
    m_timer = new QTimer(this);
    // 信号与槽建立连接
    connect(m_timer, &QTimer::timeout, this, &Widget::captureFrame);
    connect(ui->cameraButton, &QPushButton::clicked,
            this, &Widget::onCameraButtonClicked);

    // 加载模型
    if (!initModel(DEFAULT_MODEL_PATH)) {
        qWarning() << "RKNN 模型初始化失败";
        return;
    }

    // 创建推理线程
    // 线程对象
    m_inferThread = new QThread(this);
    // worker对象
    m_inferWorker = new InferenceWorker;
    m_inferWorker->init(m_rknnCtx, m_ioNum,
                        m_modelWidth, m_modelHeight, m_modelChannel,
                        m_outScales, m_outZps);
    // 移到子线程中，由信号触发
    m_inferWorker->moveToThread(m_inferThread);

    // 推理线程推理信号触发时触发主线程的onResultReady
    connect(m_inferWorker, &InferenceWorker::resultReady,
            this, &Widget::onResultReady);
    // 线程销毁同时销毁worker
    connect(m_inferThread, &QThread::finished,
            m_inferWorker, &QObject::deleteLater);
    
    // 开启线程
    m_inferThread->start();
}

// 析构函数
Widget::~Widget()
{
    closeCamera();
    if (m_inferWorker) { m_inferWorker->stop(); m_inferWorker->disconnect(); }
    if (m_inferThread) { m_inferThread->quit(); m_inferThread->wait(2000); }
    releaseModel();
    delete ui;
}

// =================================================================
//  摄像头开关
// =================================================================
void Widget::onCameraButtonClicked()
{
    if (m_isCameraOpen) {
        m_timer->stop(); closeCamera(); m_isCameraOpen = false;
        ui->cameraButton->setText("打开摄像头");
        ui->videoLabel->setText("摄像头未打开");
    } else {
        if (openCamera()) {
            m_isCameraOpen = true; m_timer->start(FRAME_INTERVAL);
            ui->cameraButton->setText("关闭摄像头");
        } else {
            ui->videoLabel->setText("摄像头打开失败\n请检查 /dev/video9");
        }
    }
}

// =================================================================
//  V4L2 打开/关闭摄像头
// =================================================================
bool Widget::openCamera()
{
    m_v4l2Fd = open(CAMERA_DEVICE, O_RDWR | O_NONBLOCK, 0);
    if (m_v4l2Fd < 0) { qWarning("无法打开 %s", CAMERA_DEVICE); return false; }

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = CAMERA_WIDTH; fmt.fmt.pix.height = CAMERA_HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    if (ioctl(m_v4l2Fd, VIDIOC_S_FMT, &fmt) < 0) {
        ::close(m_v4l2Fd); m_v4l2Fd = -1; return false;
    }
    m_actualWidth = fmt.fmt.pix.width; m_actualHeight = fmt.fmt.pix.height;

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 4; req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(m_v4l2Fd, VIDIOC_REQBUFS, &req) < 0) {
        ::close(m_v4l2Fd); m_v4l2Fd = -1; return false;
    }
    m_bufferCount = req.count;
    m_buffers = new unsigned char*[m_bufferCount];
    m_bufferLengths = new unsigned int[m_bufferCount];

    for (unsigned int i = 0; i < m_bufferCount; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP; buf.index = i;
        if (ioctl(m_v4l2Fd, VIDIOC_QUERYBUF, &buf) < 0)
            { closeCamera(); return false; }
        m_buffers[i] = (unsigned char*)mmap(nullptr, buf.length,
                    PROT_READ|PROT_WRITE, MAP_SHARED, m_v4l2Fd, buf.m.offset);
        if (m_buffers[i] == MAP_FAILED) { closeCamera(); return false; }
        m_bufferLengths[i] = buf.length;
    }
    for (unsigned int i = 0; i < m_bufferCount; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP; buf.index = i;
        if (ioctl(m_v4l2Fd, VIDIOC_QBUF, &buf) < 0)
            { closeCamera(); return false; }
    }
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(m_v4l2Fd, VIDIOC_STREAMON, &type) < 0)
        { closeCamera(); return false; }
    return true;
}

void Widget::closeCamera()
{
    if (m_v4l2Fd >= 0) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(m_v4l2Fd, VIDIOC_STREAMOFF, &type);
    }
    if (m_buffers) {
        for (unsigned int i = 0; i < m_bufferCount; i++)
            if (m_buffers[i] && m_buffers[i] != MAP_FAILED)
                munmap(m_buffers[i], m_bufferLengths[i]);
        delete[] m_buffers; m_buffers = nullptr;
    }
    delete[] m_bufferLengths; m_bufferLengths = nullptr;
    m_bufferCount = 0;
    if (m_v4l2Fd >= 0) { ::close(m_v4l2Fd); m_v4l2Fd = -1; }
}

// =================================================================
//  RKNN 模型加载（主线程）
// =================================================================
bool Widget::initModel(const QString &modelPath)
{
    if (m_isModelLoaded) return true;

    int modelSize = 0;
    auto *modelData = read_file_data(modelPath.toUtf8().constData(), &modelSize);
    if (!modelData) return false;

    int ret = rknn_init(&m_rknnCtx, modelData, modelSize, 0, nullptr);
    free(modelData);
    if (ret < 0) { qWarning("rknn_init 失败 ret=%d", ret); return false; }

    rknn_sdk_version ver;
    rknn_query(m_rknnCtx, RKNN_QUERY_SDK_VERSION, &ver, sizeof(ver));
    qDebug() << "RKNN SDK:" << ver.api_version;

    rknn_query(m_rknnCtx, RKNN_QUERY_IN_OUT_NUM, &m_ioNum, sizeof(m_ioNum));

    m_inputAttrs = (rknn_tensor_attr*)malloc(
        m_ioNum.n_input * sizeof(rknn_tensor_attr));
    memset(m_inputAttrs, 0, m_ioNum.n_input * sizeof(rknn_tensor_attr));
    for (uint32_t i = 0; i < m_ioNum.n_input; i++) {
        m_inputAttrs[i].index = i;
        rknn_query(m_rknnCtx, RKNN_QUERY_INPUT_ATTR,
                   &m_inputAttrs[i], sizeof(rknn_tensor_attr));
    }

    // 打印模型输入tensor和输出tensor的属性
    m_outputAttrs = (rknn_tensor_attr*)malloc(
        m_ioNum.n_output * sizeof(rknn_tensor_attr));
    memset(m_outputAttrs, 0, m_ioNum.n_output * sizeof(rknn_tensor_attr));
    for (uint32_t i = 0; i < m_ioNum.n_output; i++) {
        m_outputAttrs[i].index = i;
        rknn_query(m_rknnCtx, RKNN_QUERY_OUTPUT_ATTR,
                   &m_outputAttrs[i], sizeof(rknn_tensor_attr));
        m_outScales.push_back(m_outputAttrs[i].scale);
        m_outZps.push_back(m_outputAttrs[i].zp);
    }

    if (m_inputAttrs[0].fmt == RKNN_TENSOR_NCHW) {
        m_modelChannel = m_inputAttrs[0].dims[1];
        m_modelHeight  = m_inputAttrs[0].dims[2];
        m_modelWidth   = m_inputAttrs[0].dims[3];
    } else {
        m_modelHeight  = m_inputAttrs[0].dims[1];
        m_modelWidth   = m_inputAttrs[0].dims[2];
        m_modelChannel = m_inputAttrs[0].dims[3];
    }
    qDebug() << "模型输入:" << m_modelWidth << "x" << m_modelHeight;

    m_isModelLoaded = true;
    return true;
}

void Widget::releaseModel()
{
    if (m_rknnCtx) { rknn_destroy(m_rknnCtx); m_rknnCtx = 0; }
    free(m_inputAttrs);  m_inputAttrs  = nullptr;
    free(m_outputAttrs); m_outputAttrs = nullptr;
    m_outScales.clear(); m_outZps.clear();
    deinitPostProcess();
    m_isModelLoaded = false;
}

// =================================================================
//  推理结果回调（子线程 emit，主线程槽执行）
// =================================================================
void Widget::onResultReady(detect_result_group_t result, float inferMs)
{
    QMutexLocker lock(&m_resultMutex);
    m_latestResult    = result;
    m_hasResult       = true;
    m_workerBusy.storeRelaxed(0);
}

// =================================================================
//  主线程：取帧 → 异步推理 → 画最新框 → 显示
// =================================================================
void Widget::captureFrame()
{
    if (!m_isCameraOpen || m_v4l2Fd < 0) return;

    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if (ioctl(m_v4l2Fd, VIDIOC_DQBUF, &buf) < 0) return;

    QImage rgbImage;
    if (!rgbImage.loadFromData(m_buffers[buf.index], buf.bytesused, "JPEG")) {
        ioctl(m_v4l2Fd, VIDIOC_QBUF, &buf); return;
    }
    if (rgbImage.format() != QImage::Format_RGB888)
        rgbImage = rgbImage.convertToFormat(QImage::Format_RGB888);

    // 推理线程空闲时发送新帧（丢帧策略：不堆积）
    if (m_isModelLoaded && m_workerBusy.loadRelaxed() == 0) {
        m_workerBusy.storeRelaxed(1);
        QMetaObject::invokeMethod(m_inferWorker, "doInference",
                                  Qt::QueuedConnection,
                                  Q_ARG(QImage, rgbImage));
    }

    // 取最新推理结果画框
    detect_result_group_t drawResult;
    bool hasDraw = false;
    {
        QMutexLocker lock(&m_resultMutex);
        if (m_hasResult) {
            drawResult = m_latestResult;
            hasDraw = true;
        }
    }

    if (hasDraw && drawResult.count > 0) {
        QPainter painter(&rgbImage);
        painter.setRenderHint(QPainter::Antialiasing, true);
        for (int i = 0; i < drawResult.count; i++) {
            detect_result_t *d = &drawResult.results[i];
            int x = d->box.left, y = d->box.top;
            int w = d->box.right - d->box.left;
            int h = d->box.bottom - d->box.top;

            QPen pen(QColor(255,0,0)); pen.setWidth(3);
            painter.setPen(pen); painter.setBrush(Qt::NoBrush);
            painter.drawRect(x, y, w, h);

            QString label = QString("%1 %2%")
                .arg(d->name).arg(d->prop * 100, 0, 'f', 1);
            painter.setFont(QFont("Arial", 14));
            QFontMetrics fm(painter.font());
            int tw = fm.horizontalAdvance(label) + 8, th = fm.height() + 4;
            QRect tr(x, y - th, tw, th);
            if (tr.top() < 0) tr.moveTop(y);
            painter.setBrush(QColor(255,0,0,180));
            painter.setPen(Qt::NoPen); painter.drawRect(tr);
            painter.setPen(Qt::white);
            painter.drawText(tr.adjusted(4,2,0,0), label);
        }
        painter.end();
    }

    ui->videoLabel->setPixmap(
        QPixmap::fromImage(rgbImage).scaled(
            CAMERA_WIDTH, CAMERA_HEIGHT,
            Qt::KeepAspectRatio, Qt::SmoothTransformation));

    if (ioctl(m_v4l2Fd, VIDIOC_QBUF, &buf) < 0)
        qWarning("VIDIOC_QBUF 失败");
}
