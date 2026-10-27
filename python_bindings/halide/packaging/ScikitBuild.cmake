# The version file consumed here is produced by this package's ordinary
# packaging directory. Append the wheel-only trampoline rules once the rest of
# the Python bindings build graph has been configured.
cmake_path(ABSOLUTE_PATH CMAKE_MODULE_PATH BASE_DIRECTORY "${CMAKE_SOURCE_DIR}" NORMALIZE)
include(InstallPythonTrampoline)

function(_Halide_Python_add_scikit_build_packaging)
    _Halide_install_python_trampoline(
        PACKAGE Halide_Python
        INSTALL_DIR "${Halide_Python_INSTALL_CMAKEDIR}"
        VERSION_FILE "${Halide_Python_BINARY_DIR}/halide/packaging/Halide_PythonConfigVersion.cmake"
        OUTPUT_DIR "${Halide_Python_BINARY_DIR}/halide/packaging/pip"
        COMPONENT Halide_Python
    )
endfunction()

cmake_language(DEFER CALL _Halide_Python_add_scikit_build_packaging)
