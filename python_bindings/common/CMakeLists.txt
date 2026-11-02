add_library(Halide_PythonCommon INTERFACE)
add_library(Halide::PythonCommon ALIAS Halide_PythonCommon)

target_sources(Halide_PythonCommon INTERFACE FILE_SET HEADERS FILES PyBufferProtocol.h)
target_link_libraries(Halide_PythonCommon INTERFACE Halide::Runtime)
