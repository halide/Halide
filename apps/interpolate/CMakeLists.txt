add_executable(interpolate_filter filter.cpp)
halide_use_image_io(interpolate_filter)

halide_generator(interpolate.generator SRCS interpolate_generator.cpp)
foreach(AUTO_SCHEDULE false true)
    if(${AUTO_SCHEDULE})
        set(LIB interpolate_auto_schedule)
    else()
        set(LIB interpolate)
    endif()
    halide_library_from_generator(${LIB}
                                  GENERATOR interpolate.generator
                                  GENERATOR_ARGS auto_schedule=${AUTO_SCHEDULE})
    target_link_libraries(interpolate_filter PRIVATE ${LIB})
endforeach()
