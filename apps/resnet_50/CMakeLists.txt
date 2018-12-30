add_executable(resnet50_process process.cpp)
halide_use_image_io(resnet50_process)

halide_generator(resnet50.generator SRCS Resnet50Generator.cpp)
foreach(AUTO_SCHEDULE false)
    set(LIB resnet50)
    halide_library_from_generator(${LIB}
                                  GENERATOR resnet50.generator
                                  GENERATOR_ARGS auto_schedule=${AUTO_SCHEDULE})
    target_link_libraries(resnet50_process PRIVATE ${LIB})
endforeach()
