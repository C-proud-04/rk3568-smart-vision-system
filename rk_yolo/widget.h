#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QTimer>
#include <QImage>
#include <QElapsedTimer>
#include <QThread>
#include <QMutex>
#include <QAtomicInt>

#include <vector>

extern "C" {
#include <linux/videodev2.h>
#include <rockchip/rk_mpi.h>
#include <rockchip/mpp_frame.h>
}

#include "rknn_api.h"
#include "postprocess.h"
#include "im2d.h"
#include "rga.h"

QT_BEGIN_NAMESPACE
namespace Ui { class Widget; }
QT_END_NAMESPACE

// 推理工作线程：预处理 + NPU + 后处理
class InferenceWorker : public QObject
{
    Q_OBJECT
public:
    void init(rknn_context ctx, const rknn_input_output_num &ioNum,
              int modelW, int modelH, int modelC,
              const std::vector<float> &scales,
              const std::vector<int32_t> &zps);
    void stop();

public slots:
    void doInference(QImage image);          // 主线程 → 工作线程

signals:
    void resultReady(detect_result_group_t result, float inferMs);

private:
    rknn_context         m_ctx;
    rknn_input_output_num m_ioNum;
    int                  m_modelW, m_modelH, m_modelC;
    std::vector<float>   m_scales;
    std::vector<int32_t> m_zps;
    QAtomicInt           m_running;
};

class Widget : public QWidget
{
    Q_OBJECT

public:
    Widget(QWidget *parent = nullptr);
    ~Widget();

private slots:
    void onCameraButtonClicked();
    void captureFrame();
    void onResultReady(detect_result_group_t result, float inferMs);

private:
    bool openCamera();
    void closeCamera();
    bool initModel(const QString &modelPath);
    void releaseModel();

    Ui::Widget         *ui;
    QTimer             *m_timer;
    int                 m_v4l2Fd;
    bool                m_isCameraOpen;
    int                 m_actualWidth, m_actualHeight;
    unsigned char     **m_buffers;
    unsigned int       *m_bufferLengths;
    unsigned int        m_bufferCount;

    // RKNN 上下文（主线程创建，工作线程使用）
    rknn_context          m_rknnCtx;
    rknn_input_output_num m_ioNum;
    rknn_tensor_attr     *m_inputAttrs;
    rknn_tensor_attr     *m_outputAttrs;
    int                   m_modelWidth, m_modelHeight, m_modelChannel;
    bool                  m_isModelLoaded;
    std::vector<float>    m_outScales;
    std::vector<int32_t>  m_outZps;

    // 推理线程
    QThread             *m_inferThread;
    InferenceWorker     *m_inferWorker;

    // 最新结果（互斥保护）
    QMutex               m_resultMutex;
    detect_result_group_t m_latestResult;
    bool                  m_hasResult;
    QAtomicInt            m_workerBusy;
};
#endif // WIDGET_H
