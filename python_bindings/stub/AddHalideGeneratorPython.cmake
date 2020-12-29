set(HALIDE_PYSTUB_CPP_PATH ${CMAKE_CURRENT_LIST_DIR}/PyStub.cpp)

function(add_generator_python TARGET)
    set(options)
    set(oneValueArgs)
    set(multiValueArgs)
    cmake_parse_arguments(args "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    Python3_add_library(${TARGET} MODULE ${HALIDE_PYSTUB_CPP_PATH} ${args_UNPARSED_ARGUMENTS})
    target_compile_definitions(${TARGET} PRIVATE
                               "HALIDE_PYSTUB_GENERATOR_NAME=${TARGET}"
                               "HALIDE_PYSTUB_MODULE_NAME=${TARGET}")
    target_link_libraries(${TARGET} PRIVATE Halide::PyStubs)
endfunction()
