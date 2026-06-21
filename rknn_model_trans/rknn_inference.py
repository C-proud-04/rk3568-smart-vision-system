"""
YOLOv5n RKNN 推理脚本
模型: yolov5n.rknn (输入 640x640, int8 量化, RK3568)
"""
from rknn.api import RKNN
import cv2
import numpy as np

# ======================== 配置参数 ========================
USE_SIMULATOR = True            # True=PC模拟, False=开发板推理

RKNN_MODEL_PATH = "/home/alientek/czh/yolov5s.rknn"
ONNX_MODEL_PATH = "/home/alientek/czh/yolov5s.onnx"          # 模拟时用
CALIB_DATASET  = "/home/alientek/czh/dateset/calibration.txt" # 模拟时用

IMG_SIZE = 640               # 模型输入尺寸
CONF_THRESHOLD = 0.25        # 置信度阈值
NMS_THRESHOLD = 0.45         # NMS 阈值
STRIDES = [8, 16, 32]        # YOLOv5 三个检测头的 stride

# COCO 80 类别名称
CLASS_NAMES = [
    "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck",
    "boat", "traffic light", "fire hydrant", "stop sign", "parking meter", "bench",
    "bird", "cat", "dog", "horse", "sheep", "cow", "elephant", "bear", "zebra",
    "giraffe", "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee",
    "skis", "snowboard", "sports ball", "kite", "baseball bat", "baseball glove",
    "skateboard", "surfboard", "tennis racket", "bottle", "wine glass", "cup",
    "fork", "knife", "spoon", "bowl", "banana", "apple", "sandwich", "orange",
    "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "couch",
    "potted plant", "bed", "dining table", "toilet", "tv", "laptop", "mouse",
    "remote", "keyboard", "cell phone", "microwave", "oven", "toaster", "sink",
    "refrigerator", "book", "clock", "vase", "scissors", "teddy bear", "hair drier",
    "toothbrush",
]

# YOLOv5n 默认 anchor (640×640 输入)
# 输出网格: 80×80 (stride=8) + 40×40 (stride=16) + 20×20 (stride=32)
# 每网格 3 个 anchor → 80²×3 + 40²×3 + 20²×3 = 25200 个预测
ANCHORS = [
    [[10, 13],  [16, 30],  [33, 23]],        # P3/8  (小目标)
    [[30, 61],  [62, 45],  [59, 119]],       # P4/16 (中目标)
    [[116, 90], [156, 198], [373, 326]],     # P5/32 (大目标)
]


# ======================== 预处理 ========================
def letterbox(im, new_shape=(640, 640), color=(0, 0, 0)):
    shape = im.shape[:2]  # current shape [height, width]
    if isinstance(new_shape, int): # 是否是一个整数
        new_shape = (new_shape, new_shape)

    # 比例 (new / old)
    r = min(new_shape[0] / shape[0], new_shape[1] / shape[1])

    # 计算填充
    ratio = r  # ratios
    new_unpad = int(round(shape[1] * r)), int(round(shape[0] * r))
    dw, dh = new_shape[1] - new_unpad[0], new_shape[0] - new_unpad[1]  # wh padding

    dw /= 2  # divide padding into 2 sides
    dh /= 2

    if shape[::-1] != new_unpad:  # resize
        im = cv2.resize(im, new_unpad, interpolation=cv2.INTER_LINEAR)
    top, bottom = int(round(dh - 0.1)), int(round(dh + 0.1))
    left, right = int(round(dw - 0.1)), int(round(dw + 0.1))
    im = cv2.copyMakeBorder(im, top, bottom, left, right, cv2.BORDER_CONSTANT, value=color)  # add border
    return im, ratio, (dw, dh)


# ======================== 后处理 ========================
def sigmoid(x):
    return 1.0 / (1.0 + np.exp(-x))


def xywh2xyxy(x):
    """将 [cx, cy, w, h] 转换为 [x1, y1, x2, y2]"""
    y = np.copy(x)
    y[:, 0] = x[:, 0] - x[:, 2] / 2
    y[:, 1] = x[:, 1] - x[:, 3] / 2
    y[:, 2] = x[:, 0] + x[:, 2] / 2
    y[:, 3] = x[:, 1] + x[:, 3] / 2
    return y


def nms(boxes, scores, threshold):
    """非极大值抑制"""
    if len(boxes) == 0:
        return []
    x1 = boxes[:, 0]
    y1 = boxes[:, 1]
    x2 = boxes[:, 2]
    y2 = boxes[:, 3]
    areas = (x2 - x1 + 1) * (y2 - y1 + 1)
    order = scores.argsort()[::-1]

    keep = []
    while order.size > 0:
        i = order[0]
        keep.append(i)
        xx1 = np.maximum(x1[i], x1[order[1:]])
        yy1 = np.maximum(y1[i], y1[order[1:]])
        xx2 = np.minimum(x2[i], x2[order[1:]])
        yy2 = np.minimum(y2[i], y2[order[1:]])
        w = np.maximum(0.0, xx2 - xx1 + 1)
        h = np.maximum(0.0, yy2 - yy1 + 1)
        inter = w * h
        iou = inter / (areas[i] + areas[order[1:]] - inter)
        inds = np.where(iou <= threshold)[0]
        order = order[inds + 1]
    return keep


def yolov5_postprocess(output, img_shape, letterbox_ratio, pad_offset,
                       conf_thres=CONF_THRESHOLD, nms_thres=NMS_THRESHOLD):
    """
    YOLOv5 输出后处理：解码 + NMS
    使用经典 YOLO 解码公式（RKNN 转换常用）：
      bx = (σ(tx) + cx) × stride    ← 框中心 x
      by = (σ(ty) + cy) × stride    ← 框中心 y
      bw = anchor_w × exp(tw)       ← 框宽 (anchor 为像素单位)
      bh = anchor_h × exp(th)       ← 框高

    output:  numpy array, shape [1, 25200, 85]  (NHWC 格式)
             25200 = 80²×3 + 40²×3 + 20²×3
             85   = tx,ty,tw,th + obj + 80 classes
    """
    output = output[0]                     # [25200, 85]
    pad_left, pad_top = pad_offset

    # ================================================================
    # 步骤 1：构建网格 + anchor（像素单位），与 RKNN 输出顺序严格对齐
    # 输出排列: [stride8 的 19200 行] [stride16 的 4800 行] [stride32 的 1200 行]
    # 每个 stride 内部: 逐行遍历网格，每格 3 个 anchor
    # ================================================================
    all_grid_xy = []    # 每个预测对应的网格坐标 (cx, cy)，特征图坐标
    all_anchor_wh = []  # 每个预测对应的 anchor 宽高，输入图像像素单位
    all_stride = []     # 每个预测对应的 stride

    for s in STRIDES:
        g = IMG_SIZE // s                     # 特征图边长: 80, 40, 20
        anchors = np.array(ANCHORS[STRIDES.index(s)], dtype=np.float32)  # [3, 2]

        # 网格中心坐标：cx = 0,1,...,g-1;  cy = 0,1,...,g-1
        cy = np.arange(g, dtype=np.float32)
        cx = np.arange(g, dtype=np.float32)
        # meshgrid → [g, g, 2]，第一维是 y(行)，第二维是 x(列)
        grid_y, grid_x = np.meshgrid(cy, cx, indexing='ij')
        grid = np.stack([grid_x, grid_y], axis=-1)     # [g, g, 2]

        # 展平 → [g*g, 2]，C 序(row-major)：先遍历第 0 行所有列，再第 1 行...
        grid = grid.reshape(-1, 2)                     # [g*g, 2]

        # 每格展开 3 个 anchor：repeat 使 grid 按 (a0,a1,a2) 重复
        # 例如 grid=(0,0) → (0,0)(0,0)(0,0) 对应 3 个 anchor
        grid = np.repeat(grid, 3, axis=0)              # [g*g*3, 2]

        # anchor 在每个网格位置平铺
        anchor_wh = np.tile(anchors, (g * g, 1))       # [g*g*3, 2]

        all_grid_xy.append(grid)
        all_anchor_wh.append(anchor_wh)
        all_stride.append(np.full((g * g * 3, 1), s, dtype=np.float32))

    all_grid_xy   = np.concatenate(all_grid_xy, axis=0)    # [25200, 2]
    all_anchor_wh = np.concatenate(all_anchor_wh, axis=0)  # [25200, 2]
    all_stride    = np.concatenate(all_stride, axis=0)      # [25200, 1]

    # ================================================================
    # 步骤 2：经典 YOLO 解码
    #   bx = (σ(tx) + cx) × stride     其中 cx ∈ [0, g-1] 是特征图网格坐标
    #   bw = aw × exp(tw)               aw 是 anchor 宽(像素单位)
    # ================================================================
    tx = output[:, 0:1]
    ty = output[:, 1:2]
    tw = output[:, 2:3]
    th = output[:, 3:4]

    bx = (sigmoid(tx) + all_grid_xy[:, 0:1]) * all_stride
    by = (sigmoid(ty) + all_grid_xy[:, 1:2]) * all_stride
    bw = np.exp(tw) * all_anchor_wh[:, 0:1]         # anchor_w 像素单位
    bh = np.exp(th) * all_anchor_wh[:, 1:2]         # anchor_h 像素单位

    boxes = np.concatenate([bx, by, bw, bh], axis=1)   # [25200, 4]

    # --- 3. 计算置信度 ---
    obj_conf = sigmoid(output[:, 4:5])                   # [25200, 1]
    cls_conf = sigmoid(output[:, 5:])                    # [25200, 80]
    scores = obj_conf * cls_conf                         # [25200, 80]

    # --- 4. 阈值筛选 ---
    class_ids = np.argmax(scores, axis=1)                # [25200,]
    max_scores = np.max(scores, axis=1)                  # [25200,]
    mask = max_scores > conf_thres

    boxes = boxes[mask]                                  # [N, 4]
    scores = max_scores[mask]                            # [N,]
    class_ids = class_ids[mask]                          # [N,]

    # --- 5. 转为 xyxy ---
    boxes_xyxy = xywh2xyxy(boxes)

    # --- 6. 坐标映射：640×640 → 原图（letterbox 逆变换）---
    h, w = img_shape[:2]
    r = letterbox_ratio
    boxes_xyxy[:, 0] = (boxes_xyxy[:, 0] - pad_left) / r
    boxes_xyxy[:, 1] = (boxes_xyxy[:, 1] - pad_top)  / r
    boxes_xyxy[:, 2] = (boxes_xyxy[:, 2] - pad_left) / r
    boxes_xyxy[:, 3] = (boxes_xyxy[:, 3] - pad_top)  / r

    boxes_xyxy[:, 0] = np.clip(boxes_xyxy[:, 0], 0, w)
    boxes_xyxy[:, 1] = np.clip(boxes_xyxy[:, 1], 0, h)
    boxes_xyxy[:, 2] = np.clip(boxes_xyxy[:, 2], 0, w)
    boxes_xyxy[:, 3] = np.clip(boxes_xyxy[:, 3], 0, h)

    # --- 7. NMS ---
    final_detections = []
    unique_classes = np.unique(class_ids)
    for cls_id in unique_classes:
        cls_mask = class_ids == cls_id
        cls_boxes = boxes_xyxy[cls_mask]
        cls_scores = scores[cls_mask]
        keep = nms(cls_boxes, cls_scores, nms_thres)
        for i in keep:
            final_detections.append([
                int(cls_boxes[i][0]), int(cls_boxes[i][1]),
                int(cls_boxes[i][2]), int(cls_boxes[i][3]),
                float(cls_scores[i]), int(cls_id),
            ])

    return final_detections


def draw_detections(img, detections):
    """在图片上绘制检测框"""
    colors = np.random.randint(0, 255, size=(len(CLASS_NAMES), 3), dtype=np.uint8)
    for det in detections:
        x1, y1, x2, y2, conf, cls_id = det
        color = colors[cls_id].tolist()
        label = f"{CLASS_NAMES[cls_id]} {conf:.2f}"
        cv2.rectangle(img, (x1, y1), (x2, y2), color, 2)
        (tw, th), _ = cv2.getTextSize(label, cv2.FONT_HERSHEY_SIMPLEX, 0.5, 1)
        cv2.rectangle(img, (x1, y1 - th - 4), (x1 + tw, y1), color, -1)
        cv2.putText(img, label, (x1, y1 - 2),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1)
    return img


# ======================== 主程序 ========================
if __name__ == '__main__':
    rknn = RKNN(verbose=True, verbose_file="log.txt")

    # ================================================================
    # 步骤 1：加载模型（两条路径二选一）
    # ================================================================
    #  路径 A (USE_SIMULATOR=True):  在 PC 上模拟推理
    #        流程: load_onnx → build → init_runtime(target=None)
    #        原理: RKNN Toolkit2 在 PC 上软件模拟 NPU 执行，速度慢但不需要硬件
    #
    #  路径 B (USE_SIMULATOR=False): 在 RK3568 开发板上真实推理
    #        流程: load_rknn → init_runtime(target='rk3568')
    #        原理: 直接加载已量化的 .rknn 文件，通过 USB/NTB 下发到 NPU 执行
    # ================================================================
    if USE_SIMULATOR:
        print(f"[INFO] PC 模拟模式: 加载 ONNX → 构建 RKNN")
        rknn.config(
            mean_values=[[0, 0, 0]],
            std_values=[[255, 255, 255]],
            target_platform="rk3568",
            quantized_dtype="asymmetric_quantized-8",
            optimization_level=3,
        )
        ret = rknn.load_onnx(model=ONNX_MODEL_PATH)
        if ret != 0:
            print(f"[ERROR] 加载 ONNX 失败, ret = {ret}")
            exit(1)
        ret = rknn.build(do_quantization=True, dataset=CALIB_DATASET)
        if ret != 0:
            print(f"[ERROR] 构建 RKNN 失败, ret = {ret}")
            exit(1)
        print("[INFO] 初始化模拟器运行时...")
        ret = rknn.init_runtime(target=None)
    else:
        print(f"[INFO] 开发板模式: 加载 RKNN 模型 {RKNN_MODEL_PATH}")
        ret = rknn.load_rknn(path=RKNN_MODEL_PATH)
        if ret != 0:
            print(f"[ERROR] 加载 RKNN 模型失败, ret = {ret}")
            exit(1)
        print("[INFO] 初始化开发板运行时 (target='rk3568')...")
        ret = rknn.init_runtime(target='rk3568')

    if ret != 0:
        print(f"[ERROR] 初始化运行时失败, ret = {ret}")
        exit(1)

    # 3. 读取图片
    img_path = "/home/alientek/software/rknpu2-master/examples/rknn_yolov5_demo/model/bus.jpg"
    print(f"[INFO] 读取图片: {img_path}")
    img = cv2.imread(img_path)
    if img is None:
        print(f"[ERROR] 无法读取图片: {img_path}")
        rknn.release()
        exit(1)

    orig_img = img.copy()

    # 4. 预处理
    #    BGR -> RGB, letterbox resize 到 640x640
    #    不手动做 /255，因为 RKNN 模型内部已配置 mean/std 归一化
    img_rgb = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
    img_input, ratio, (pad_left, pad_top) = letterbox(img_rgb, IMG_SIZE)
    # 加 batch 维度：(640,640,3) → (1,640,640,3) ，RKNN 要求 4 维 NHWC
    img_input = np.expand_dims(img_input, 0)

    print(f"[INFO] 预处理完成, 输入尺寸: {img_input.shape}")
    import pdb
    pdb.set_trace()
    # 5. 推理
    print("[INFO] 开始推理...")
    outputs = rknn.inference(
        inputs=[img_input],
        data_format="nhwc",
    )

    # ----------------- 调试打印：查看原始输出分布 -----------------
    out = np.array(outputs[0])  # expected shape (1, N, 85)
    print('[DEBUG] raw output shape:', out.shape)
    print('[DEBUG] raw out min/max:', float(out.min()), float(out.max()))
    flat = out[0]  # [N,85]
    # 目标置信度和类别概率均使用 sigmoid 激活
    obj = sigmoid(flat[:, 4])                 # [N]
    cls = sigmoid(flat[:, 5:])                # [N,80]
    cls_max = cls.max(axis=1)                 # [N]
    scores = obj * cls_max                    # [N]
    print('[DEBUG] obj max/mean:', float(obj.max()), float(obj.mean()))
    print('[DEBUG] cls_max max/mean:', float(cls_max.max()), float(cls_max.mean()))
    print('[DEBUG] combined score max/mean:', float(scores.max()), float(scores.mean()))
    # top candidates
    topk = 20
    idx = np.argsort(scores)[-topk:][::-1]
    print('[DEBUG] top candidates (idx, score, obj, cls_max):')
    for i in idx:
        print(i, float(scores[i]), float(obj[i]), float(cls_max[i]))
    # 打印前 5 个 top 的原始前 6 个值 (tx,ty,tw,th,obj,first_cls)
    for i in idx[:5]:
        print(f"[DEBUG] raw[{i}] (tx,ty,tw,th,obj,cls0):", flat[i, :6])
    # ----------------- 调试打印结束 -----------------

    # 6. 后处理
    print(f"[INFO] 输出形状: {outputs[0].shape}")
    detections = yolov5_postprocess(
        np.array(outputs[0]),
        orig_img.shape,
        letterbox_ratio=ratio,
        pad_offset=(pad_left, pad_top),
        conf_thres=CONF_THRESHOLD,
        nms_thres=NMS_THRESHOLD,
    )

    print(f"[INFO] 检测到 {len(detections)} 个目标:")
    for det in detections:
        x1, y1, x2, y2, conf, cls_id = det
        print(f"  {CLASS_NAMES[cls_id]:15s}  conf={conf:.3f}  "
              f"box=({x1},{y1},{x2},{y2})")

    # 7. 绘制并保存结果
    result_img = draw_detections(orig_img, detections)
    cv2.imwrite("./result.jpg", result_img)
    print("[INFO] 结果已保存到 ./result.jpg")

    # 8. 释放资源
    rknn.release()
    print("[INFO] 推理完成")