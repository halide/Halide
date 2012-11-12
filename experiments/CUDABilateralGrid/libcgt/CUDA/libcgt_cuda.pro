TEMPLATE = lib
CONFIG += qt thread debug_and_release staticlib

DESTDIR = "../lib"

INCLUDEPATH += "/usr/local/cuda/include"
INCLUDEPATH += "/usr/local/NVIDIA_GPU_Computing_SDK/C/common/inc"

INCLUDEPATH += $(CUDA_INC_PATH)
INCLUDEPATH += $(NVSDKCOMPUTE_ROOT)/C/common/inc
INCLUDEPATH += "./include"
INCLUDEPATH += "../core/include"

LIBPATH += $(CUDA_LIB_PATH)

CONFIG( debug, debug|release ) {
  TARGET = libcgt_cudad
} else {
  TARGET = libcgt_cuda
  DEFINES += _SECURE_SCL=0
}

QMAKE_CXXFLAGS += -std=c++0x -fpermissive
QMAKE_CXX = g++-4.6
DEFINES += _CRT_SECURE_NO_WARNINGS

HEADERS += include/*.h
SOURCES += src/*.cpp
