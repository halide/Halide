add_executable(local_laplacian_process process.cpp)
halide_use_image_io(local_laplacian_process)

halide_generator(local_laplacian.generator SRCS local_laplacian_generator.cpp)
foreach(AUTO_SCHEDULE false true)
    if(${AUTO_SCHEDULE})
        set(LIB local_laplacian_auto_schedule)
    else()
        set(LIB local_laplacian)
    endif()
    halide_library_from_generator(${LIB}
                                  GENERATOR local_laplacian.generator
                                  GENERATOR_ARGS auto_schedule=${AUTO_SCHEDULE})
    target_link_libraries(local_laplacian_process PRIVATE ${LIB})
endforeach()
