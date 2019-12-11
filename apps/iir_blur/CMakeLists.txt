add_executable(iir_blur_filter filter.cpp)
halide_use_image_io(iir_blur_filter)

halide_generator(iir_blur.generator SRCS iir_blur_generator.cpp)
foreach(AUTO_SCHEDULE false true)
    if(${AUTO_SCHEDULE})
        set(LIB iir_blur_auto_schedule)
    else()
        set(LIB iir_blur)
    endif()
    halide_library_from_generator(${LIB}
                                  GENERATOR iir_blur.generator
                                  GENERATOR_ARGS auto_schedule=${AUTO_SCHEDULE})
    target_link_libraries(iir_blur_filter PRIVATE ${LIB})
endforeach()
