# rk_yolo — RK3568 实时 YOLOv5 目标检测 (Qt GUI)

基于 RK3568 开发板，通过 V4L2 采集 USB 摄像头 MJPEG 视频流，经 NPU 推理实现实时目标检测，Qt5 Widgets 显示。

---

## 1. 项目结构

```
rk_yolo/
├── main.cpp                 # Qt 入口
├── widget.h                 # Widget + InferenceWorker 声明
├── widget.cpp               # V4L2采集 + 异步推理 + 画框显示
├── widget.ui                # Qt Designer UI 布局
├── rk_yolo.pro              # qmake 项目文件（含第三方库引用）
├── Makefile                 # qmake 生成的构建规则
├── model/
│   └── coco_80_labels_list.txt  # COCO 标签
└── README.md
```

> 后处理代码引用 `../rknn_C_API/src/postprocess.cc`（含 sigmoid 修复）

---

## 2. 架构设计

```
┌── 主线程 (Qt GUI) ──────────────────────┐
│  QTimer(33ms) → captureFrame()           │
│    ├─ V4L2 DQBUF (MJPEG帧, 非阻塞)        │
│    ├─ QImage MJPEG解码 (libjpeg)         │
│    ├─ 发图到推理线程 (异步)                │
│    ├─ 读最新检测结果 (QMutex)             │
│    ├─ QPainter 画框 + 标签               │
│    └─ QLabel::setPixmap 显示             │
└──────────────────────────────────────────┘
         │  QMetaObject::invokeMethod
         ▼
┌── 推理线程 (QThread + InferenceWorker) ──┐
│  doInference(QImage)                     │
│    ├─ Letterbox: 等比缩放+灰边填充 640×640│
│    ├─ rknn_inputs_set                    │
│    ├─ rknn_run (NPU 推理)                │
│    ├─ post_process (含 sigmoid 后处理)    │
│    └─ emit resultReady(boxes, time)      │
└──────────────────────────────────────────┘
```

**线程安全策略：**
- RKNN 上下文由主线程创建，**仅推理线程调用** — 单线程访问，无需锁
- 检测结果通过 `QMutex` 保护，主线程非阻塞读取最新框
- `QAtomicInt m_workerBusy` 控制丢帧 — 推理忙时不堆积新帧

---

## 3. 数据流管线

```
摄像头 MJPEG 800×600
  │
  ├─ Qt解码 → QImage RGB888 (主线程, ~3ms)
  │     │
  │     ├─→ 推理线程: Letterbox(RGB 640×640) → NPU推理(~70ms) → sigmoid后处理 → 框坐标
  │     │
  │     └─→ 主线程: QPainter 画框 ─→ QLabel::setPixmap
  │
  └─ 丢帧策略: 推理忙时跳过，不阻塞主线程
```

---

## 4. .pro 文件第三方库索引详解

### 4.1 完整 .pro 配置

```qmake
QT       += core gui
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets
CONFIG += c++17

# 源码
SOURCES += main.cpp widget.cpp ../rknn_C_API/src/postprocess.cc
HEADERS += widget.h
FORMS   += widget.ui

# ========================= 第三方 SDK 路径 =========================
RKNPU2_PATH = /home/alientek/software/rknpu2-master

!isEmpty(RKNPU2_PATH) {
    # ① RKNN API — NPU 推理运行时
    INCLUDEPATH += $${RKNPU2_PATH}/runtime/RK356X/Linux/librknn_api/include
    LIBS += -L$${RKNPU2_PATH}/runtime/RK356X/Linux/librknn_api/aarch64 -lrknnrt

    # ② RGA — 硬件图像缩放/颜色转换 (当前未使用，预留)
    INCLUDEPATH += $${RKNPU2_PATH}/examples/3rdparty/rga/RK356X/include
    LIBS += -L$${RKNPU2_PATH}/examples/3rdparty/rga/RK356X/lib/Linux/aarch64 -lrga

    # ③ MPP — 硬件编解码 (当前未使用，预留)
    INCLUDEPATH += $${RKNPU2_PATH}/examples/3rdparty/mpp/include
    LIBS += -L$${RKNPU2_PATH}/examples/3rdparty/mpp/Linux/aarch64 -lrockchip_mpp
}

# postprocess 头文件 (非 SDK，项目内引用)
INCLUDEPATH += ../rknn_C_API/include
```

### 4.2 各第三方库说明

| 库 | 头文件路径 (INCLUDEPATH) | 链接路径 (-L) | 库名 (-l) | 作用 |
|---|---|---|---|---|
| **RKNN** | `rknpu2/runtime/RK356X/Linux/librknn_api/include` | `.../aarch64` | `rknnrt` | NPU 推理运行时 API |
| **RGA** | `rknpu2/examples/3rdparty/rga/RK356X/include` | `rknpu2/.../rga/.../aarch64` | `rga` | 硬件图像 resize/cvtColor |
| **MPP** | `rknpu2/examples/3rdparty/mpp/include` | `rknpu2/.../mpp/.../aarch64` | `rockchip_mpp` | 硬件编解码 (MJPEG/H.264) |

### 4.3 SDK 目录树 (关键路径)

```
rknpu2-master/
├── runtime/RK356X/Linux/librknn_api/
│   ├── include/
│   │   └── rknn_api.h          # rknn_init, rknn_run, rknn_query ...
│   └── aarch64/
│       └── librknnrt.so        # NPU 运行时动态库
└── examples/3rdparty/
    ├── rga/RK356X/
    │   ├── include/
    │   │   ├── im2d.h           # imresize, imcheck, wrapbuffer_*
    │   │   ├── rga.h            # rga_buffer_t 类型
    │   │   └── im2d_type.h      # IM_STATUS, RK_FORMAT_*
    │   └── lib/Linux/aarch64/
    │       └── librga.so
    └── mpp/
        ├── include/rockchip/
        │   ├── rk_mpi.h         # mpp_create, mpp_init, MppApi
        │   ├── mpp_frame.h      # mpp_frame_get_*, MppFrame
        │   └── mpp_buffer.h     # mpp_buffer_get_ptr, mpp_buffer_get_fd
        └── Linux/aarch64/
            └── librockchip_mpp.so
```

### 4.4 qmake 变量说明

| 变量 | 语法 | 示例 |
|---|---|---|
| `INCLUDEPATH` | 追加头文件搜索路径 (`-I`) | `INCLUDEPATH += /path/to/include` |
| `LIBS` | 追加链接选项 | `LIBS += -L/path -lname` |
| `$${VAR}` | 展开自定义变量 | `$${RKNPU2_PATH}` |
| `!isEmpty(VAR)` | 条件：变量非空才执行 | `!isEmpty(RKNPU2_PATH) { ... }` |

### 4.5 切换编译器/套件

当前使用 ATOMPI-CA1 SDK 交叉编译链：

| 配置项 | 值 |
|---|---|
| 编译器 | `aarch64-rockchip1031-linux-gnu-g++` |
| qmake | `~/Qt/qt-everywhere-src-5.15.2/qtbase/qmake/qmake` |
| Qt目标 | `linux-aarch64-gnu-g++` (mkspec) |

如果需要切换到 `buildroot` 工具链：

```bash
/opt/atk-dlrk3568-5_10_sdk-toolchain/bin/qmake rk_yolo.pro
make -j8
```

---

## 5. 关键配置

```cpp
// widget.cpp
#define CAMERA_DEVICE          "/dev/video9"          // V4L2 设备节点
#define CAMERA_WIDTH           800
#define CAMERA_HEIGHT          600
#define FRAME_INTERVAL         33                     // Qt 定时器 ms
#define DEFAULT_MODEL_PATH     "/czh/model/yolov5s.rknn"
```

模型输出要求：**不含 sigmoid** (后处理代码已内置 sigmoid)

---

## 6. 编译

```bash
cd rk_yolo

# 生成 Makefile
/home/alientek/Qt/qt-everywhere-src-5.15.2/qtbase/qmake/qmake rk_yolo.pro

# 编译
make clean && make -j4
```

产物：`rk_yolo` (ELF 64-bit, ARM aarch64)

---

## 7. 部署到 RK3568 开发板

### 7.1 推送文件

```bash
# 可执行文件
adb push rk_yolo /czh/

# 模型 + 标签
adb push model/coco_80_labels_list.txt /czh/model/
adb push ../rknn_C_API/model/RK3566_RK3568/yolov5s.rknn /czh/model/

# 动态库（若开发板缺少）
adb push $RKNPU2_PATH/runtime/RK356X/Linux/librknn_api/aarch64/librknnrt.so  /czh/lib/
adb push $RKNPU2_PATH/examples/3rdparty/rga/RK356X/lib/Linux/aarch64/librga.so /czh/lib/
adb push $RKNPU2_PATH/examples/3rdparty/mpp/Linux/aarch64/librockchip_mpp.so   /czh/lib/
```

### 7.2 运行

```bash
export LD_LIBRARY_PATH=/czh/lib:$LD_LIBRARY_PATH
cd /czh && ./rk_yolo -platform linuxfb
```

> 必须在包含 `model/` 子目录的路径下运行（标签文件路径 `./model/coco_80_labels_list.txt` 硬编码在 `postprocess.cc` 中）。

---

## 8. 后处理 sigmoid 修复

`postprocess.cc` 含关键修改：模型输出 logits → `sigmoid()` 转为概率。

| 修改位置 | 内容 |
|---|---|
| 阈值 | `BOX_THRESH` → `unsigmoid(BOX_THRESH)` 转到 logit 空间 |
| tx, ty | `sigmoid(deqnt(x)) * 2.0 - 0.5` |
| tw, th | `sigmoid(deqnt(x)) * 2.0` → 平方 |
| obj_conf | `sigmoid(deqnt(x))` |
| cls_conf | `sigmoid(deqnt(x))` |

---

## 9. 模型导出参数建议

```python
# onnx2rknn.py 推荐配置
rknn.config(
    mean_values=[[0, 0, 0]],
    std_values=[[255, 255, 255]],
    target_platform="rk3568",
)

rknn.build(
    do_quantization=True,
    dataset="calibration.txt",      # ≥100张实际场景图片
    quantized_algorithm="mmse",     # 精度优先 (normal=速度优先)
    optimization_level=1,           # 保守优化 (3=最激进)
)
```

---

## 10. 已知问题与限制

| 问题 | 说明 |
|---|---|
| 帧率 | 摄像头 MJPEG 800×600 实际约 ~10fps (USB 带宽/驱动限制) |
| RKNN 线程安全 | `rknn_context` 非线程安全，仅单线程调用 |
| 标签路径 | 硬编码 `./model/coco_80_labels_list.txt`，运行目录必须含 `model/` |
| RGA/MPP | 已链接但未使用，预处理用 Qt CPU 缩放 (稳定优先) |
