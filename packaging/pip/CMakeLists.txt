##
# Create a trampoline to the real *Config.cmake.
#
# The various wheel directories get grafted to one of an unpredictable set
# of paths determined by sysconfig. The trampoline finds platlib via sysconfig
# before jumping to the real *Config.cmake inside our pip package.

function(configure_trampoline PACKAGE INSTALL_DIR)
    configure_file(TrampolineConfig.cmake.in "${PACKAGE}Config.cmake" @ONLY)
endfunction()

configure_trampoline(Halide "${Halide_INSTALL_CMAKEDIR}")
configure_trampoline(HalideHelpers "${Halide_INSTALL_HELPERSDIR}")

install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/HalideConfig.cmake"
    # It's better to duplicate the version file than to trampoline to it, as
    # this would require calling find_package(Python) in the version file.
    "${CMAKE_CURRENT_BINARY_DIR}/../HalideConfigVersion.cmake"
    # It's okay to hard-code the destination because this code is only
    # called by scikit-build-core. Installing to /data ultimately installs
    # to the Python root installation directory, whose bin/ directory is
    # located on the PATH. On Unix systems, this is directly compatible with
    # find_package. Python on Windows unfortunately uses Scripts/ instead of
    # bin/, which CMake does not understand. These users can add %VIRTUAL_ENV%
    # to their CMAKE_PREFIX_PATH.
    DESTINATION "${SKBUILD_DATA_DIR}/share/cmake/Halide"
    COMPONENT Halide_Python
)

# Same thing for HalideHelpers
install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/HalideHelpersConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/../HalideHelpersConfigVersion.cmake"
    DESTINATION "${SKBUILD_DATA_DIR}/share/cmake/HalideHelpers"
    COMPONENT Halide_Python
)

##
# Set up RPATH for the Python bindings plugin.

if (WITH_PYTHON_BINDINGS)
    _Halide_compute_rpath(
        TARGETS Halide_Python
        ORIGIN_DIR "${Halide_INSTALL_PYTHONDIR}"
        LIB_DIR "${CMAKE_INSTALL_LIBDIR}"
    )
endif ()