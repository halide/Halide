add_executable(lens_blur_process process.cpp)
halide_use_image_io(lens_blur_process)

halide_generator(lens_blur.generator SRCS lens_blur_generator.cpp)
foreach(AUTO_SCHEDULE false true)
    if(${AUTO_SCHEDULE})
        set(LIB lens_blur_auto_schedule)
    else()
        set(LIB lens_blur)
    endif()
    halide_library_from_generator(${LIB}
                                  GENERATOR lens_blur.generator
                                  GENERATOR_ARGS auto_schedule=${AUTO_SCHEDULE})
    target_link_libraries(lens_blur_process PRIVATE ${LIB})
endforeach()
