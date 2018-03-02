add_executable(random_pipeline_process process.cpp)
halide_use_image_io(random_pipeline_process)

halide_generator(random_pipeline.generator SRCS random_pipeline_generator.cpp)
foreach(AUTO_SCHEDULE false true)
    if(${AUTO_SCHEDULE})
        set(LIB random_pipeline_auto_schedule)
    else()
        set(LIB random_pipeline)
    endif()
    halide_library_from_generator(${LIB}
                                  GENERATOR random_pipeline.generator
                                  GENERATOR_ARGS auto_schedule=${AUTO_SCHEDULE})
    target_link_libraries(random_pipeline_process PRIVATE ${LIB})
  endforeach()
  
