include("shared-Release/CPackConfig.cmake")

set(CPACK_COMPONENTS_HALIDE_RUNTIME Halide_Runtime)
set(CPACK_COMPONENTS_HALIDE_DEVELOPMENT Halide_Development)

set(CPACK_INSTALL_CMAKE_PROJECTS
    # We don't package debug binaries on Unix systems. Our developers
    # don't use them and LLVM in debug mode is next to unusable, too.
    # static-Debug Halide ALL /
    # shared-Debug Halide ALL /
    static-Release Halide ALL /
    shared-Release Halide ALL /
    )
