# Python build requirements install their CMake packages beneath Python package
# directories rather than directly beneath site-packages. Add those prefixes
# before src resolves its dependencies.
cmake_path(ABSOLUTE_PATH CMAKE_MODULE_PATH BASE_DIRECTORY "${CMAKE_SOURCE_DIR}" NORMALIZE)
include(InstallPythonTrampoline)

function(_Halide_find_python_build_requirement distribution package_dir result)
    if (NOT Python_EXECUTABLE)
        message(FATAL_ERROR "Python_EXECUTABLE is required to locate ${distribution}")
    endif ()

    execute_process(
        COMMAND
            "${Python_EXECUTABLE}"
            -c "from importlib.metadata import distribution; print(distribution('${distribution}').locate_file('${package_dir}'))"
        OUTPUT_VARIABLE prefix
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY
    )
    if (NOT IS_DIRECTORY "${prefix}")
        message(FATAL_ERROR "Could not locate the ${distribution} build requirement")
    endif ()

    set("${result}" "${prefix}" PARENT_SCOPE)
endfunction()

_Halide_find_python_build_requirement(
    halide-flatbuffers halide-flatbuffers Halide_FLATBUFFERS_BUILD_PREFIX
)
list(PREPEND CMAKE_PREFIX_PATH "${Halide_FLATBUFFERS_BUILD_PREFIX}")

if (Halide_WASM_BACKEND STREQUAL "wabt")
    _Halide_find_python_build_requirement(halide-wabt halide-wabt Halide_WABT_BUILD_PREFIX)
    list(PREPEND CMAKE_PREFIX_PATH "${Halide_WABT_BUILD_PREFIX}")
endif ()

# The files consumed here are produced by packaging/CMakeLists.txt, so append
# these wheel-only install rules after the ordinary top-level build graph.
function(_Halide_add_scikit_build_packaging)
    set(pip_binary_dir "${Halide_BINARY_DIR}/packaging/pip")
    _Halide_install_python_trampoline(
        PACKAGE Halide
        INSTALL_DIR "${Halide_INSTALL_CMAKEDIR}"
        VERSION_FILE "${Halide_BINARY_DIR}/packaging/HalideConfigVersion.cmake"
        OUTPUT_DIR "${pip_binary_dir}"
        COMPONENT Halide_Development
    )
    _Halide_install_python_trampoline(
        PACKAGE HalideCompiler
        INSTALL_DIR "${Halide_INSTALL_COMPILERDIR}"
        VERSION_FILE "${Halide_BINARY_DIR}/packaging/HalideCompilerConfigVersion.cmake"
        OUTPUT_DIR "${pip_binary_dir}"
        COMPONENT Halide_Development
    )
    _Halide_install_python_trampoline(
        PACKAGE HalideAutoschedulers
        INSTALL_DIR "${Halide_INSTALL_AUTOSCHEDULERSDIR}"
        VERSION_FILE "${Halide_BINARY_DIR}/packaging/HalideAutoschedulersConfigVersion.cmake"
        OUTPUT_DIR "${pip_binary_dir}"
        COMPONENT Halide_Python
    )
endfunction()

cmake_language(DEFER CALL _Halide_add_scikit_build_packaging)
