##
# Tests for the debugger-integration helpers in tools/ (lldbhalide.py,
# gdbhalide.py, Halide.natvis). The LLDB/GDB tests drive the real debugger in
# batch mode against a small fixture and check that each value pretty-prints as
# expected. They only run when the debugger is installed *and* libHalide has
# debug info; the harness prints "[SKIP]" (honored below via
# SKIP_REGULAR_EXPRESSION) when it cannot evaluate debug_string() in the
# inferior, which is the case for a Release build.
##

# The natvis XML check needs no debugger; the LLDB/GDB tests need a Python
# interpreter to run the harness.
find_package(Python3 COMPONENTS Interpreter QUIET)

set(_tools_dir "${Halide_SOURCE_DIR}/tools")
set(_harness "${CMAKE_CURRENT_SOURCE_DIR}/run_debugger_test.py")

# Validate that the Natvis file is at least well-formed XML. Rendering still has
# to be checked by hand in Visual Studio / CLion, but a malformed visualizer
# should fail CI.
if (Python3_Interpreter_FOUND)
    add_test(
        NAME debugger_natvis_xml
        COMMAND
            ${Python3_EXECUTABLE} -c
            "import sys, xml.etree.ElementTree as ET; ET.parse(sys.argv[1])"
            "${_tools_dir}/Halide.natvis"
    )
    set_tests_properties(debugger_natvis_xml PROPERTIES LABELS debugger)
endif ()

find_program(LLDB_EXECUTABLE lldb)
find_program(GDB_EXECUTABLE gdb)

# The fixture is only needed if a debugger is available to drive it. Build it
# unoptimized and with debug info so breakpoints and expression evaluation are
# reliable regardless of the overall CMAKE_BUILD_TYPE.
if (Python3_Interpreter_FOUND AND (LLDB_EXECUTABLE OR GDB_EXECUTABLE))
    add_executable(debugger_fixture debugger_fixture.cpp)
    target_link_libraries(debugger_fixture PRIVATE Halide::Halide)
    target_include_directories(debugger_fixture PRIVATE "${Halide_SOURCE_DIR}/src")
    target_compile_options(
        debugger_fixture
        PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-g;-O0;-fno-omit-frame-pointer>
    )

    function(add_debugger_test name debugger executable)
        add_test(
            NAME ${name}
            COMMAND
                ${Python3_EXECUTABLE} "${_harness}"
                --debugger ${debugger}
                --debugger-path "${executable}"
                --fixture "$<TARGET_FILE:debugger_fixture>"
                --tools-dir "${_tools_dir}"
        )
        set_tests_properties(
            ${name}
            PROPERTIES
            LABELS debugger
            SKIP_REGULAR_EXPRESSION "\\[SKIP\\]"
        )
    endfunction ()

    # LLDB's Windows support does not reliably evaluate expressions against
    # MSVC-emitted PDB debug info, so skip it there even if an lldb.exe
    # happens to be on PATH (e.g. the ambient one on GitHub Actions runners).
    if (LLDB_EXECUTABLE AND NOT (WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC"))
        add_debugger_test(debugger_lldb lldb "${LLDB_EXECUTABLE}")
    endif ()
    if (GDB_EXECUTABLE)
        add_debugger_test(debugger_gdb gdb "${GDB_EXECUTABLE}")
    endif ()
endif ()
