#!/bin/bash
rm -f results_sioutas.txt

touch results_sioutas.txt
export HL_GPU_DEVICE=0
export HL_PERMIT_FAILED_UNROLL=1
export CXXFLAGS=
export CXXFLAGS+=-Dcuda_alloc
export HL_GPU_L2_COST=200
export HL_GPU_SHARED_COST=1
export HL_GPU_GLOBAL_COST=1
export HL_CUDA_JIT_MAX_REGISTERS=256
export HL_MACHINE_PARAMS=80,16777216,4
export NO_GRADIENT_AUTO_SCHEDULE=1
export HL_TARGET=host-cuda-cuda_capability_61

make -C sioutas2020

#echo "BGU"
#echo "bgu:" >> results_sioutas.txt
#cd bgu
#make clean
#make  test | tail -5  >> ../results_sioutas.txt
#cd ..

#echo "DEPTHWISE"
#echo "depthwise_separable_conv:" >> results_sioutas.txt
#cd depthwise_separable_conv
#make clean
#make  test | tail -5  >> ../results_sioutas.txt
#cd ..

#echo "BILATERAL"
#echo "bilateral_grid:" >> results_sioutas.txt
#cd bilateral_grid
#make clean
#make  test
#| tail -5  >> ../results_sioutas.txt
#cd ..

#echo "AHD_DEMOSAIC"
#echo "ahd_demosaic:" >> results_sioutas.txt
#cd ahd_demosaic
#make clean
#make  test
#| tail -5  >> ../results_sioutas.txt
#cd ..

#echo "BASIC_DEMOSAIC"
#echo "basic_demosaic:" >> results_sioutas.txt
#cd basic_demosaic
#make clean
#make  test
#| tail -5  >> ../results_sioutas.txt
#cd ..

echo "MULTIRES_DEMOSAIC"
echo "multires_demosaic:" >> results_sioutas.txt
cd multires_demosaic
make clean
make  test
#| tail -5  >> ../results_sioutas.txt
cd ..

#echo "CAMERA"
#echo "camera_pipe:" >> results_sioutas.txt
#cd camera_pipe
#make clean
#make test | tail -5  >> ../results_sioutas.txt
#cd ..

#echo "HARRIS"
#echo "harris:" >> results_sioutas.txt
#cd harris
#make clean
#make test | tail -5  >> ../results_sioutas.txt
#cd ..

#echo "hist"
#echo "hist:" >> results_sioutas.txt
#cd hist
#make clean
#make test
##| tail -5  >> ../results_sioutas.txt
#cd ..

#echo "iir"
#echo "IIR:" >> results_sioutas.txt
#cd iir_blur
#make clean
#make test | tail -5  >> ../results_sioutas.txt
#cd ..

#echo "INTERPOLATE"
#echo "interpolate:" >> results_sioutas.txt
#cd interpolate
#make clean
#make test | tail -5  >> ../results_sioutas.txt
#cd ..

#echo "LAPLACIAN"
#echo "local_laplacian:" >> results_sioutas.txt
#cd local_laplacian
#make clean
#make test | tail -5  >> ../results_sioutas.txt
#cd ..

#echo "MAXFILTER"
#echo "max_filter:" >> results_sioutas.txt
#cd max_filter
#make clean
#make test | tail -5  >> ../results_sioutas.txt
#cd ..

#echo "UNSHARP"
#echo "unsharp:" >> results_sioutas.txt
#cd unsharp
#make clean
#make test | tail -5  >> ../results_sioutas.txt
#cd ..

#echo "NLMEANS"
#echo "nlmeans:" >> results_sioutas.txt
#cd nl_means
#make clean
#make test | tail -5  >> ../results_sioutas.txt
#cd ..

#echo "stencil"
#echo "stencil:" >> results_sioutas.txt
#cd stencil_chain
#make clean
#make test | tail -5  >> ../results_sioutas.txt
#cd ..

#echo "matmul"
#echo "matmul:" >> results_sioutas.txt
#cd cuda_mat_mul
#make clean
#make test | tail -5 >> ../results_sioutas.txt
#cd ..

#echo "CONV"
#echo "conv_layer:" >> results_sioutas.txt
#cd conv_layer
#make clean
#make test | tail -4  >> ../results_sioutas.txt
#cd ..

#echo "LENSBLUR"
#echo "lens_blur:" >> results_sioutas.txt
#cd lens_blur
#make clean
#make test | tail -4 >> ../results_sioutas.txt
#cd ..
#echo "VDSR"
#echo "VDSR:" >> results_sioutas.txt
#cd VDSR
#make clean
#make HL_TARGET=host-cuda-cuda_capability_35 test #| tail -2  >> ../results_sioutas.txt
#cd ..

#echo "resnet50"
#echo "resnet50:" >> results_sioutas.txt
#cd resnet_50
#make clean
#make HL_TARGET=host-cuda-cuda_capability_35 test #| tail -2  >> ../results_sioutas.txt
#cd ..
