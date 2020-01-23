add_executable(unsharp_filter filter.cpp)
halide_use_image_io(unsharp_filter)

halide_generator(unsharp.generator SRCS unsharp_generator.cpp)
foreach(AUTO_SCHEDULE false true)
    if(${AUTO_SCHEDULE})
        set(LIB unsharp_auto_schedule)
    else()
        set(LIB unsharp)
    endif()
    halide_library_from_generator(${LIB}
                                  GENERATOR unsharp.generator
                                  GENERATOR_ARGS auto_schedule=${AUTO_SCHEDULE})
    target_link_libraries(unsharp_filter PRIVATE ${LIB})
endforeach()
