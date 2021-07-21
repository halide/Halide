# This is directly mimicking the approach used by jsvu.
# See here: https://github.com/GoogleChromeLabs/jsvu/tree/main/engines/v8

find_program(D8_EXECUTABLE d8)

if (NOT D8_EXECUTABLE)
    set(wasm_Windows win64)
    set(wasm_Darwin mac64)
    set(wasm_Linux linux64)

    set(wasm_platform "${wasm_${CMAKE_HOST_SYSTEM_NAME}}")

    # Download the exact version requested
    set(wasm_version "${${CMAKE_FIND_PACKAGE_NAME}_FIND_VERSION}")
    FetchContent_Declare(
            wasm_shell
            URL "https://storage.googleapis.com/chromium-v8/official/canary/v8-${wasm_platform}-rel-${wasm_version}.zip"
    )
    FetchContent_MakeAvailable(wasm_shell)

    find_program(D8_EXECUTABLE d8 HINTS "${wasm_shell_SOURCE_DIR}")
endif ()

if (D8_EXECUTABLE)
    add_executable(D8::d8 IMPORTED)
    set_target_properties(D8::d8 PROPERTIES IMPORTED_LOCATION "${D8_EXECUTABLE}")

    # D8 does not have any other command line interface for getting the version number.
    execute_process(COMMAND ${D8_EXECUTABLE} -e "console.log(version())"
                    OUTPUT_VARIABLE D8_VERSION
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
endif ()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
        D8
        REQUIRED_VARS D8_EXECUTABLE
        VERSION_VAR D8_VERSION
)
