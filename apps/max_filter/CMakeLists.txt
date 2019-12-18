add_executable(max_filter_filter filter.cpp)
halide_use_image_io(max_filter_filter)

halide_generator(max_filter.generator SRCS max_filter_generator.cpp)
foreach(AUTO_SCHEDULE false true)
    if(${AUTO_SCHEDULE})
        set(LIB max_filter_auto_schedule)
    else()
        set(LIB max_filter)
    endif()
    halide_library_from_generator(${LIB}
                                  GENERATOR max_filter.generator
                                  GENERATOR_ARGS auto_schedule=${AUTO_SCHEDULE})
    target_link_libraries(max_filter_filter PRIVATE ${LIB})
endforeach()
