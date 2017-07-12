# Generator
halide_add_generator(camera_pipe.generator
                     SRCS camera_pipe_generator.cpp)
halide_add_aot_library(camera_pipe
                       GENERATOR_TARGET camera_pipe.generator
                       GENERATOR_ARGS target=host)

# fcam
# FIXME: Set -O3 here
add_library(fcam fcam/Demosaic.cpp fcam/Demosaic_ARM.cpp)
FIND_PACKAGE(OpenMP)
if (OPENMP_FOUND)
  target_compile_options(fcam PUBLIC "-fopenmp")
endif()
if (NOT MSVC)
  target_compile_options(fcam PRIVATE "-std=c++11")
endif()

# Final executable
add_executable(process process.cpp)
halide_add_aot_library_dependency(process camera_pipe)
target_link_libraries(process PRIVATE ${curved_lib} fcam)
use_image_io(process)

if (NOT MSVC)
  target_compile_options(process PRIVATE "-std=c++11" "-O2" "-msse2")
  if (OPENMP_FOUND)
    target_compile_options(process PRIVATE ${OpenMP_CXX_FLAGS})
    target_link_libraries(process PRIVATE ${OpenMP_CXX_FLAGS})
  else()
    target_compile_options(process PRIVATE "-Wno-unknown-pragmas")
  endif()
endif()
