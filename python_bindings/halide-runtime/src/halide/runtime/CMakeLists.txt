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

target_sources(
    Halide_PythonRuntime
    PRIVATE
        PyRuntime.cpp
        PyRuntimeBuffer.cpp
        # float16_t is a standalone scalar utility whose implementation depends
        # only on runtime headers and the C++ standard library. Reuse it here so
        # Buffer's half support does not duplicate its conversion logic or link
        # libHalide.
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../src/Float16.cpp"
    PRIVATE
    FILE_SET runtime_support_headers
        TYPE HEADERS
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../src"
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../src/Float16.h"
)

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

# Header-only runtime interface only: loaded AOT modules supply their own
# compiled Halide runtime.
target_link_libraries(Halide_PythonRuntime PRIVATE Halide::Runtime)

# Place the module in the combined build-tree package. The package directory is
# normally set by python_bindings/CMakeLists.txt.
if (NOT DEFINED Halide_Python_PACKAGE_DIR)
    set(Halide_Python_PACKAGE_DIR "${CMAKE_BINARY_DIR}/$<CONFIG>/site-packages/halide")
endif ()
set_target_properties(Halide_PythonRuntime
    PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${Halide_Python_PACKAGE_DIR}/runtime"
)

if (Halide_ASAN_ENABLED)
    set_target_properties(Halide_PythonRuntime PROPERTIES CMAKE_SHARED_LINKER_FLAGS -shared-libasan)
endif ()

# Copy the Python source owned by the halide.runtime subpackage.
set(python_runtime_source "${CMAKE_CURRENT_SOURCE_DIR}/__init__.py")
set(python_runtime_destination "${Halide_Python_PACKAGE_DIR}/runtime/__init__.py")

add_custom_command(
    OUTPUT "${python_runtime_destination}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${Halide_Python_PACKAGE_DIR}/runtime"
    COMMAND "${CMAKE_COMMAND}" -E copy "${python_runtime_source}" "${python_runtime_destination}"
    DEPENDS "${python_runtime_source}"
    VERBATIM
)

add_custom_target(Halide_PythonRuntime_sources DEPENDS "${python_runtime_destination}")
add_dependencies(Halide_PythonRuntime Halide_PythonRuntime_sources)
