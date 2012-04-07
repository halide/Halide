INCLUDES=-I/usr/local/NVIDIA_GPU_Computing_SDK/C/common/inc -I/usr/local/cuda/include -I./libcgt/core/include -I./libcgt/CUDA/include
INCLUDES += -I/usr/include/qt4/QtCore -I/usr/include/qt4/QtGui -I/usr/include/qt4
LINKS=-L./libcgt/lib -llibcgt_core -llibcgt_cuda -lQtGui -lQtCore -L/usr/local/NVIDIA_GPU_Computing_SDK/C/lib -lcutil_x86_64
NVCC=/usr/local/cuda/bin/nvcc

# GCC 4.6: not supported by nvcc
# --compiler-bindir=/usr/local/Cellar/gcc/4.6/bin
grid: BilateralFilter.cu BilateralFilter.h main2.cpp
	${NVCC} -o grid -arch sm_20 ${INCLUDES} ${LINKS} BilateralFilter.cu main2.cpp

test: grid
	LD_LIBRARY_PATH=/usr/local/cuda/lib64 ./grid
