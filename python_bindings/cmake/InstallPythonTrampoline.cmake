function(_Halide_install_python_trampoline)
    cmake_parse_arguments(
        PARSE_ARGV 0 arg ""
        "PACKAGE;INSTALL_DIR;VERSION_FILE;OUTPUT_DIR;COMPONENT" ""
    )

    set(PACKAGE "${arg_PACKAGE}")
    set(INSTALL_DIR "${arg_INSTALL_DIR}")
    file(MAKE_DIRECTORY "${arg_OUTPUT_DIR}")
    configure_file(
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/PythonTrampolineConfig.cmake.in"
        "${arg_OUTPUT_DIR}/${PACKAGE}Config.cmake" @ONLY
    )

    install(
        FILES
            "${arg_OUTPUT_DIR}/${PACKAGE}Config.cmake"
            "${arg_VERSION_FILE}"
        DESTINATION "${SKBUILD_DATA_DIR}/share/cmake/${PACKAGE}"
        COMPONENT "${arg_COMPONENT}"
    )
endfunction()
