##
# Test apps from the perspective of a consuming project.
##

cmake_minimum_required(VERSION 3.22)
project(Halide_apps)

enable_testing()

if (WIN32)
    option(ENABLE_APPS_HANNK "Build apps/hannk" OFF)
else ()
    option(ENABLE_APPS_HANNK "Build apps/hannk" ON)
endif ()

function(add_app app_name)
    string(TOUPPER "ENABLE_APPS_${app_name}" opt)
    option(${opt} "Build apps/${app_name}" ON)
    if (${opt})
        add_subdirectory(${app_name})
    endif ()
endfunction()

# TODO: most of the apps need to be smartened to be crosscompilable under wasm.
message(STATUS "Halide_TARGET ${Halide_TARGET}")
if (Halide_TARGET MATCHES "wasm")
    message(WARNING "Skipping apps when building under wasm")
    return()
endif()

# add_app(HelloAndroid)  # TODO(#5374): missing CMake build
# add_app(HelloAndroidCamera2)  # TODO(#5374): missing CMake build
# add_app(HelloPyTorch)  # TODO(#5374): missing CMake build
# add_app(HelloiOS)  # TODO(#5374): missing CMake build
# add_app(auto_viz)  # TODO(#5374): missing CMake build
add_app(bgu)
add_app(bilateral_grid)
add_app(blur)
add_app(c_backend)
add_app(camera_pipe)
add_app(conv_layer)
add_app(cuda_mat_mul)
add_app(depthwise_separable_conv)
add_app(fft)
add_app(hannk)
add_app(harris)
# add_app(hexagon_benchmarks)  # TODO(#5374): missing CMake build
# add_app(hexagon_dma)  # TODO(#5374): missing CMake build
add_app(hist)
add_app(iir_blur)
add_app(interpolate)
add_app(lens_blur)
add_app(linear_algebra)
# add_app(linear_blur)  # TODO(#5374): missing CMake build
add_app(local_laplacian)
add_app(max_filter)
add_app(nl_means)
# add_app(nn_ops)  # TODO(#5374): missing CMake build
# add_app(onnx)  # TODO(#5374): missing CMake build
# add_app(openglcompute)  # TODO(#5374): missing CMake build
add_app(resize)
# add_app(resnet_50)  # TODO(#5374): missing CMake build
# add_app(simd_op_check)  # TODO(#5374): missing CMake build
add_app(stencil_chain)
add_app(unsharp)
add_app(wavelet)
add_app(HelloBaremetal)
