##
# The `halide.runtime` extension module.
#
# This links ONLY the header-only Halide runtime (Halide::Runtime), never
# libHalide, so it can be imported standalone to call precompiled AOT kernels.
##

pybind11_add_module(Halide_PythonRuntime)
add_library(Halide::PythonRuntime ALIAS Halide_PythonRuntime)

set_target_properties(Halide_PythonRuntime
    PROPERTIES
    OUTPUT_NAME _runtime
    EXPORT_NAME PythonRuntime
)

target_sources(Halide_PythonRuntime PRIVATE PyRuntime.cpp)

# PyRuntime.cpp #includes the shared marshalling core (it depends only on
# <Python.h> + HalideRuntime.h). In an in-tree build it lives in the Halide
# source tree; when building against an installed Halide it is installed next to
# HalideRuntime.h and found via Halide::Runtime's include directories (so no
# extra include path is needed in that case).
if (
    DEFINED Halide_SOURCE_DIR
    AND EXISTS "${Halide_SOURCE_DIR}/src/PythonExtensionRuntime.template.cpp"
)
    target_include_directories(Halide_PythonRuntime PRIVATE "${Halide_SOURCE_DIR}/src")
endif ()

# Runtime only: link a compiled Halide runtime (halide_copy_to_host,
# halide_device_free, error/print handlers, ...) plus the header-only runtime
# interface -- but NOT libHalide/the compiler.
add_halide_runtime(Halide_PythonRuntime_rt)
target_link_libraries(Halide_PythonRuntime PRIVATE Halide::Runtime Halide_PythonRuntime_rt)

# Place the module next to the compiler extension in normal CMake builds. The
# package binary directory is set by python_bindings/CMakeLists.txt.
if (NOT DEFINED Halide_Python_PACKAGE_BINARY_DIR)
    set(Halide_Python_PACKAGE_BINARY_DIR "${CMAKE_BINARY_DIR}")
endif ()
set_target_properties(Halide_PythonRuntime
    PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${Halide_Python_PACKAGE_BINARY_DIR}/$<CONFIG>/halide/runtime"
)

if (Halide_ASAN_ENABLED)
    set_target_properties(Halide_PythonRuntime PROPERTIES CMAKE_SHARED_LINKER_FLAGS -shared-libasan)
endif ()

# Copy the Python source for the subpackage next to the extension.
set(python_sources __init__.py)
list(TRANSFORM python_sources
    PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/"
    OUTPUT_VARIABLE python_sources_source_dir
)

set(stamp_file "$<CONFIG>/Halide_PythonRuntime_sources.stamp")
add_custom_command(
    OUTPUT "${stamp_file}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory $<TARGET_FILE_DIR:Halide_PythonRuntime>
    COMMAND
        "${CMAKE_COMMAND}" -E copy -t $<TARGET_FILE_DIR:Halide_PythonRuntime>
        ${python_sources_source_dir}
    COMMAND "${CMAKE_COMMAND}" -E touch "${stamp_file}"
    DEPENDS ${python_sources_source_dir}
    VERBATIM
)

add_custom_target(Halide_PythonRuntime_sources DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/${stamp_file}")
add_dependencies(Halide_PythonRuntime Halide_PythonRuntime_sources)
