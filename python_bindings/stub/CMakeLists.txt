add_library(Halide_PyStubs STATIC ${CMAKE_CURRENT_LIST_DIR}/PyStubImpl.cpp)
add_library(Halide::PyStubs ALIAS Halide_PyStubs)

# Don't add a direct dependency on pybind11::pybind11 here: that will add a
# phantom dependency which gets propagated into our install setup. All we
# really need here is a path to the include directories for pybind11, which
# BUILD_INTERFACE will accomplish,
target_link_libraries(Halide_PyStubs PRIVATE Halide::Halide $<BUILD_INTERFACE:pybind11::pybind11>)

set_target_properties(Halide_PyStubs PROPERTIES
                      EXPORT_NAME PyStubs
                      OUTPUT_NAME HalidePyStubs
                      CXX_VISIBILITY_PRESET hidden
                      VISIBILITY_INLINES_HIDDEN TRUE
                      POSITION_INDEPENDENT_CODE ON)

install(TARGETS Halide_PyStubs
        EXPORT Halide_Targets
        COMPONENT Halide_Python)
