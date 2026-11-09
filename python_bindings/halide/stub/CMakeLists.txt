add_library(Halide_PyStubs STATIC ${CMAKE_CURRENT_LIST_DIR}/PyStubImpl.cpp)
add_library(Halide::PyStubs ALIAS Halide_PyStubs)

target_compile_definitions(Halide_PyStubs PRIVATE Py_LIMITED_API=0x030C0000)
target_link_libraries(
    Halide_PyStubs
    PRIVATE Halide::Halide $<BUILD_LOCAL_INTERFACE:Python::SABIModule>
)

set_target_properties(Halide_PyStubs
    PROPERTIES
    EXPORT_NAME PyStubs
    OUTPUT_NAME HalidePyStubs
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN TRUE
    POSITION_INDEPENDENT_CODE ON
    # This static library is installed, so it must contain object code rather
    # than LTO bitcode. -fno-lto overrides LTO flags injected through CXXFLAGS.
    INTERPROCEDURAL_OPTIMIZATION OFF
)
target_compile_options(Halide_PyStubs PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-fno-lto>)
