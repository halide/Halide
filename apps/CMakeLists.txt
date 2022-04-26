##
# Test apps from the perspective of a consuming project.
##

cmake_minimum_required(VERSION 3.16)
project(Halide_apps)

if (WIN32)
    option(ENABLE_APPS_HANNK "Build apps/hannk" OFF)
else ()
    option(ENABLE_APPS_HANNK "Build apps/hannk" ON)
endif ()

enable_testing()

# add_subdirectory(HelloAndroid)  # TODO(#5374): missing CMake build
# add_subdirectory(HelloAndroidCamera2)  # TODO(#5374): missing CMake build
# add_subdirectory(HelloPyTorch)  # TODO(#5374): missing CMake build
# add_subdirectory(HelloWasm)  # TODO(#5374): missing CMake build
# add_subdirectory(HelloiOS)  # TODO(#5374): missing CMake build
# add_subdirectory(auto_viz)  # TODO(#5374): missing CMake build
add_subdirectory(bgu)
add_subdirectory(bilateral_grid)
add_subdirectory(blur)
add_subdirectory(c_backend)
add_subdirectory(camera_pipe)
add_subdirectory(conv_layer)
add_subdirectory(cuda_mat_mul)
add_subdirectory(depthwise_separable_conv)
add_subdirectory(fft)
if (ENABLE_APPS_HANNK)
    add_subdirectory(hannk)
endif ()
add_subdirectory(harris)
# add_subdirectory(hexagon_benchmarks)  # TODO(#5374): missing CMake build
# add_subdirectory(hexagon_dma)  # TODO(#5374): missing CMake build
add_subdirectory(hist)
add_subdirectory(iir_blur)
add_subdirectory(interpolate)
add_subdirectory(lens_blur)
add_subdirectory(linear_algebra)
# add_subdirectory(linear_blur)  # TODO(#5374): missing CMake build
add_subdirectory(local_laplacian)
add_subdirectory(max_filter)
add_subdirectory(nl_means)
# add_subdirectory(nn_ops)  # TODO(#5374): missing CMake build
# add_subdirectory(onnx)  # TODO(#5374): missing CMake build
# add_subdirectory(openglcompute)  # TODO(#5374): missing CMake build
add_subdirectory(resize)
# add_subdirectory(resnet_50)  # TODO(#5374): missing CMake build
# add_subdirectory(simd_op_check)  # TODO(#5374): missing CMake build
add_subdirectory(stencil_chain)
add_subdirectory(unsharp)
add_subdirectory(wavelet)
