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

target_sources(Halide_Generator INTERFACE $<INSTALL_INTERFACE:${Halide_INSTALL_TOOLSDIR}/GenGen.cpp>)

install(TARGETS Halide Halide_Generator Halide_LanguageOptions
        EXPORT Halide_Targets
        RUNTIME COMPONENT Halide_Runtime
        LIBRARY COMPONENT Halide_Runtime
        NAMELINK_COMPONENT Halide_Development
        ARCHIVE COMPONENT Halide_Development
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if (TARGET Halide_Adams2019)
    install(TARGETS Halide_Adams2019 Halide_Li2018 Halide_Mullapudi2016
            EXPORT Halide_Targets
            LIBRARY DESTINATION ${Halide_INSTALL_PLUGINDIR} COMPONENT Halide_Runtime
            NAMELINK_COMPONENT Halide_Development)
endif ()

# Halide_LLVM
foreach (dep IN ITEMS Halide_LLVM Halide_wabt)
    if (TARGET ${dep})
        install(TARGETS ${dep} EXPORT Halide_Targets)
    endif ()
endforeach ()

##
# Python bindings
##

if (WITH_PYTHON_BINDINGS)
    set(Halide_INSTALL_PYTHONDIR "${CMAKE_INSTALL_LIBDIR}/python3/site-packages"
        CACHE STRING "Path to Halide Python bindings folder")
    install(TARGETS Halide_Python
            LIBRARY DESTINATION ${Halide_INSTALL_PYTHONDIR} COMPONENT Halide_Python
            NAMELINK_COMPONENT Halide_Python)
endif ()

##
# Library-type-agnostic interface targets
##

target_sources(Halide_RunGenMain INTERFACE $<INSTALL_INTERFACE:${Halide_INSTALL_TOOLSDIR}/RunGenMain.cpp>)

install(TARGETS Halide_Tools Halide_ImageIO Halide_RunGenMain
        EXPORT Halide_Interfaces
        INCLUDES DESTINATION ${Halide_INSTALL_TOOLSDIR})

install(TARGETS Halide_Runtime
        EXPORT Halide_Interfaces
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Captures both the runtime and Halide.h
install(DIRECTORY ${Halide_BINARY_DIR}/include/
        TYPE INCLUDE
        COMPONENT Halide_Development
        FILES_MATCHING PATTERN "include/*.h")

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

if (TARGET Halide_Adams2019)
    set(autoscheduler_utils retrain_cost_model featurization_to_sample get_host_target weightsdir_to_weightsfile)
    if (NOT CMAKE_INSTALL_RPATH)
        set_target_properties(${autoscheduler_utils} PROPERTIES INSTALL_RPATH "${rbase};${rbase}/${lib_dir}")
    endif ()
    install(TARGETS ${autoscheduler_utils}
            EXPORT Halide_Interfaces
            COMPONENT Halide_Development)
endif ()

##
# READMEs and other top-level documentation
##

install(FILES
        ${Halide_SOURCE_DIR}/README_cmake.md
        ${Halide_SOURCE_DIR}/README.md
        ${Halide_SOURCE_DIR}/README_rungen.md
        ${Halide_SOURCE_DIR}/README_webassembly.md
        ${Halide_SOURCE_DIR}/LICENSE.txt
        COMPONENT Halide_Documentation
        TYPE DOC)

##
# Tools
##

install(DIRECTORY ${Halide_SOURCE_DIR}/tools/
        DESTINATION ${Halide_INSTALL_TOOLSDIR}
        COMPONENT Halide_Development
        FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*.cpp"
        PATTERN "*.m"
        PATTERN "binary2cpp.cpp" EXCLUDE
        PATTERN "build_halide_h.cpp" EXCLUDE
        PATTERN "find_inverse.cpp" EXCLUDE)

install(FILES ${Halide_SOURCE_DIR}/src/autoschedulers/adams2019/autotune_loop.sh
        DESTINATION ${Halide_INSTALL_TOOLSDIR}
        PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE
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

    if (WITH_PYTHON_BINDINGS)
      install(DIRECTORY ${Halide_SOURCE_DIR}/python_bindings/tutorial/
              DESTINATION ${CMAKE_INSTALL_DOCDIR}/tutorial-python
              COMPONENT Halide_Documentation
              FILES_MATCHING PATTERN "*.py")
    endif ()
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
if (Halide_SHARED_LLVM OR (NOT BUILD_SHARED_LIBS AND NOT Halide_BUNDLE_LLVM))
    set(depFile "${CMAKE_CURRENT_BINARY_DIR}/Halide-${type}-deps.cmake")
    file(WRITE "${depFile}" "find_dependency(LLVM ${LLVM_PACKAGE_VERSION})\n")

    if (TARGET_WEBASSEMBLY)
        file(APPEND "${depFile}" [[find_dependency(LLD HINTS "${LLVM_DIR}/../lld" "${LLVM_DIR}/../lib/cmake/lld")]])
    endif ()

    install(FILES "${depFile}"
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
        ${Halide_SOURCE_DIR}/cmake/HalideTargetHelpers.cmake
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

cpack_add_component(Halide_Documentation
                    DISPLAY_NAME "Halide documentation"
                    DESCRIPTION "Documentation for Halide")
