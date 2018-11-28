if(WIN32)
    # Building this app for x86 in VS seems to have issues
    # https://github.com/halide/Halide/pull/2740
    return()
endif()

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
    target_link_libraries(camera_pipe_process PRIVATE ${LIB} ${curved_lib})
endforeach()
