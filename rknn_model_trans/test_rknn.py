import os
import urllib
import traceback
import time
import sys
import numpy as np
import cv2
from rknn.api import RKNN

ONNX_MODEL = '/home/alientek/czh/yolov5s.onnx'
RKNN_MODEL = 'yolov5s.rknn'
IMG_PATH = '/home/alientek/software/rknpu2-master/examples/rknn_yolov5_demo/model/bus.jpg'
DATASET = '/home/alientek/czh/dataset/calibration.txt'

QUANTIZE_ON = True

OBJ_THRESH = 0.25
NMS_THRESH = 0.45
IMG_SIZE = 640

CLASSES = ("person", "bicycle", "car", "motorbike ", "aeroplane ", "bus ", "train", "truck ", "boat", "traffic light",
           "fire hydrant", "stop sign ", "parking meter", "bench", "bird", "cat", "dog ", "horse ", "sheep", "cow", "elephant",
           "bear", "zebra ", "giraffe", "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee", "skis", "snowboard", "sports ball", "kite",
           "baseball bat", "baseball glove", "skateboard", "surfboard", "tennis racket", "bottle", "wine glass", "cup", "fork", "knife ",
           "spoon", "bowl", "banana", "apple", "sandwich", "orange", "broccoli", "carrot", "hot dog", "pizza ", "donut", "cake", "chair", "sofa",
           "pottedplant", "bed", "diningtable", "toilet ", "tvmonitor", "laptop	", "mouse	", "remote ", "keyboard ", "cell phone", "microwave ",
           "oven ", "toaster", "sink", "refrigerator ", "book", "clock", "vase", "scissors ", "teddy bear ", "hair drier", "toothbrush ")


def sigmoid(x):
    return 1 / (1 + np.exp(-x))


def xywh2xyxy(x):
    # Convert [x, y, w, h] to [x1, y1, x2, y2]
    y = np.copy(x)
    y[:, 0] = x[:, 0] - x[:, 2] / 2  # top left x
    y[:, 1] = x[:, 1] - x[:, 3] / 2  # top left y
    y[:, 2] = x[:, 0] + x[:, 2] / 2  # bottom right x
    y[:, 3] = x[:, 1] + x[:, 3] / 2  # bottom right y
    return y


# input.shape= (H, W, 3, 85)
def process(input, mask, anchors):

    # 获取检测头的对应检测框
    anchors = [anchors[i] for i in mask]
    # grid_h = H
    # grid_w = W
    grid_h, grid_w = map(int, input.shape[0:2])

    # input[..., 4]：前面的维度不管，取最后一个维度85的第4个通道，物体置信度
    box_confidence = sigmoid(input[..., 4])# (H, W, 3)
    box_confidence = np.expand_dims(box_confidence, axis=-1)# (H, W, 3, 1)

    # 取最后一个维度所有分类的概率
    box_class_probs = sigmoid(input[..., 5:])# (H, W, 3, 80)
    
    # 取tx, ty
    # (H, W, 3, 2)
    box_xy = sigmoid(input[..., :2])*2 - 0.5

    # 构造(H, W, 3, 2)的网格坐标
    col = np.tile(np.arange(0, grid_w), grid_w).reshape(-1, grid_w)
    row = np.tile(np.arange(0, grid_h).reshape(-1, 1), grid_h)
    col = col.reshape(grid_h, grid_w, 1, 1).repeat(3, axis=-2)
    row = row.reshape(grid_h, grid_w, 1, 1).repeat(3, axis=-2)
    # 拼接
    grid = np.concatenate((col, row), axis=-1)
    box_xy += grid # 偏移 + 网格坐标
    box_xy *= int(IMG_SIZE/grid_h)# 缩放到原像素的坐标

    # 取tx, ty（检测框的缩放）
    # (H, W, 3, 2)
    box_wh = pow(sigmoid(input[..., 2:4])*2, 2)
    box_wh = box_wh * anchors# 得到检测框的大小

    # 对应网格坐标可能的检测框大小
    # (H, W, 3, 4)
    box = np.concatenate((box_xy, box_wh), axis=-1)

    return box, box_confidence, box_class_probs


def filter_boxes(boxes, box_confidences, box_class_probs):
    """Filter boxes with box threshold. It's a bit different with origin yolov5 post process!

    # Arguments
        boxes: ndarray, boxes of objects.
        box_confidences: ndarray, confidences of objects.
        box_class_probs: ndarray, class_probs of objects.

    # Returns
        boxes: ndarray, filtered boxes.
        classes: ndarray, classes for boxes.
        scores: ndarray, scores for boxes.
    """
    # 重新调整矩阵，网格——>像素坐标 (H*W*3, 4)
    boxes = boxes.reshape(-1, 4)
    # 网格——>物体置信度 (H*W*3, 1)
    box_confidences = box_confidences.reshape(-1)
    # 网格——>分类概率 (H*W*3, 80)
    box_class_probs = box_class_probs.reshape(-1, box_class_probs.shape[-1])

    # 返回True，False的矩阵，筛选出置信度概率大于阈值的框 (存在物体的框, 1)
    _box_pos = np.where(box_confidences >= OBJ_THRESH)
    # 挑选出框 (存在物体的框, 4)
    boxes = boxes[_box_pos]
    # 挑选出对应概率大于阈值的置信度 (存在物体的框, 1)
    box_confidences = box_confidences[_box_pos]
    # 挑选出存在物体概率大于阈值的框的80个分类概率 (存在物体的框, 80)
    box_class_probs = box_class_probs[_box_pos]

    # 挑选出存在物体的框中分类概率最大的框的概率 (存在物体的框, 1)
    class_max_score = np.max(box_class_probs, axis=-1)
    # 对应概率的分类索引 (存在物体的框, 1)
    classes = np.argmax(box_class_probs, axis=-1)
    # 再从分类概率里面挑选出大于阈值的框 (存在物体概率大于阈值的框, 1)
    _class_pos = np.where(class_max_score >= OBJ_THRESH)

    # 再精细筛选一遍
    boxes = boxes[_class_pos]
    classes = classes[_class_pos]
    scores = (class_max_score* box_confidences)[_class_pos]

    return boxes, classes, scores

# 保留最大概率的框，筛掉和当前概率框重叠度过高的框
def nms_boxes(boxes, scores):
    """Suppress non-maximal boxes.

    # Arguments
        boxes: ndarray, boxes of objects.
        scores: ndarray, scores of objects.

    # Returns
        keep: ndarray, index of effective boxes.
    """
    # 所有框的左上角坐标
    x = boxes[:, 0]
    y = boxes[:, 1]
    # 所有框的框的宽高
    w = boxes[:, 2] - boxes[:, 0]
    h = boxes[:, 3] - boxes[:, 1]

    areas = w * h
    # 同一类别框的分数从大到小排列的索引
    order = scores.argsort()[::-1]

    keep = []
    while order.size > 0:
        i = order[0]
        keep.append(i)

        # x[order[1:]]：去掉最高分框之后，剩余框的左上角坐标
        xx1 = np.maximum(x[i], x[order[1:]])
        yy1 = np.maximum(y[i], y[order[1:]])
        # 交集部分的右下角坐标
        xx2 = np.minimum(x[i] + w[i], x[order[1:]] + w[order[1:]])
        yy2 = np.minimum(y[i] + h[i], y[order[1:]] + h[order[1:]])

        w1 = np.maximum(0.0, xx2 - xx1 + 0.00001)
        h1 = np.maximum(0.0, yy2 - yy1 + 0.00001)
        # 交集面积
        inter = w1 * h1
        # IoU = 交集面积 / (当前框面积 + 其他框面积 - 交集面积)
        ovr = inter / (areas[i] + areas[order[1:]] - inter)
        # 选出IoU小于阈值的框的索引
        inds = np.where(ovr <= NMS_THRESH)[0]
        # ovr中是筛掉最高分框的，所以对应原来的order应该+1
        order = order[inds + 1]
    keep = np.array(keep)
    return keep


def yolov5_post_process(input_data):
    masks = [[0, 1, 2], [3, 4, 5], [6, 7, 8]]
    anchors = [[10, 13], [16, 30], [33, 23], [30, 61], [62, 45],
               [59, 119], [116, 90], [156, 198], [373, 326]]

    boxes, classes, scores = [], [], []
    for input, mask in zip(input_data, masks):
        # b: (H, W, 3, 4), c: (H, W, 3, 1), s: (H, W, 3, 80)
        # 检测框；物体置信度；分类概率
        b, c, s = process(input, mask, anchors)
        # 坐标, 网格对应类别的索引, 对应分数
        b, c, s = filter_boxes(b, c, s)
        boxes.append(b)
        classes.append(c)
        scores.append(s)
    # 经过上一个for之后,boxes等是list，list中的每个元素都是(对应网格候选框, 4)

    # 下面操作将list整合成(所有网格候选框, 4)
    boxes = np.concatenate(boxes)
    # 将网格中心和宽高缩放比转换成对应框的四角坐标(x1, y1, x2, y2)
    boxes = xywh2xyxy(boxes)
    classes = np.concatenate(classes)
    scores = np.concatenate(scores)

    # 以类别区分的list
    nboxes, nclasses, nscores = [], [], []
    # set(classed)：去重，得到图中识别到的类别
    for c in set(classes):
        # 选出对应类别的框的索引
        inds = np.where(classes == c)
        # 坐标
        b = boxes[inds]
        c = classes[inds]
        # 分数
        s = scores[inds]
        # 根据类别筛选掉重叠度高的框
        keep = nms_boxes(b, s)

        nboxes.append(b[keep])
        nclasses.append(c[keep])
        nscores.append(s[keep])

    if not nclasses and not nscores:
        return None, None, None

    boxes = np.concatenate(nboxes)
    classes = np.concatenate(nclasses)
    scores = np.concatenate(nscores)

    return boxes, classes, scores


def draw(image, boxes, scores, classes):
    """Draw the boxes on the image.

    # Argument:
        image: original image.
        boxes: ndarray, boxes of objects.
        classes: ndarray, classes of objects.
        scores: ndarray, scores of objects.
        all_classes: all classes name.
    """
    for box, score, cl in zip(boxes, scores, classes):
        top, left, right, bottom = box
        print('class: {}, score: {}'.format(CLASSES[cl], score))
        print('box coordinate left,top,right,down: [{}, {}, {}, {}]'.format(top, left, right, bottom))
        top = int(top)
        left = int(left)
        right = int(right)
        bottom = int(bottom)

        cv2.rectangle(image, (top, left), (right, bottom), (255, 0, 0), 2)
        cv2.putText(image, '{0} {1:.2f}'.format(CLASSES[cl], score),
                    (top, left - 6),
                    cv2.FONT_HERSHEY_SIMPLEX,
                    0.6, (0, 0, 255), 2)


def letterbox(im, new_shape=(640, 640), color=(0, 0, 0)):
    # Resize and pad image while meeting stride-multiple constraints
    shape = im.shape[:2]  # current shape [height, width]
    if isinstance(new_shape, int):
        new_shape = (new_shape, new_shape)

    # Scale ratio (new / old)
    r = min(new_shape[0] / shape[0], new_shape[1] / shape[1])

    # Compute padding
    ratio = r, r  # width, height ratios
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


if __name__ == '__main__':

    # Create RKNN object
    rknn = RKNN(verbose=True)

    # pre-process config
    print('--> Config model')
    rknn.config(mean_values=[[0, 0, 0]], std_values=[[255, 255, 255]],target_platform="rk3568")
    print('done')

    rknn.load_rknn(path=RKNN_MODEL)

    # Init runtime environment
    print('--> Init runtime environment')
    ret = rknn.init_runtime('rk3568')
    # ret = rknn.init_runtime('rk3566')
    if ret != 0:
        print('Init runtime environment failed!')
        exit(ret)
    print('done')

    # Set inputs
    img = cv2.imread(IMG_PATH)
    # img, ratio, (dw, dh) = letterbox(img, new_shape=(IMG_SIZE, IMG_SIZE))
    img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
    img = cv2.resize(img, (IMG_SIZE, IMG_SIZE))

    # Inference
    print('--> Running model')
    outputs = rknn.inference(inputs=[img])
    np.save('./onnx_yolov5_0.npy', outputs[0])
    np.save('./onnx_yolov5_1.npy', outputs[1])
    np.save('./onnx_yolov5_2.npy', outputs[2])
    print('done')

    # post process
    input0_data = outputs[0]# (1, 255, 80, 80)
    input1_data = outputs[1]# (1, 255, 40, 40)
    input2_data = outputs[2]# (1, 255, 20, 20)

    # (3, 85, H, W)
    input0_data = input0_data.reshape([3, -1]+list(input0_data.shape[-2:]))# (3, 85, 80, 80)
    input1_data = input1_data.reshape([3, -1]+list(input1_data.shape[-2:]))# (3, 85, 40, 40)
    input2_data = input2_data.reshape([3, -1]+list(input2_data.shape[-2:]))# (3, 85, 20, 20)

    # (H, W, 3, 85)
    input_data = list()
    input_data.append(np.transpose(input0_data, (2, 3, 0, 1)))#(80, 80, 3, 85)
    input_data.append(np.transpose(input1_data, (2, 3, 0, 1)))#(40, 40, 3, 85)
    input_data.append(np.transpose(input2_data, (2, 3, 0, 1)))#(20, 20, 3, 85)

    boxes, classes, scores = yolov5_post_process(input_data)

    img_1 = cv2.cvtColor(img, cv2.COLOR_RGB2BGR)
    if boxes is not None:
        draw(img_1, boxes, scores, classes)
        cv2.imwrite('result.jpg', img_1)

    rknn.release()
