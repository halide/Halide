add_library(Halide_PyStubs STATIC ${CMAKE_CURRENT_LIST_DIR}/PyStubImpl.cpp)
add_library(Halide::PyStubs ALIAS Halide_PyStubs)

target_link_libraries(Halide_PyStubs PRIVATE pybind11::module Halide::Halide)
set_target_properties(Halide_PyStubs PROPERTIES
                      EXPORT_NAME PyStubs
                      OUTPUT_NAME HalidePyStubs
                      CXX_VISIBILITY_PRESET hidden
                      VISIBILITY_INLINES_HIDDEN TRUE
                      POSITION_INDEPENDENT_CODE ON)
