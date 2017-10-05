add_executable(shallow_lower_bound_process process.cpp)
halide_use_image_io(shallow_lower_bound_process)

halide_generator(shallow_lower_bound.generator SRCS shallow_lower_bound_generator.cpp)
foreach(AUTO_SCHEDULE false true)
    if(${AUTO_SCHEDULE})
        set(LIB shallow_lower_bound_auto_schedule)
    else()
        set(LIB shallow_lower_bound)
    endif()
    halide_library_from_generator(${LIB}
                                  GENERATOR shallow_lower_bound.generator
                                  GENERATOR_ARGS auto_schedule=${AUTO_SCHEDULE})
    target_link_libraries(shallow_lower_bound_process PRIVATE ${LIB})
endforeach()
