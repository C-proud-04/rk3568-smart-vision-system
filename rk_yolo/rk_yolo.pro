QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp \
    widget.cpp \
    ../rknn_C_API/src/postprocess.cc

HEADERS += \
    widget.h

FORMS += \
    widget.ui

# --------------------------------------------------------------------
#  RKNN / RGA 第三方库路径（根据 rknpu2 SDK 实际位置修改）
# --------------------------------------------------------------------
RKNPU2_PATH = /home/alientek/software/rknpu2-master

!isEmpty(RKNPU2_PATH) {
    # RKNN API
    INCLUDEPATH += $${RKNPU2_PATH}/runtime/RK356X/Linux/librknn_api/include
    LIBS += -L$${RKNPU2_PATH}/runtime/RK356X/Linux/librknn_api/aarch64 -lrknnrt

    # RGA (硬件缩放/颜色转换)
    INCLUDEPATH += $${RKNPU2_PATH}/examples/3rdparty/rga/RK356X/include
    LIBS += -L$${RKNPU2_PATH}/examples/3rdparty/rga/RK356X/lib/Linux/aarch64 -lrga

    # MPP (硬件解码)
    INCLUDEPATH += $${RKNPU2_PATH}/examples/3rdparty/mpp/include
    LIBS += -L$${RKNPU2_PATH}/examples/3rdparty/mpp/Linux/aarch64 -lrockchip_mpp
}

# postprocess 头文件路径
INCLUDEPATH += ../rknn_C_API/include

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
