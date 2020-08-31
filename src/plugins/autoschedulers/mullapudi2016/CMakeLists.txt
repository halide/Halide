add_library(Halide_Mullapudi2016 MODULE AutoSchedule.cpp)
add_library(Halide::Mullapudi2016 ALIAS Halide_Mullapudi2016)

include(GenerateExportHeader)
generate_export_header(Halide_Mullapudi2016)
target_include_directories(Halide_Mullapudi2016 PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)

target_link_libraries(Halide_Mullapudi2016 PRIVATE Halide::Halide)
target_compile_definitions(Halide_Mullapudi2016 PRIVATE Halide_ERROR_MACROS)

set_target_properties(Halide_Mullapudi2016 PROPERTIES
                      EXPORT_NAME Mullapudi2016
                      OUTPUT_NAME mullapudi2016)
