include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

##
# Custom locations
##

set(HALIDE_INSTALL_CMAKEDIR
    "${CMAKE_INSTALL_LIBDIR}/cmake/Halide"
    CACHE STRING "Path to Halide cmake files")

##
# Main library exports
##

# TODO(#4053): fix when autoschedulers are refactored
unset(EXTRA_TARGETS)
foreach (T IN ITEMS Halide_Adams2019 Halide_Li2018 Halide_Python)
    if (TARGET ${T})
        list(APPEND EXTRA_TARGETS ${T})
    endif ()
endforeach ()

# Define destinations for all the targets
install(TARGETS Halide Halide_Plugin Halide_Runtime ${EXTRA_TARGETS}
        EXPORT Halide_Targets

        RUNTIME
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT Halide_Runtime

        LIBRARY
        DESTINATION ${CMAKE_INSTALL_LIBDIR}
        COMPONENT Halide_Runtime
        NAMELINK_COMPONENT Halide_Development

        ARCHIVE
        DESTINATION ${CMAKE_INSTALL_LIBDIR}
        COMPONENT Halide_Development

        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(TARGETS Halide_Tools Halide_ImageIO
        EXPORT Halide_Targets
        INCLUDES DESTINATION ${CMAKE_INSTALL_DATADIR}/tools)

install(TARGETS Halide_Generator Halide_RunGenMain
        EXPORT Halide_Targets)

# Create a cmake script for importing those targets for consuming libraries.
install(EXPORT Halide_Targets
        DESTINATION ${HALIDE_INSTALL_CMAKEDIR}
        NAMESPACE Halide::
        FILE Halide-Targets.cmake
        COMPONENT Halide_Development)

# Captures both the runtime and Halide.h
install(DIRECTORY ${Halide_BINARY_DIR}/include/ TYPE INCLUDE FILES_MATCHING PATTERN "include/*.h")

##
# READMEs and other top-level documentation
##

install(FILES
        ${Halide_SOURCE_DIR}/CODE_OF_CONDUCT.md
        ${Halide_SOURCE_DIR}/README_cmake.md
        ${Halide_SOURCE_DIR}/README.md
        ${Halide_SOURCE_DIR}/README_rungen.md
        ${Halide_SOURCE_DIR}/README_webassembly.md
        TYPE DATA)

##
# Tools
##

install(DIRECTORY ${Halide_SOURCE_DIR}/tools
        TYPE DATA
        FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*.cpp"
        PATTERN "*.m"
        PATTERN "binary2cpp.cpp" EXCLUDE
        PATTERN "build_halide_h.cpp" EXCLUDE
        PATTERN "find_inverse.cpp" EXCLUDE)

##
# Tutorial
##

install(DIRECTORY ${Halide_SOURCE_DIR}/tutorial
        TYPE DATA
        FILES_MATCHING
        PATTERN "*.cpp"
        PATTERN "*.h"
        PATTERN "lesson_*.sh"
        PATTERN "*.gif"
        PATTERN "*.jpg"
        PATTERN "*.mp4"
        PATTERN "*.png")

##
# CMake scripts
##

if (BUILD_SHARED_LIBS)
    # Don't need to expose LLVM-related targets
    unset(__find_LLVM_deps)
else ()
    set(__find_LLVM_deps "find_dependency(LLVM CONFIG)")
endif ()

configure_file(HalideConfig.cmake.in HalideConfig.cmake @ONLY)

write_basic_package_version_file(
        HalideConfigVersion.cmake
        COMPATIBILITY SameMajorVersion)

install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/HalideConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/HalideConfigVersion.cmake
        ${Halide_SOURCE_DIR}/cmake/HalideGeneratorHelpers.cmake
        DESTINATION ${HALIDE_INSTALL_CMAKEDIR})

##
# Documentation
##

# See /doc/CMakeLists.txt. This way, special install logic isn't needed.

##
# Actual packaging
##

set(CPACK_PACKAGE_VENDOR "Halide")
set(CPACK_RESOURCE_FILE_LICENSE "${Halide_SOURCE_DIR}/LICENSE.txt")
if (WIN32)
    set(CPACK_GENERATOR "ZIP")
else ()
    set(CPACK_GENERATOR "TGZ")
endif ()
# Remove this to get package names that are formatted as
# ${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CPACK_SYSTEM_NAME}.
set(CPACK_PACKAGE_FILE_NAME "Halide" CACHE STRING "Name of package created by distrib target")
include(CPack)
