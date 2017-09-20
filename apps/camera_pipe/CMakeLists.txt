# Generator
halide_library(camera_pipe SRCS camera_pipe_generator.cpp)

# fcam
# FIXME: Set -O3 here
add_library(fcam fcam/Demosaic.cpp fcam/Demosaic_ARM.cpp)
FIND_PACKAGE(OpenMP QUIET)
if (OPENMP_FOUND)
  target_compile_options(fcam PUBLIC "-fopenmp")
endif()

# Final executable
add_executable(process process.cpp)
target_link_libraries(process PUBLIC camera_pipe)
target_link_libraries(process PRIVATE ${curved_lib} fcam)
halide_use_image_io(process)

if (NOT MSVC)
  target_compile_options(process PRIVATE "-O2" "-msse2")
  if (OPENMP_FOUND)
    target_compile_options(process PRIVATE ${OpenMP_CXX_FLAGS})
    target_link_libraries(process PRIVATE ${OpenMP_CXX_FLAGS})
  else()
    target_compile_options(process PRIVATE "-Wno-unknown-pragmas")
  endif()
endif()
