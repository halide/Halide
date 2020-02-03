add_executable(stencil_chain_process process.cpp)
target_link_libraries(stencil_chain_process PRIVATE Halide::ImageIO)

halide_generator(stencil_chain.generator SRCS stencil_chain_generator.cpp)
foreach(AUTO_SCHEDULE false true)
    if(${AUTO_SCHEDULE})
        set(LIB stencil_chain_auto_schedule)
    else()
        set(LIB stencil_chain)
    endif()
    halide_library_from_generator(${LIB}
                                  GENERATOR stencil_chain.generator
                                  GENERATOR_ARGS auto_schedule=${AUTO_SCHEDULE})
    target_link_libraries(stencil_chain_process PRIVATE ${LIB})
endforeach()
