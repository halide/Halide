add_library(Halide_PyStubs STATIC ${CMAKE_CURRENT_LIST_DIR}/PyStubImpl.cpp)
add_library(Halide::PyStubs ALIAS Halide_PyStubs)

target_link_libraries(Halide_PyStubs PRIVATE Halide::Halide)

# Don't add a dependency on pybind11::pybind11 here: that will add
# a phantom dependency is propagated into our install setup.
# All we really need here is a path to the include directories for pybind11,
# which is publicly documented as `pybind11_INCLUDE_DIRS`,
target_include_directories(Halide_PyStubs PRIVATE "${HALIDE_INCLUDES}" "${pybind11_INCLUDE_DIRS}")

set_target_properties(Halide_PyStubs PROPERTIES
                      EXPORT_NAME PyStubs
                      OUTPUT_NAME HalidePyStubs
                      CXX_VISIBILITY_PRESET hidden
                      VISIBILITY_INLINES_HIDDEN TRUE
                      POSITION_INDEPENDENT_CODE ON)
