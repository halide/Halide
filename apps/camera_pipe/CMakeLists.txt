add_executable(camera_pipe_process process.cpp)
halide_use_image_io(camera_pipe_process)

halide_generator(camera_pipe.generator SRCS camera_pipe_generator.cpp)

foreach(AUTO_SCHEDULE false true)
    if(${AUTO_SCHEDULE})
        set(LIB camera_pipe_auto_schedule)
    else()
        set(LIB camera_pipe)
    endif()
    halide_library_from_generator(${LIB}
                                  GENERATOR camera_pipe.generator
                                  GENERATOR_ARGS auto_schedule=${AUTO_SCHEDULE})
    target_link_libraries(camera_pipe_process PRIVATE ${LIB} ${curved_lib} fcam)
endforeach()

# fcam
# FIXME: Set -O3 here
add_library(fcam fcam/Demosaic.cpp fcam/Demosaic_ARM.cpp)
FIND_PACKAGE(OpenMP QUIET)
if (OPENMP_FOUND)
  target_compile_options(fcam PUBLIC "-fopenmp")
endif()

if (NOT MSVC)
  target_compile_options(camera_pipe_process PRIVATE "-O2" "-msse2")
  if (OPENMP_FOUND)
    target_compile_options(camera_pipe_process PRIVATE ${OpenMP_CXX_FLAGS})
    target_link_libraries(camera_pipe_process PRIVATE ${OpenMP_CXX_FLAGS})
  else()
    target_compile_options(camera_pipe_process PRIVATE "-Wno-unknown-pragmas")
  endif()
endif()
