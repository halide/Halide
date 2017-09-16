add_executable(nl_means_process process.cpp)
halide_use_image_io(nl_means_process)

halide_generator(nl_means.generator SRCS nl_means_generator.cpp)
foreach(AUTO_SCHEDULE false true)
    if(${AUTO_SCHEDULE})
        set(LIB nl_means_auto_schedule)
    else()
        set(LIB nl_means)
    endif()
    halide_library_from_generator(${LIB}
                                  GENERATOR nl_means.generator
                                  GENERATOR_ARGS auto_schedule=${AUTO_SCHEDULE})
    target_link_libraries(nl_means_process PRIVATE ${LIB})
endforeach()
