##
# Create a trampoline to the real Halide_PythonConfig.cmake.
#
# Same trick as the halide-bin CMake trampoline: the wheel
# directory gets grafted to one of an unpredictable set of paths determined
# by sysconfig, so this trampoline finds platlib via sysconfig before jumping
# to the real Halide_PythonConfig.cmake inside the halide package.

configure_file(TrampolineConfig.cmake.in "Halide_PythonConfig.cmake" @ONLY)

install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/Halide_PythonConfig.cmake"
        # It's better to duplicate the version file than to trampoline to it, as
        # this would require calling find_package(Python) in the version file.
        "${CMAKE_CURRENT_BINARY_DIR}/../Halide_PythonConfigVersion.cmake"
    DESTINATION "${SKBUILD_DATA_DIR}/share/cmake/Halide_Python"
    COMPONENT Halide_Python
)
