add_executable(harris_filter filter.cpp)
halide_use_image_io(harris_filter)

halide_generator(harris.generator SRCS harris_generator.cpp)
foreach(AUTO_SCHEDULE false true)
    if(${AUTO_SCHEDULE})
        set(LIB harris_auto_schedule)
    else()
        set(LIB harris)
    endif()
    halide_library_from_generator(${LIB}
                                  GENERATOR harris.generator
                                  GENERATOR_ARGS auto_schedule=${AUTO_SCHEDULE})
    target_link_libraries(harris_filter PRIVATE ${LIB})
endforeach()
