cmake_minimum_required(VERSION 3.28)
project(integration_tests CXX)

enable_testing()

##
# Single-linkage JIT integration tests.
##

# Prefer ldd (Linux); fall back to otool -L (macOS). Both list a binary's
# linked dynamic libraries by path, so a "libHalide" match works the same way
# for either. If neither is available, the check_linkage_* tests below are
# skipped rather than added as tests that can never run.
find_program(Halide_LDD_EXECUTABLE ldd)
find_program(Halide_OTOOL_EXECUTABLE otool)

if (Halide_LDD_EXECUTABLE)
    set(Halide_check_linkage_command "${Halide_LDD_EXECUTABLE}")
elseif (Halide_OTOOL_EXECUTABLE)
    set(Halide_check_linkage_command "${Halide_OTOOL_EXECUTABLE}" -L)
endif ()

foreach (bsl IN ITEMS "" "-DBUILD_SHARED_LIBS=NO" "-DBUILD_SHARED_LIBS=YES")
    foreach (hsl IN ITEMS "" "-DHalide_SHARED_LIBS=NO" "-DHalide_SHARED_LIBS=YES")
        foreach (comp IN ITEMS "" "-Djit_HALIDE_COMPONENTS=static" "-Djit_HALIDE_COMPONENTS=shared")
            # Compute whether we expect to link to static or shared Halide given the relevant variables.
            # Explicitly listing a component always wins. Then Halide_SHARED_LIBS takes over. If that's
            # not available, it consults BUILD_SHARED_LIBS. If that's not defined, it defaults to shared,
            # rather than static, because that's less likely to lead to pathologies with generators.
            if (comp MATCHES "shared")
                set(expect_shared TRUE)
            elseif (comp MATCHES "static")
                set(expect_shared FALSE)
            elseif (hsl MATCHES "YES")
                set(expect_shared TRUE)
            elseif (hsl MATCHES "NO")
                set(expect_shared FALSE)
            elseif (bsl MATCHES "YES")
                set(expect_shared TRUE)
            elseif (bsl MATCHES "NO")
                set(expect_shared FALSE)
            else ()
                set(expect_shared TRUE)
            endif ()

            set(test_name "${bsl} ${hsl} ${comp}")
            string(REPLACE "-D" "" test_name "${test_name}")
            string(STRIP "${test_name}" test_name)
            string(MAKE_C_IDENTIFIER "jit_${test_name}" test_name)

            set(build_step "check_builds_${test_name}")
            set(check_link_step "check_linkage_${test_name}")

            set(build_dir "${CMAKE_CURRENT_BINARY_DIR}/jit/${test_name}")

            # This builds and runs the tiny example app.
            add_test(
                NAME "${build_step}"
                COMMAND
                    ${CMAKE_CTEST_COMMAND}
                    --build-and-test "${CMAKE_CURRENT_LIST_DIR}/jit" "${build_dir}"
                    --build-generator Ninja
                    --build-options -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER} ${bsl} ${hsl} ${comp}
                    -DCMAKE_BUILD_TYPE=Release
                    --test-command ${CMAKE_CTEST_COMMAND} --output-on-failure
            )

            set_tests_properties("${build_step}" PROPERTIES FIXTURES_SETUP "${test_name}")

            if (Halide_check_linkage_command)
                # List the output binary's linked libraries. The pass/fail regexes are set below.
                add_test(
                    NAME "${check_link_step}"
                    COMMAND ${Halide_check_linkage_command} "${build_dir}/main"
                )

                # Make sure we don't check linkage before building...
                set_tests_properties(
                    "${check_link_step}"
                    PROPERTIES
                    FIXTURES_REQUIRED "${test_name}"
                )

                if (expect_shared)
                    set_tests_properties(
                        "${check_link_step}"
                        PROPERTIES
                        PASS_REGULAR_EXPRESSION "libHalide"
                    )
                else ()
                    set_tests_properties(
                        "${check_link_step}"
                        PROPERTIES
                        FAIL_REGULAR_EXPRESSION "libHalide"
                    )
                endif ()
            endif ()
        endforeach ()
    endforeach ()
endforeach ()

##
# AOT integration tests
##

add_test(
    NAME aot_shared_generator
    COMMAND
        ${CMAKE_CTEST_COMMAND}
        --build-and-test "${CMAKE_CURRENT_LIST_DIR}/aot" "${CMAKE_CURRENT_BINARY_DIR}/aot-shared"
        --build-generator Ninja
        --build-options -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER} -DCMAKE_BUILD_TYPE=Release
        --test-command ${CMAKE_CTEST_COMMAND} --output-on-failure
)

add_test(
    NAME aot_static_generator
    COMMAND
        ${CMAKE_CTEST_COMMAND}
        --build-and-test "${CMAKE_CURRENT_LIST_DIR}/aot" "${CMAKE_CURRENT_BINARY_DIR}/aot-static"
        --build-generator Ninja
        --build-options -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER} -DHalide_SHARED_LIBS=NO
        -DCMAKE_BUILD_TYPE=Release
        --test-command ${CMAKE_CTEST_COMMAND} --output-on-failure
)

add_test(
    NAME aot_shared_generator_adams2019
    COMMAND
        ${CMAKE_CTEST_COMMAND}
        --build-and-test "${CMAKE_CURRENT_LIST_DIR}/aot" "${CMAKE_CURRENT_BINARY_DIR}/aot-shared-auto"
        --build-generator Ninja
        --build-options -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER} -DCMAKE_BUILD_TYPE=Release
        -Daot_USE_AUTOSCHEDULER=YES
        --test-command ${CMAKE_CTEST_COMMAND} --output-on-failure
)

add_test(
    NAME aot_static_generator_adams2019
    COMMAND
        ${CMAKE_CTEST_COMMAND}
        --build-and-test "${CMAKE_CURRENT_LIST_DIR}/aot" "${CMAKE_CURRENT_BINARY_DIR}/aot-static-auto"
        --build-generator Ninja
        --build-options -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER} -DHalide_SHARED_LIBS=NO
        -DCMAKE_BUILD_TYPE=Release -Daot_USE_AUTOSCHEDULER=YES
        --test-command ${CMAKE_CTEST_COMMAND} --output-on-failure
)

# Cannot use autoscheduler with generators linked to STATIC Halide
set_tests_properties(
    aot_static_generator_adams2019
    PROPERTIES
    WILL_FAIL TRUE
    FAIL_REGULAR_EXPRESSION "Autoscheduler Halide::[A-Za-z0-9_]+ does not exist"
)

##
# Multi-directory static/shared independence tests
##

add_test(
    NAME multi_dir_independent_siblings
    COMMAND
        ${CMAKE_CTEST_COMMAND}
        --build-and-test "${CMAKE_CURRENT_LIST_DIR}/multi_dir" "${CMAKE_CURRENT_BINARY_DIR}/multi-dir"
        --build-generator Ninja
        --build-options -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER} -DCMAKE_BUILD_TYPE=Release
        --test-command ${CMAKE_CTEST_COMMAND} --output-on-failure
)

add_test(
    NAME multi_dir_conflict_fails_cleanly
    COMMAND
        ${CMAKE_CTEST_COMMAND}
        --build-and-test "${CMAKE_CURRENT_LIST_DIR}/multi_dir"
        "${CMAKE_CURRENT_BINARY_DIR}/multi-dir-conflict"
        --build-generator Ninja
        --build-options -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER} -DCMAKE_BUILD_TYPE=Release
        -Dmulti_dir_TEST_CONFLICT=YES
        --test-command ${CMAKE_CTEST_COMMAND} --output-on-failure
)
set_tests_properties(
    multi_dir_conflict_fails_cleanly
    PROPERTIES
    WILL_FAIL TRUE
    FAIL_REGULAR_EXPRESSION "already loaded by an ancestor directory"
)

##
# Cross compiling test
##

if (CMAKE_HOST_SYSTEM_NAME MATCHES "Linux")
    add_test(
        NAME cross_compile_host
        COMMAND
            ${CMAKE_CTEST_COMMAND}
            --build-and-test "${CMAKE_CURRENT_LIST_DIR}/xc" "${CMAKE_CURRENT_BINARY_DIR}/xc-host"
            --build-generator Ninja
            --build-options -DCMAKE_BUILD_TYPE=Release
            --test-command ${CMAKE_CTEST_COMMAND} --output-on-failure
    )

    add_test(
        NAME cross_compile_aarch64
        COMMAND
            ${CMAKE_CTEST_COMMAND}
            --build-and-test "${CMAKE_CURRENT_LIST_DIR}/xc" "${CMAKE_CURRENT_BINARY_DIR}/xc-aarch64"
            --build-generator Ninja
            --build-options
            -DCMAKE_TOOLCHAIN_FILE=${CMAKE_CURRENT_LIST_DIR}/../../cmake/toolchain.linux-aarch64.cmake
            -DCMAKE_BUILD_TYPE=Release
            "-Dxc-halide_generators_ROOT=${CMAKE_CURRENT_BINARY_DIR}/xc-host"
            --test-command ${CMAKE_CTEST_COMMAND} --output-on-failure
    )

    set_tests_properties(cross_compile_host PROPERTIES FIXTURES_SETUP xc-host)
    set_tests_properties(cross_compile_aarch64 PROPERTIES FIXTURES_REQUIRED xc-host)

    # Verifies that explicitly forcing the JIT component while cross-compiling
    # (find_package(Halide REQUIRED COMPONENTS JIT)) fails cleanly via a normal
    # find_package error when no matching HalideCompiler package exists for the
    # target, rather than silently finding a mismatched one. Requires a prefix
    # containing only the Halide (helpers) package and no HalideCompiler; left
    # empty, this test is skipped, since building that prefix requires extra CI
    # setup that isn't expected of a typical local dev configure.
    set(xc_HELPERS_ONLY_PREFIX ""
        CACHE PATH
        "A CMAKE_PREFIX_PATH entry containing only the installed Halide (helpers) \
package and no HalideCompiler. See cross_compile_aarch64_force_jit_fails_cleanly."
    )

    if (xc_HELPERS_ONLY_PREFIX)
        add_test(
            NAME cross_compile_aarch64_force_jit_fails_cleanly
            COMMAND
                ${CMAKE_CTEST_COMMAND}
                --build-and-test "${CMAKE_CURRENT_LIST_DIR}/xc"
                "${CMAKE_CURRENT_BINARY_DIR}/xc-aarch64-force-jit"
                --build-generator Ninja
                --build-options
                -DCMAKE_TOOLCHAIN_FILE=${CMAKE_CURRENT_LIST_DIR}/../../cmake/toolchain.linux-aarch64.cmake
                -DCMAKE_BUILD_TYPE=Release
                -Dxc_FORCE_JIT=YES
                --test-command ${CMAKE_CTEST_COMMAND} --output-on-failure
        )
        set_tests_properties(
            cross_compile_aarch64_force_jit_fails_cleanly
            PROPERTIES
            # Override, not append: the ambient CMAKE_PREFIX_PATH (used by every
            # other test in this file) points at a prefix that also has a
            # (host-arch) HalideCompiler installed, which would otherwise still
            # be found here and defeat the point of this test.
            ENVIRONMENT "CMAKE_PREFIX_PATH=${xc_HELPERS_ONLY_PREFIX}"
            WILL_FAIL TRUE
            FAIL_REGULAR_EXPRESSION
                "Could not find a package configuration file provided by \"HalideCompiler\""
        )
    endif ()
endif ()
