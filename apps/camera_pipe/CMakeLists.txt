# Generator
halide_project(camera_pipe "apps" camera_pipe.cpp)

set(curved_h "${CMAKE_CURRENT_BINARY_DIR}/curved.h")
set(curved_lib "${CMAKE_CURRENT_BINARY_DIR}/curved${CMAKE_STATIC_LIBRARY_SUFFIX}")

# FIXME: Set -O3 here
add_library(fcam fcam/Demosaic.cpp fcam/Demosaic_ARM.cpp)
FIND_PACKAGE(OpenMP)
if (OPENMP_FOUND)
    target_compile_options(fcam PUBLIC "-fopenmp")
endif()

# Final executable
add_executable(process process.cpp ${curved_h})
target_link_libraries(process PRIVATE ${curved_lib} fcam ${PNG_LIBRARIES})
target_include_directories(process PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
                           ${PNG_INCLUDE_DIRS})
target_compile_definitions(process PRIVATE ${PNG_DEFINITIONS})
if (NOT WIN32)
  target_link_libraries(process PRIVATE dl pthread)
endif()
if (NOT MSVC)
  target_compile_options(process PRIVATE "-std=c++11")
endif()

# FIXME: Cannot use halide_add_generator_dependency()
add_custom_command(OUTPUT "${curved_h}" "${curved_lib}"
                   COMMAND camera_pipe 8 0
                   WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
                   COMMENT "Generating curved header and object file")
