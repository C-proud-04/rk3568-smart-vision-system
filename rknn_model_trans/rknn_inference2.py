from rknn.api import RKNN
import cv2
import numpy as np

IMG_SIZE = 640  

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


if __name__ == '__main__':
    rknn = RKNN(verbose=True, verbose_file="log.txt")

    rknn.config(
        mean_values=[[0, 0, 0]],
        std_values=[[255, 255, 255]],
        target_platform="rk3568",
    )

    # 加载 ONNX 模型（替代 load_pytorch，避免 PyTorch 版本不兼容）
    rknn.load_onnx(model="/home/alientek/czh/yolov5s.onnx")

    # 调用build接口构建RKNN模型
    rknn.build(
        do_quantization=True,
        dataset="/home/alientek/czh/dateset/calibration.txt",
    )

    # 调用init_runtime接口初始化运行时环境
    rknn.init_runtime()  # None=PC模拟

    # 使用opencv获取要推理的图片数据
    img = cv2.imread(
        filename="/home/alientek/software/rknpu2-master/examples/rknn_yolov5_demo/model/bus.jpg",
        flags=None
    )
    img_rgb = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
    img_input, ratio, (pad_left, pad_top) = letterbox(img_rgb, IMG_SIZE)
    img_input = np.expand_dims(img_input, 0)

    # 调用inference接口进行推理测试
    outputs = rknn.inference(
        inputs=[img_input],
        data_format="nhwc",
    )

    print(f"outputs[0].shape = {outputs[0].shape}")
    import pdb
    pdb.set_trace()

    # 调用结束就release
    rknn.release()