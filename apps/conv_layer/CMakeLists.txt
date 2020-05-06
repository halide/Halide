add_executable(conv_layer_process process.cpp)
target_link_libraries(conv_layer_process PRIVATE Halide::ImageIO)

halide_generator(conv_layer.generator SRCS conv_layer_generator.cpp)
foreach(AUTO_SCHEDULE false true)
    if(${AUTO_SCHEDULE})
        set(LIB conv_layer_auto_schedule)
    else()
        set(LIB conv_layer)
    endif()
    halide_library_from_generator(${LIB}
                                  GENERATOR conv_layer.generator
                                  GENERATOR_ARGS auto_schedule=${AUTO_SCHEDULE})
    target_link_libraries(conv_layer_process PRIVATE ${LIB})
endforeach()
