# Ensure that plugins export only what is needed to load them.
# Everything else should be omitted to keep binary size low.
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS OFF)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN YES)

function(add_autoscheduler)
    set(options)
    set(oneValueArgs NAME)
    set(multiValueArgs SOURCES)
    cmake_parse_arguments("arg" "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    add_library(Halide_${arg_NAME} MODULE ${arg_SOURCES})
    add_library(Halide::${arg_NAME} ALIAS Halide_${arg_NAME})

    target_compile_definitions(Halide_${arg_NAME} PRIVATE Halide_EXPORTS)
    target_link_libraries(Halide_${arg_NAME} PRIVATE Halide::Plugin)

    string(TOLOWER "autoschedule_${arg_NAME}" output_name)
    set_target_properties(Halide_${arg_NAME} PROPERTIES
                          EXPORT_NAME "${arg_NAME}"
                          OUTPUT_NAME "${output_name}")

    set_property(DIRECTORY "${Halide_SOURCE_DIR}"
                 APPEND PROPERTY Halide_AUTOSCHEDULERS
                 Halide_${arg_NAME})
endfunction()

## Autoschedulers must always be built for the same target as libHalide.
set(Halide_TARGET cmake)

add_subdirectory(common)

add_subdirectory(adams2019)
add_subdirectory(li2018)
add_subdirectory(mullapudi2016)
add_subdirectory(anderson2021)
