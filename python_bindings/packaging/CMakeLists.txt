include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(Halide_INSTALL_PYTHONDIR "${CMAKE_INSTALL_LIBDIR}/python3/site-packages/halide"
    CACHE STRING "Path to the Python site-packages folder")

if (WITH_PYTHON_BINDINGS)
    install(DIRECTORY "${Halide_Python_SOURCE_DIR}/src/halide/"
            DESTINATION "${Halide_INSTALL_PYTHONDIR}"
            COMPONENT Halide_Python
            FILES_MATCHING
            PATTERN "*.py"
            PATTERN "halide_" EXCLUDE)

    install(TARGETS Halide_Python
            EXPORT Halide_Python-targets
            LIBRARY DESTINATION "${Halide_INSTALL_PYTHONDIR}"
            COMPONENT Halide_Python)

    get_property(halide_is_imported TARGET Halide::Halide PROPERTY IMPORTED)
    get_property(halide_type TARGET Halide::Halide PROPERTY TYPE)

    if (
        NOT CMAKE_INSTALL_RPATH  # Honor user overrides
        AND NOT halide_is_imported  # Imported Halide means user is responsible for RPATH
        AND halide_type STREQUAL "SHARED_LIBRARY"  # No need to set RPATH if statically linked
    )
        if (APPLE)
            set(rbase @loader_path)
        else ()
            set(rbase $ORIGIN)
        endif ()

        file(RELATIVE_PATH lib_dir
             "${CMAKE_CURRENT_BINARY_DIR}/${Halide_INSTALL_PYTHONDIR}"
             "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}")

        set_target_properties(Halide_Python PROPERTIES INSTALL_RPATH "${rbase}/${lib_dir}")
    endif ()
endif ()

if (WITH_PYTHON_STUBS)
    install(TARGETS Halide_PyStubs
            EXPORT Halide_Python-targets
            COMPONENT Halide_Python)
endif ()

if (WITH_TUTORIALS)
    install(DIRECTORY ${Halide_Python_SOURCE_DIR}/tutorial/
            DESTINATION ${CMAKE_INSTALL_DOCDIR}/tutorial-python
            COMPONENT Halide_Documentation
            FILES_MATCHING PATTERN "*.py")
endif ()

if (WITH_PYTHON_BINDINGS OR WITH_PYTHON_STUBS)
    set(Halide_Python_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/Halide_Python"
        CACHE STRING "Path to Halide_Python CMake files")

    install(EXPORT Halide_Python-targets
            DESTINATION ${Halide_Python_INSTALL_CMAKEDIR}
            NAMESPACE Halide::
            COMPONENT Halide_Python)

    configure_package_config_file(
        Halide_PythonConfig.cmake.in Halide_PythonConfig.cmake
        INSTALL_DESTINATION "${Halide_Python_INSTALL_CMAKEDIR}"
        NO_SET_AND_CHECK_MACRO
    )

    write_basic_package_version_file(
        Halide_PythonConfigVersion.cmake COMPATIBILITY SameMajorVersion
    )

    install(
        FILES
        "${CMAKE_CURRENT_BINARY_DIR}/Halide_PythonConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/Halide_PythonConfigVersion.cmake"
        DESTINATION "${Halide_Python_INSTALL_CMAKEDIR}"
        COMPONENT Halide_Python
    )
endif ()
