# Generator
halide_project(camera_pipe "apps" camera_pipe.cpp)

set(curved_h "${CMAKE_CURRENT_BINARY_DIR}/curved.h")
set(curved_obj "${CMAKE_CURRENT_BINARY_DIR}/curved${CMAKE_CXX_OUTPUT_EXTENSION}")

# FIXME: Set -O3 here
add_library(fcam fcam/Demosaic.cpp fcam/Demosaic_ARM.cpp)
if (NOT MSVC)
    target_compile_options(fcam PUBLIC "-fopenmp")
endif()

# Final executable
add_executable(process process.cpp ${curved_h})
target_link_libraries(process PRIVATE ${curved_obj} fcam ${PNG_LIBRARIES})
target_include_directories(process PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
                           ${PNG_INCLUDE_DIRS})
target_compile_definitions(process PRIVATE ${PNG_DEFINITIONS})
if (NOT WIN32)
  target_link_libraries(process PRIVATE dl pthread)
endif()
set_target_properties(process camera_pipe PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                      "${CMAKE_CURRENT_BINARY_DIR}")
if (NOT MSVC)
  target_compile_options(process PRIVATE "-std=c++11")
endif()

# FIXME: Cannot use halide_add_generator_dependency()
add_custom_command(OUTPUT "${curved_h}" "${curved_obj}"
                   COMMAND camera_pipe 8 0
                   COMMENT "Generating curved header and object file")
