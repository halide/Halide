set(CPACK_NUGET_COMPONENT_INSTALL OFF)
set(CPACK_NUGET_PACKAGE_AUTHORS "Andrew Adams, Jonathan Ragan-Kelley, Steven Johnson, Tzu-Mao Li, alexreinking, Volodymyr Kysenko, Benoit Steiner, Dillon Sharlet, Shoaib Kamil, Zalman Stern")
set(CPACK_NUGET_PACKAGE_TITLE "Halide Compiler and Libraries")
set(CPACK_NUGET_PACKAGE_OWNERS alexreinking)
set(CPACK_NUGET_PACKAGE_COPYRIGHT "Copyright (c) 2012-2020 MIT CSAIL, Google, Facebook, Adobe, NVIDIA CORPORATION, and other contributors.")
set(CPACK_NUGET_PACKAGE_TAGS "Halide C++ CUDA OpenCL GPU Performance DSL native")
set(CPACK_NUGET_PACKAGE_DEPENDENCIES "")
set(CPACK_NUGET_PACKAGE_DEBUG OFF)
set(CPACK_NUGET_PACKAGE_LICENSE_EXPRESSION MIT)

# CMake 3.20 correctly uses SPDX license variables and drops
# support for the deprecated license URL option in Nuget.
if (CMAKE_VERSION VERSION_LESS 3.20)
    set(CPACK_NUGET_PACKAGE_LICENSEURL "https://github.com/halide/Halide/blob/master/LICENSE.txt")
endif ()
