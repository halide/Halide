add_executable(hist_filter filter.cpp)
halide_use_image_io(hist_filter)

halide_generator(hist.generator SRCS hist_generator.cpp)
foreach(AUTO_SCHEDULE false true)
    if(${AUTO_SCHEDULE})
        set(LIB hist_auto_schedule)
    else()
        set(LIB hist)
    endif()
    halide_library_from_generator(${LIB}
                                  GENERATOR hist.generator
                                  GENERATOR_ARGS auto_schedule=${AUTO_SCHEDULE})
    target_link_libraries(hist_filter PRIVATE ${LIB})
endforeach()
