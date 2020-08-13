include("shared-Release/CPackConfig.cmake")

set(CPACK_COMPONENTS_HALIDE_RUNTIME Halide_Runtime)
set(CPACK_COMPONENTS_HALIDE_DEVELOPMENT Halide_Development)

set(CPACK_INSTALL_CMAKE_PROJECTS
    static-Release Halide ALL /
    shared-Release Halide ALL /
    )
