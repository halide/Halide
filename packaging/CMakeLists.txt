include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

##
# Custom locations
##

set(Halide_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/Halide"
    CACHE STRING "Path to Halide CMake files")

set(Halide_INSTALL_HELPERSDIR "${CMAKE_INSTALL_LIBDIR}/cmake/HalideHelpers"
    CACHE STRING "Path to Halide platform-independent CMake files")

set(Halide_INSTALL_PLUGINDIR "${CMAKE_INSTALL_LIBDIR}"
    CACHE STRING "Path to Halide plugins folder")

set(Halide_INSTALL_TOOLSDIR "${CMAKE_INSTALL_DATADIR}/tools"
    CACHE STRING "Path to Halide build-time tools and sources")

##
# Main library exports
##

install(TARGETS Halide Halide_Generator Halide_GenGen
        EXPORT Halide_Targets
        RUNTIME COMPONENT Halide_Runtime
        LIBRARY COMPONENT Halide_Runtime
        NAMELINK_COMPONENT Halide_Development
        ARCHIVE COMPONENT Halide_Development
        FILE_SET HEADERS COMPONENT Halide_Development)

if (TARGET Halide_Adams2019)
    install(TARGETS Halide_Adams2019 Halide_Li2018 Halide_Mullapudi2016 Halide_Anderson2021
            EXPORT Halide_Interfaces
            LIBRARY DESTINATION ${Halide_INSTALL_PLUGINDIR} COMPONENT Halide_Runtime
            NAMELINK_COMPONENT Halide_Development)
endif ()

##
# Runtime headers
##

install(TARGETS Halide_Runtime
        EXPORT Halide_Interfaces
        FILE_SET HEADERS COMPONENT Halide_Development)

##
# Halide tools
##

target_sources(Halide_RunGenMain INTERFACE $<INSTALL_INTERFACE:${Halide_INSTALL_TOOLSDIR}/RunGenMain.cpp>)

install(FILES ${Halide_SOURCE_DIR}/tools/RunGenMain.cpp
        DESTINATION ${Halide_INSTALL_TOOLSDIR}
        COMPONENT Halide_Development)

install(TARGETS Halide_Tools Halide_ImageIO Halide_RunGenMain Halide_ThreadPool
        EXPORT Halide_Interfaces
        FILE_SET HEADERS COMPONENT Halide_Development DESTINATION ${Halide_INSTALL_TOOLSDIR})

##
# Patch RPATH for executable targets
##

file(RELATIVE_PATH lib_dir
     ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}
     ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR})

if (APPLE)
    set(rbase @loader_path)
else ()
    set(rbase $ORIGIN)
endif ()

foreach (util IN ITEMS adams2019_retrain_cost_model
                       adams2019_weightsdir_to_weightsfile
                       anderson2021_retrain_cost_model
                       anderson2021_weightsdir_to_weightsfile
                       featurization_to_sample
                       get_host_target)
    if (TARGET ${util})
        if (NOT CMAKE_INSTALL_RPATH)
            set_target_properties(${util} PROPERTIES INSTALL_RPATH "${rbase};${rbase}/${lib_dir}")
        endif ()
        install(
            TARGETS ${util}
            EXPORT Halide_Interfaces
            COMPONENT Halide_Development
        )
    endif ()
endforeach ()

##
# READMEs and other top-level documentation
##

install(FILES
        ${Halide_SOURCE_DIR}/README_cmake.md
        ${Halide_SOURCE_DIR}/README.md
        ${Halide_SOURCE_DIR}/README_python.md
        ${Halide_SOURCE_DIR}/README_rungen.md
        ${Halide_SOURCE_DIR}/README_webassembly.md
        ${Halide_SOURCE_DIR}/LICENSE.txt
        COMPONENT Halide_Documentation
        TYPE DOC)

##
# Tools
##

install(PROGRAMS ${Halide_SOURCE_DIR}/src/autoschedulers/adams2019/adams2019_autotune_loop.sh
                 ${Halide_SOURCE_DIR}/src/autoschedulers/anderson2021/anderson2021_autotune_loop.sh
        DESTINATION ${Halide_INSTALL_TOOLSDIR}
        COMPONENT Halide_Development)

##
# Tutorial
##

if (WITH_TUTORIALS)
    install(DIRECTORY ${Halide_SOURCE_DIR}/tutorial
            TYPE DOC
            COMPONENT Halide_Documentation
            FILES_MATCHING
            PATTERN "*.cpp"
            PATTERN "*.h"
            PATTERN "lesson_*.sh"
            PATTERN "*.gif"
            PATTERN "*.jpg"
            PATTERN "*.mp4"
            PATTERN "*.png")
endif ()

##
# CMake scripts
##

if (BUILD_SHARED_LIBS)
    set(type shared)
else ()
    set(type static)
endif ()

# If Halide explicitly links against shared LLVM or if it is a static library
# and we are not bundling our static dependencies, then end-users must have
# the relevant system libraries installed.
if (Halide_LLVM_SHARED_LIBS OR (NOT BUILD_SHARED_LIBS AND NOT Halide_BUNDLE_LLVM))
    set(depFile "${CMAKE_CURRENT_BINARY_DIR}/Halide-${type}-deps.cmake")
    file(CONFIGURE
         OUTPUT "${depFile}"
         CONTENT [[
set(Halide_LLVM_SHARED_LIBS @Halide_LLVM_SHARED_LIBS@)
find_dependency(
    Halide_LLVM @Halide_LLVM_VERSION@
    COMPONENTS @Halide_LLVM_COMPONENTS@
)
]] @ONLY)

    install(FILES "${depFile}" "${Halide_SOURCE_DIR}/cmake/FindHalide_LLVM.cmake"
            DESTINATION ${Halide_INSTALL_CMAKEDIR}
            COMPONENT Halide_Development)
endif ()

install(EXPORT Halide_Targets
        DESTINATION ${Halide_INSTALL_CMAKEDIR}
        NAMESPACE Halide::
        FILE Halide-${type}-targets.cmake
        COMPONENT Halide_Development)

install(EXPORT Halide_Interfaces
        DESTINATION ${Halide_INSTALL_HELPERSDIR}
        NAMESPACE Halide::
        FILE Halide-Interfaces.cmake
        COMPONENT Halide_Development)

write_basic_package_version_file(HalideConfigVersion.cmake
                                 COMPATIBILITY SameMajorVersion)

write_basic_package_version_file(HalideHelpersConfigVersion.cmake
                                 COMPATIBILITY SameMajorVersion
                                 ARCH_INDEPENDENT)

# Compute a hint to make it easier to find HalideHelpers from find_package(Halide)
# This is read by configure_file below.
file(RELATIVE_PATH HalideHelpers_HINT
     "${CMAKE_CURRENT_BINARY_DIR}/${Halide_INSTALL_CMAKEDIR}"
     "${CMAKE_CURRENT_BINARY_DIR}/${Halide_INSTALL_HELPERSDIR}")

configure_file(common/HalideConfig.cmake HalideConfig.cmake @ONLY)
configure_file(common/HalideHelpersConfig.cmake HalideHelpersConfig.cmake @ONLY)

install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/HalideConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/HalideConfigVersion.cmake
        DESTINATION ${Halide_INSTALL_CMAKEDIR}
        COMPONENT Halide_Development)

install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/HalideHelpersConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/HalideHelpersConfigVersion.cmake
        ${Halide_SOURCE_DIR}/cmake/HalideGeneratorHelpers.cmake
        ${Halide_SOURCE_DIR}/cmake/FindHalide_WebGPU.cmake
        ${Halide_SOURCE_DIR}/cmake/HalideTargetHelpers.cmake
        ${Halide_SOURCE_DIR}/cmake/TargetExportScript.cmake
        ${Halide_SOURCE_DIR}/cmake/MutexCopy.ps1
        DESTINATION ${Halide_INSTALL_HELPERSDIR}
        COMPONENT Halide_Development)

##
# Documentation
##

if (WITH_DOCS)
    install(DIRECTORY ${Halide_BINARY_DIR}/doc/html
            TYPE DOC
            COMPONENT Halide_Documentation)
endif ()

##
# General packaging variables.
##

set(CPACK_PACKAGE_NAME Halide)
set(CPACK_PACKAGE_VENDOR Halide)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Halide compiler and libraries")
set(CPACK_PACKAGE_INSTALL_DIRECTORY ${CPACK_PACKAGE_NAME})
set(CPACK_PACKAGE_VERSION_MAJOR ${Halide_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${Halide_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${Halide_VERSION_PATCH})
set(CPACK_VERBATIM_VARIABLES YES)
set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_CURRENT_LIST_DIR}/common/Description.txt")
set(CPACK_RESOURCE_FILE_WELCOME "${CMAKE_CURRENT_LIST_DIR}/common/Welcome.txt")
set(CPACK_RESOURCE_FILE_LICENSE "${Halide_SOURCE_DIR}/LICENSE.txt")
set(CPACK_RESOURCE_FILE_README "${Halide_SOURCE_DIR}/README.md")

set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${Halide_VERSION}-${Halide_HOST_TARGET}")

include(CPack)

##
# Configure components
##

cpack_add_component(Halide_Runtime
                    DISPLAY_NAME "Halide"
                    DESCRIPTION "Runtime files for libHalide and autoschedulers")

cpack_add_component(Halide_Development
                    DISPLAY_NAME "Halide development"
                    DESCRIPTION "Static Halide libraries and CMake development files"
                    DEPENDS Halide_Runtime)

cpack_add_component(Halide_Python
                    DISPLAY_NAME "Python bindings"
                    DESCRIPTION "Python package providing bindings to Halide"
                    DEPENDS Halide_Runtime)

cpack_add_component(Halide_Documentation
                    DISPLAY_NAME "Halide documentation"
                    DESCRIPTION "Documentation for Halide")
