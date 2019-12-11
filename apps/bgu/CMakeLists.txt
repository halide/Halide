add_executable(bgu_filter filter.cpp)
halide_use_image_io(bgu_filter)

halide_generator(bgu.generator SRCS bgu_generator.cpp)
foreach(AUTO_SCHEDULE false true)
    if(${AUTO_SCHEDULE})
        set(LIB bgu_auto_schedule)
    else()
        set(LIB bgu)
    endif()
    halide_library_from_generator(${LIB}
                                  GENERATOR bgu.generator
                                  GENERATOR_ARGS auto_schedule=${AUTO_SCHEDULE})
    target_link_libraries(bgu_filter PRIVATE ${LIB})
endforeach()
