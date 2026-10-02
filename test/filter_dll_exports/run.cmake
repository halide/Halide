# Runs cmake/FilterDllExports.cmake on input.def and compares against expected.def.
# Usage: cmake -DSCRIPT=<FilterDllExports.cmake> -DDIR=<this dir> -DOUT=<scratch file> -P run.cmake
execute_process(
    COMMAND
        "${CMAKE_COMMAND}" --log-level=VERBOSE "-DIN=${DIR}/input.def" "-DOUT=${OUT}" -P "${SCRIPT}"
    RESULT_VARIABLE rc
)
if (NOT rc EQUAL 0)
    message(FATAL_ERROR "FilterDllExports.cmake failed: ${rc}")
endif ()
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${DIR}/expected.def" "${OUT}"
    RESULT_VARIABLE rc
)
if (NOT rc EQUAL 0)
    message(FATAL_ERROR "FilterDllExports.cmake output differs from expected.def")
endif ()
