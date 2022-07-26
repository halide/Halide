##
# Standard CMake find-module dependencies
##

set(THREADS_PREFER_PTHREAD_FLAG YES)
find_package(Threads REQUIRED)
set_target_properties(Threads::Threads PROPERTIES IMPORTED_GLOBAL TRUE)

# TODO(https://github.com/halide/Halide/issues/5633): verify this is still correct / necessary for OpenGLCompute
find_package(OpenGL)
if (TARGET OpenGL::GL)
    set_target_properties(OpenGL::GL PROPERTIES IMPORTED_GLOBAL TRUE)
endif ()

find_package(OpenMP)
if (TARGET OpenMP::OpenMP_CXX)
    set_target_properties(OpenMP::OpenMP_CXX PROPERTIES IMPORTED_GLOBAL TRUE)
endif ()

##
# Third-party dependencies in their own subdirectories
##

add_subdirectory(llvm)

add_subdirectory(jpeg)
add_subdirectory(png)

if (TARGET_SPIRV)
    add_subdirectory(spirv)
endif()

# Needs cache vars set by llvm, do not reorder.
add_subdirectory(wasm)
