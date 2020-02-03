add_executable(interpolate_filter filter.cpp)
target_link_libraries(interpolate_filter PRIVATE Halide::ImageIO)

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
