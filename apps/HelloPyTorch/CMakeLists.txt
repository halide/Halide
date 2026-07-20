cmake_minimum_required(VERSION 3.28)
project(HelloPyTorch)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Find Halide
find_package(Halide REQUIRED)

# This app wraps Halide-generated ops in a PyTorch C++ extension, so it needs
# Python and PyTorch. Skip cleanly if either is missing, the same way apps/onnx
# gates on its optional dependencies.
find_package(Python 3 COMPONENTS Interpreter Development.Module)
if (NOT Python_FOUND)
    message(WARNING "HelloPyTorch: could NOT find Python 3; skipping")
    return()
endif ()

# CUDA ops can only be exercised on a machine with a CUDA-enabled PyTorch and a
# GPU, which CPU-only CI does not have. Keep them opt-in.
option(HELLO_PYTORCH_CUDA "Build the CUDA variants of the HelloPyTorch ops" OFF)
if (HELLO_PYTORCH_CUDA)
    set(torch_cuda_arg True)
else ()
    set(torch_cuda_arg False)
endif ()

# Pull the extension's build settings straight from torch, exactly as
# torch.utils.cpp_extension (and hence setup.py) does. We deliberately avoid
# find_package(Torch): its Caffe2 config force-enables the CUDA language and
# demands a standalone CUDA toolkit matching the wheel's version, even though
# this wrapper has no CUDA sources of its own -- which makes it fail to configure
# against a CUDA-enabled wheel unless an exactly-matching toolkit is installed.
execute_process(
    COMMAND
        "${Python_EXECUTABLE}"
        -c "import torch.utils.cpp_extension as C; print(';'.join(C.include_paths(${torch_cuda_arg})))"
    OUTPUT_VARIABLE Torch_INCLUDE_DIRS
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE Torch_probe_result
    ERROR_VARIABLE Torch_probe_error
)
if (NOT Torch_probe_result EQUAL 0)
    message(WARNING "HelloPyTorch: PyTorch is not usable; skipping\n${Torch_probe_error}")
    return()
endif ()
execute_process(
    COMMAND
        "${Python_EXECUTABLE}"
        -c "import torch.utils.cpp_extension as C; print(';'.join(C.library_paths(${torch_cuda_arg})))"
    OUTPUT_VARIABLE Torch_LIBRARY_DIRS
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
execute_process(
    COMMAND
        "${Python_EXECUTABLE}" -c "import torch; print(1 if torch._C._GLIBCXX_USE_CXX11_ABI else 0)"
    OUTPUT_VARIABLE Torch_CXX11_ABI
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
message(STATUS "HelloPyTorch: using Torch headers from ${Torch_INCLUDE_DIRS}")

# Resolve the torch shared libraries the extension links against (the set
# CppExtension/CUDAExtension link by default).
set(torch_lib_names c10 torch torch_cpu torch_python)
if (HELLO_PYTORCH_CUDA)
    list(APPEND torch_lib_names torch_cuda c10_cuda)
    find_package(CUDAToolkit REQUIRED)
endif ()
set(torch_libs "")
foreach (name IN LISTS torch_lib_names)
    find_library(Torch_${name}_LIBRARY NAMES ${name} PATHS ${Torch_LIBRARY_DIRS} REQUIRED)
    list(APPEND torch_libs "${Torch_${name}_LIBRARY}")
endforeach ()

##
# Halide generator and ops
##

add_halide_generator(add.generator SOURCES src/add_generator.cpp)

# Type-parameter sets, mirroring the Makefile.
set(add_types_f32 input_a.type=float32 input_b.type=float32 output.type=float32)
set(add_types_f64 input_a.type=float64 input_b.type=float64 output.type=float64)
set(add_grad_types_f32
    input_a.type=float32 input_b.type=float32 d_input_a.type=float32 d_input_b.type=float32
    d_output.type=float32
)
set(add_grad_types_f64
    input_a.type=float64 input_b.type=float64 d_input_a.type=float64 d_input_b.type=float64
    d_output.type=float64
)

# List of ops exposed by the extension; populated as libraries are added.
set(op_libs "")

# CPU ops -------------------------------------------------------------------

# Forward op (add) and hand-written gradient (add_grad).
add_halide_library(
    add_float32
    FROM add.generator
    GENERATOR add
    PARAMS ${add_types_f32}
    PYTORCH_WRAPPER add_float32.pytorch.h
)
add_halide_library(
    add_float64
    FROM add.generator
    GENERATOR add
    PARAMS ${add_types_f64}
    PYTORCH_WRAPPER add_float64.pytorch.h
)
add_halide_library(
    add_grad_float32
    FROM add.generator
    GENERATOR add_grad
    PARAMS ${add_grad_types_f32}
    PYTORCH_WRAPPER add_grad_float32.pytorch.h
)
add_halide_library(
    add_grad_float64
    FROM add.generator
    GENERATOR add_grad
    PARAMS ${add_grad_types_f64}
    PYTORCH_WRAPPER add_grad_float64.pytorch.h
)

# Halide-autodifferentiated gradient (add with -d 1 + Li2018 autoscheduler).
add_halide_library(
    add_halidegrad_float32
    FROM add.generator
    GENERATOR add
    GRADIENT_DESCENT
    AUTOSCHEDULER Halide::Li2018
    PARAMS ${add_types_f32}
    PYTORCH_WRAPPER add_halidegrad_float32.pytorch.h
)
add_halide_library(
    add_halidegrad_float64
    FROM add.generator
    GENERATOR add
    GRADIENT_DESCENT
    AUTOSCHEDULER Halide::Li2018
    PARAMS ${add_types_f64}
    PYTORCH_WRAPPER add_halidegrad_float64.pytorch.h
)

list(APPEND op_libs
    add_float32 add_float64 add_grad_float32 add_grad_float64 add_halidegrad_float32
    add_halidegrad_float64
)

# CUDA ops ------------------------------------------------------------------

if (HELLO_PYTORCH_CUDA)
    # The PyTorch CUDA wrapper requires the user_context feature so Halide's and
    # PyTorch's GPU memory managers can talk to each other.
    set(cuda_features cuda cuda_capability_61 user_context)

    add_halide_library(
        add_cuda_float32
        FROM add.generator
        GENERATOR add
        FEATURES ${cuda_features}
        PARAMS ${add_types_f32}
        PYTORCH_WRAPPER add_cuda_float32.pytorch.h
    )
    add_halide_library(
        add_cuda_float64
        FROM add.generator
        GENERATOR add
        FEATURES ${cuda_features}
        PARAMS ${add_types_f64}
        PYTORCH_WRAPPER add_cuda_float64.pytorch.h
    )
    add_halide_library(
        add_grad_cuda_float32
        FROM add.generator
        GENERATOR add_grad
        FEATURES ${cuda_features}
        PARAMS ${add_grad_types_f32}
        PYTORCH_WRAPPER add_grad_cuda_float32.pytorch.h
    )
    add_halide_library(
        add_grad_cuda_float64
        FROM add.generator
        GENERATOR add_grad
        FEATURES ${cuda_features}
        PARAMS ${add_grad_types_f64}
        PYTORCH_WRAPPER add_grad_cuda_float64.pytorch.h
    )
    add_halide_library(
        add_halidegrad_cuda_float32
        FROM add.generator
        GENERATOR add
        GRADIENT_DESCENT
        AUTOSCHEDULER Halide::Li2018
        FEATURES ${cuda_features}
        PARAMS ${add_types_f32}
        PYTORCH_WRAPPER add_halidegrad_cuda_float32.pytorch.h
    )
    add_halide_library(
        add_halidegrad_cuda_float64
        FROM add.generator
        GENERATOR add
        GRADIENT_DESCENT
        AUTOSCHEDULER Halide::Li2018
        FEATURES ${cuda_features}
        PARAMS ${add_types_f64}
        PYTORCH_WRAPPER add_halidegrad_cuda_float64.pytorch.h
    )

    list(APPEND op_libs
        add_cuda_float32 add_cuda_float64 add_grad_cuda_float32 add_grad_cuda_float64
        add_halidegrad_cuda_float32 add_halidegrad_cuda_float64
    )
endif ()

##
# Synthesize the pybind wrapper (equivalent to setup.py's generate_pybind_wrapper)
##

set(wrapper "#include \"torch/extension.h\"\n")
if (HELLO_PYTORCH_CUDA)
    string(APPEND wrapper "#include \"HalidePyTorchCudaHelpers.h\"\n")
endif ()
string(APPEND wrapper "#include \"HalidePyTorchHelpers.h\"\n")
foreach (op IN LISTS op_libs)
    string(APPEND wrapper "#include \"${op}.pytorch.h\"\n")
endforeach ()
string(APPEND wrapper "\nPYBIND11_MODULE(TORCH_EXTENSION_NAME, m) {\n")
foreach (op IN LISTS op_libs)
    string(APPEND wrapper
        "  m.def(\"${op}\", &${op}_th_, \"PyTorch wrapper of the Halide pipeline ${op}\");\n"
    )
endforeach ()
string(APPEND wrapper "}\n")

set(wrapper_cpp "${CMAKE_CURRENT_BINARY_DIR}/pybind_wrapper.cpp")
file(GENERATE OUTPUT "${wrapper_cpp}" CONTENT "${wrapper}")

##
# The Python extension module
##

Python_add_library(halide_ops MODULE WITH_SOABI "${wrapper_cpp}")
set_target_properties(halide_ops PROPERTIES OUTPUT_NAME halide_ops)
target_compile_definitions(
    halide_ops
    PRIVATE TORCH_EXTENSION_NAME=halide_ops _GLIBCXX_USE_CXX11_ABI=${Torch_CXX11_ABI}
)
target_include_directories(halide_ops PRIVATE ${Torch_INCLUDE_DIRS})
target_link_libraries(halide_ops PRIVATE ${op_libs} ${torch_libs})
# Let the freshly built extension find the torch shared libraries at load time.
set_target_properties(halide_ops PROPERTIES BUILD_RPATH "${Torch_LIBRARY_DIRS}")
if (HELLO_PYTORCH_CUDA)
    target_link_libraries(halide_ops PRIVATE CUDA::cuda_driver)
endif ()

##
# Test: run test.py against the freshly built extension.
##
# `python -m unittest` puts the working directory on sys.path, so running from
# the source dir makes `modules` and `test` importable; the built extension is
# picked up via PYTHONPATH.
add_test(
    NAME hello_pytorch
    COMMAND ${Python_EXECUTABLE} -m unittest test
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
set_tests_properties(
    hello_pytorch
    PROPERTIES
    LABELS HelloPyTorch
    ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:halide_ops>"
    PASS_REGULAR_EXPRESSION "OK"
    SKIP_REGULAR_EXPRESSION "\\[SKIP\\]"
    TIMEOUT 300
)
