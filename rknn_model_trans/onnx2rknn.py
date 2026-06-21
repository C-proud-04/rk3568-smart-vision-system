from rknn.api import RKNN

rknn = RKNN()

# 配置：目标平台RK3568 int 8量化
rknn.config(
    mean_values=[[0, 0, 0]],       # yolov5不做 mean/std归一化
    std_values=[[255, 255, 255]],  # ֻ只做 /255 归一化
    target_platform="rk3568",
    quantized_dtype="asymmetric_quantized-8",  # int8 量化
    optimization_level=3,
)

# 加载ONNX
ret = rknn.load_onnx(model="/home/alientek/czh/yolov5n.onnx")

# 构建模型（量化）
ret = rknn.build(
    do_quantization=True,          # 开启int 8 量化
    dataset="/home/alientek/czh/dateset/calibration.txt",     # 校准数据集
)

# 导出 RKNN
rknn.export_rknn("/home/alientek/czh/yolov5n.rknn")