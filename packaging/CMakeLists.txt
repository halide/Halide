include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

##
# Custom locations
##

set(Halide_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/Halide"
    CACHE STRING "Path to Halide CMake files"
)

set(Halide_INSTALL_COMPILERDIR "${CMAKE_INSTALL_LIBDIR}/cmake/HalideCompiler"
    CACHE STRING "Path to Halide's platform-specific compiled-library CMake files"
)

set(Halide_INSTALL_AUTOSCHEDULERSDIR "${CMAKE_INSTALL_LIBDIR}/cmake/HalideAutoschedulers"
    CACHE STRING "Path to Halide's autoscheduler plugin CMake files"
)

set(Halide_INSTALL_PLUGINDIR "${CMAKE_INSTALL_LIBDIR}" CACHE STRING "Path to Halide plugins folder")

set(Halide_INSTALL_TOOLSDIR "${CMAKE_INSTALL_DATADIR}/tools"
    CACHE STRING "Path to Halide build-time tools and sources"
)

set(Halide_INSTALL_DOXYGENDIR "${CMAKE_INSTALL_DOCDIR}/html"
    CACHE STRING "Path to Halide's generated documentation website (API reference and tutorial)"
)

##
# RPATH patching helper
##

function(_Halide_compute_rpath)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "ORIGIN_DIR;LIB_DIR" "TARGETS")
    if (APPLE)
        set(rbase @loader_path)
    else ()
        set(rbase $ORIGIN)
    endif ()

    file(RELATIVE_PATH lib_dir ${CMAKE_CURRENT_BINARY_DIR}/${ARG_ORIGIN_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/${ARG_LIB_DIR}
    )

    set_target_properties(${ARG_TARGETS} PROPERTIES INSTALL_RPATH "${rbase};${rbase}/${lib_dir}")
endfunction()

##
# Main library exports
##

install(
    TARGETS Halide Halide_Generator Halide_GenGen
    EXPORT HalideCompiler_Targets
    RUNTIME COMPONENT Halide_Runtime
    LIBRARY COMPONENT Halide_Runtime NAMELINK_COMPONENT Halide_Development
    ARCHIVE COMPONENT Halide_Development
    FILE_SET HEADERS COMPONENT Halide_Development
)

if (WITH_AUTOSCHEDULERS)
    set(autoschedulers Halide_Adams2019 Halide_Li2018 Halide_Mullapudi2016 Halide_Anderson2021)

    # Autoscheduler plugins are compiled, platform-specific shared libraries,
    # but they are dlopen()'d by the generator process at Generator run time
    # and never linked, so they get their own export set/package
    # (HalideAutoschedulers) instead of joining HalideCompiler's. See the
    # ARCH_INDEPENDENT comment below for why that distinction matters.
    install(
        TARGETS ${autoschedulers}
        EXPORT HalideAutoschedulers_Targets
        LIBRARY
        DESTINATION ${Halide_INSTALL_PLUGINDIR}
        COMPONENT Halide_Runtime
        NAMELINK_COMPONENT Halide_Development
    )

    if (NOT CMAKE_INSTALL_RPATH)
        _Halide_compute_rpath(
            TARGETS ${autoschedulers}
            ORIGIN_DIR "${Halide_INSTALL_PLUGINDIR}"
            LIB_DIR "${CMAKE_INSTALL_LIBDIR}"
        )
    endif ()
endif ()

##
# Runtime headers
##

install(TARGETS Halide_Runtime EXPORT Halide_Targets FILE_SET HEADERS COMPONENT Halide_Development)

# The shared buffer-marshalling core, #included both by generated Python
# extensions (embedded via binary2cpp) and compiled into the standalone
# halide.runtime module. Install it next to HalideRuntime.h so an out-of-tree
# build of the runtime module finds it via Halide::Runtime's include dirs.
install(
    FILES "${Halide_SOURCE_DIR}/src/PythonExtensionRuntime.template.cpp"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    COMPONENT Halide_Development
)

##
# Halide tools
##

target_sources(
    Halide_RunGenMain
    INTERFACE $<INSTALL_INTERFACE:${Halide_INSTALL_TOOLSDIR}/RunGenMain.cpp>
)

install(
    FILES ${Halide_SOURCE_DIR}/tools/RunGenMain.cpp
    DESTINATION ${Halide_INSTALL_TOOLSDIR}
    COMPONENT Halide_Development
)

install(
    TARGETS Halide_Tools Halide_ImageIO Halide_RunGenMain Halide_ThreadPool
    EXPORT Halide_Targets
    FILE_SET HEADERS COMPONENT Halide_Development DESTINATION ${Halide_INSTALL_TOOLSDIR}
)

##
# Install command-line utils
##

set(utils Halide_GenRT)

if (WITH_AUTOSCHEDULERS AND WITH_UTILS)
    list(APPEND utils
        adams2019_retrain_cost_model
        adams2019_weightsdir_to_weightsfile
        anderson2021_retrain_cost_model
        anderson2021_weightsdir_to_weightsfile
        featurization_to_sample
        get_host_target
    )
endif ()

if (NOT CMAKE_INSTALL_RPATH)
    _Halide_compute_rpath(
        TARGETS ${utils}
        ORIGIN_DIR "${CMAKE_INSTALL_BINDIR}"
        LIB_DIR "${CMAKE_INSTALL_LIBDIR}"
    )
endif ()

install(TARGETS ${utils} EXPORT Halide_Targets COMPONENT Halide_Development)

##
# READMEs and other top-level documentation
##

install(
    FILES
        ${Halide_SOURCE_DIR}/README.md
        ${Halide_SOURCE_DIR}/LICENSE.txt
    COMPONENT Halide_Documentation
    TYPE DOC
)

install(
    DIRECTORY "${Halide_SOURCE_DIR}/doc"
    COMPONENT Halide_Documentation
    TYPE DOC
    FILES_MATCHING
    PATTERN "*.md"
)

##
# Tools
##

install(
    PROGRAMS
        ${Halide_SOURCE_DIR}/src/autoschedulers/adams2019/adams2019_autotune_loop.sh
        ${Halide_SOURCE_DIR}/src/autoschedulers/anderson2021/anderson2021_autotune_loop.sh
    DESTINATION ${Halide_INSTALL_TOOLSDIR}
    COMPONENT Halide_Development
)

##
# Tutorial
##

if (WITH_TUTORIALS)
    install(
        DIRECTORY ${Halide_SOURCE_DIR}/tutorial
        TYPE DOC
        COMPONENT Halide_Documentation
        FILES_MATCHING
        PATTERN "*.cpp"
        PATTERN "*.h"
        PATTERN "lesson_*.sh"
        PATTERN "*.gif"
        PATTERN "*.jpg"
        PATTERN "*.mp4"
        PATTERN "*.png"
    )
endif ()

##
# CMake scripts
##

if (BUILD_SHARED_LIBS)
    set(type shared)
else ()
    set(type static)
endif ()

install(
    FILES
        "${Halide_SOURCE_DIR}/cmake/FindHalide_LLVM.cmake"
        "${Halide_SOURCE_DIR}/cmake/FindV8.cmake"
    DESTINATION ${Halide_INSTALL_COMPILERDIR}
    COMPONENT Halide_Development
)

install(
    EXPORT HalideCompiler_Targets
    DESTINATION ${Halide_INSTALL_COMPILERDIR}
    NAMESPACE Halide::
    FILE HalideCompiler-${type}-targets.cmake
    COMPONENT Halide_Development
)

install(
    EXPORT Halide_Targets
    DESTINATION ${Halide_INSTALL_CMAKEDIR}
    NAMESPACE Halide::
    FILE Halide-targets.cmake
    COMPONENT Halide_Development
)

if (WITH_AUTOSCHEDULERS)
    install(
        EXPORT HalideAutoschedulers_Targets
        DESTINATION ${Halide_INSTALL_AUTOSCHEDULERSDIR}
        NAMESPACE Halide::
        FILE HalideAutoschedulers-targets.cmake
        COMPONENT Halide_Development
    )
endif ()

write_basic_package_version_file(
    HalideConfigVersion.cmake
    COMPATIBILITY SameMajorVersion
    ARCH_INDEPENDENT
)

write_basic_package_version_file(HalideCompilerConfigVersion.cmake COMPATIBILITY SameMajorVersion)

if (WITH_AUTOSCHEDULERS)
    # Autoscheduler plugins are real compiled, platform-specific binaries, so
    # this ARCH_INDEPENDENT is a deliberate abuse of the flag, not an oversight
    # (contrast HalideCompilerConfigVersion.cmake just above, which is
    # correctly *not* ARCH_INDEPENDENT for the same kind of compiled target).
    # It's safe here specifically because nothing ever links against
    # Halide::<Scheduler> -- add_halide_library(... AUTOSCHEDULER ...) only
    # ever reads its $<TARGET_FILE:...> to pass to the generator executable's
    # `-p` (plugin) command-line flag, which then dlopen()s it at Generator
    # run time. That means this package's version check never needs to agree
    # with the *consuming* project's word size, which is the one that
    # matters when a baremetal/embedded toolchain file is in play -- e.g.
    # CMAKE_SIZEOF_VOID_P reflects the cross-compiler's target ABI, not the
    # host machine's, when CMAKE_CROSSCOMPILING is set.
    write_basic_package_version_file(
        HalideAutoschedulersConfigVersion.cmake
        COMPATIBILITY SameMajorVersion
        ARCH_INDEPENDENT
    )
endif ()

if (WITH_PYTHON_BINDINGS)
    set(extra_paths Halide_Python_INSTALL_CMAKEDIR)
else ()
    set(extra_paths "")
endif ()

configure_package_config_file(
    HalideConfig.cmake HalideConfig.cmake
    PATH_VARS Halide_INSTALL_COMPILERDIR
    INSTALL_DESTINATION "${Halide_INSTALL_CMAKEDIR}"
    NO_SET_AND_CHECK_MACRO NO_CHECK_REQUIRED_COMPONENTS_MACRO
)

configure_package_config_file(
    HalideCompilerConfig.cmake HalideCompilerConfig.cmake
    PATH_VARS ${extra_paths}
    INSTALL_DESTINATION "${Halide_INSTALL_COMPILERDIR}"
    NO_SET_AND_CHECK_MACRO NO_CHECK_REQUIRED_COMPONENTS_MACRO
)

if (WITH_AUTOSCHEDULERS)
    configure_package_config_file(
        HalideAutoschedulersConfig.cmake HalideAutoschedulersConfig.cmake
        INSTALL_DESTINATION "${Halide_INSTALL_AUTOSCHEDULERSDIR}"
        NO_SET_AND_CHECK_MACRO NO_CHECK_REQUIRED_COMPONENTS_MACRO
    )
endif ()

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/HalideConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/HalideConfigVersion.cmake
        ${Halide_SOURCE_DIR}/cmake/HalideGeneratorHelpers.cmake
        ${Halide_SOURCE_DIR}/cmake/FindHalide_WebGPU.cmake
        ${Halide_SOURCE_DIR}/cmake/HalideTargetHelpers.cmake
        ${Halide_SOURCE_DIR}/cmake/TargetExportScript.cmake
        ${Halide_SOURCE_DIR}/cmake/MutexCopy.ps1
    DESTINATION ${Halide_INSTALL_CMAKEDIR}
    COMPONENT Halide_Development
)

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/HalideCompilerConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/HalideCompilerConfigVersion.cmake
    DESTINATION ${Halide_INSTALL_COMPILERDIR}
    COMPONENT Halide_Development
)

if (WITH_AUTOSCHEDULERS)
    install(
        FILES
            ${CMAKE_CURRENT_BINARY_DIR}/HalideAutoschedulersConfig.cmake
            ${CMAKE_CURRENT_BINARY_DIR}/HalideAutoschedulersConfigVersion.cmake
        DESTINATION ${Halide_INSTALL_AUTOSCHEDULERSDIR}
        COMPONENT Halide_Development
    )
endif ()

##
# Compute find_dependency calls for HalideCompiler
##

_Halide_install_pkgdeps(
    FILE_NAME HalideCompiler-${type}-deps.cmake
    EXPORT_FILE HalideCompiler-${type}-targets.cmake
    DESTINATION "${Halide_INSTALL_COMPILERDIR}"
    COMPONENT Halide_Development
)

##
# Documentation
##

if (WITH_DOCS)
    install(
        DIRECTORY ${Halide_BINARY_DIR}/doc/sphinx/html/
        DESTINATION ${Halide_INSTALL_DOXYGENDIR}
        COMPONENT Halide_Doxygen
    )
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
set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_CURRENT_LIST_DIR}/Description.txt")
set(CPACK_RESOURCE_FILE_WELCOME "${CMAKE_CURRENT_LIST_DIR}/Welcome.txt")
set(CPACK_RESOURCE_FILE_LICENSE "${Halide_SOURCE_DIR}/LICENSE.txt")
set(CPACK_RESOURCE_FILE_README "${Halide_SOURCE_DIR}/README.md")

if (NOT CPACK_PACKAGE_FILE_NAME)
    set(arch_tag "${Halide_CMAKE_TARGET}")
    list(REMOVE_DUPLICATES arch_tag)
    list(SORT arch_tag)
    if (arch_tag MATCHES "arm-64-osx;x86-64-osx")
        set(arch_tag "universal2")
    endif ()
    string(REPLACE ";" "_" arch_tag "${arch_tag}")
    set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${Halide_VERSION}-${arch_tag}")
endif ()

include(CPack)

##
# Configure components
##

cpack_add_component(
    Halide_Runtime
    DISPLAY_NAME "Halide"
    DESCRIPTION "Runtime files for libHalide and autoschedulers"
)

cpack_add_component(
    Halide_Development
    DISPLAY_NAME "Halide development"
    DESCRIPTION "Static Halide libraries and CMake development files"
    DEPENDS Halide_Runtime
)

cpack_add_component(
    Halide_Python
    DISPLAY_NAME "Python bindings"
    DESCRIPTION "Python package providing bindings to Halide"
    DEPENDS Halide_Runtime
)

cpack_add_component(
    Halide_PythonRuntime
    DISPLAY_NAME "Python runtime bindings"
    DESCRIPTION "Python bindings for Halide runtime types and AOT modules"
)

cpack_add_component(
    Halide_Documentation
    DISPLAY_NAME "Halide documentation"
    DESCRIPTION "Documentation for Halide"
)

cpack_add_component(
    Halide_Doxygen
    DISPLAY_NAME "Halide documentation website"
    DESCRIPTION "Generated Sphinx documentation website, including the API reference and tutorial (WITH_DOCS)"
)
