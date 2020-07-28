##
# Test apps from the perspective of a consuming project.
##

# Set up a test fixture for installing the main project into a build directory

set(HALIDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/_Halide_install")

add_test(NAME delete_halide_distrib
         COMMAND ${CMAKE_COMMAND} -E remove_directory "${HALIDE_DIR}")

set_tests_properties(delete_halide_distrib PROPERTIES
                     FIXTURES_SETUP apps)

add_test(NAME create_halide_distrib
         COMMAND ${CMAKE_COMMAND} --install . --config $<CONFIG> --prefix "${HALIDE_DIR}"
         WORKING_DIRECTORY ${Halide_BINARY_DIR})

set_tests_properties(create_halide_distrib PROPERTIES
                     FIXTURES_SETUP apps
                     DEPENDS delete_halide_distrib)

function(add_app_test NAME)
    if (TARGET_WEBASSEMBLY AND Halide_TARGET MATCHES "wasm")
        # Don't attempt to build these for wasm yet.
        return()
    endif ()
    
    unset(cmakeToolchainOpts)
    if (NOT "${CMAKE_TOOLCHAIN_FILE}" STREQUAL "")
        list(APPEND cmakeToolchainOpts "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
        if (NOT "${VCPKG_CHAINLOAD_TOOLCHAIN_FILE}" STREQUAL "")
            list(APPEND cmakeToolchainOpts "-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=${VCPKG_CHAINLOAD_TOOLCHAIN_FILE}")
        endif ()
    endif ()

    unset(cmakeGenOpts)
    if (NOT "${CMAKE_GENERATOR_TOOLSET}" STREQUAL "")
        list(APPEND cmakeGenOpts --build-generator-toolset "${CMAKE_GENERATOR_TOOLSET}")
    endif ()
    if (NOT "${CMAKE_GENERATOR_PLATFORM}" STREQUAL "")
        list(APPEND cmakeGenOpts --build-generator-platform "${CMAKE_GENERATOR_PLATFORM}")
    endif ()

    set(cmakeDefinitionOpts
        # TODO: enable verbose here temporarily, to try to track down flaky Windows failures
        "-DCMAKE_VERBOSE_MAKEFILE=ON"
        "-DCMAKE_PREFIX_PATH=${HALIDE_DIR}"
        "-DCMAKE_BUILD_TYPE=$<CONFIG>"
        "-DLLVM_DIR=${LLVM_DIR}")
    if (NOT "${Halide_TARGET}" STREQUAL "")
        list(APPEND cmakeDefinitionOpts "-DHalide_TARGET=${Halide_TARGET}")
    endif ()

    add_test(NAME ${NAME}
             COMMAND ${CMAKE_CTEST_COMMAND}
             --output-on-failure
             --build-and-test "${CMAKE_CURRENT_SOURCE_DIR}/${NAME}" "${CMAKE_CURRENT_BINARY_DIR}/app_test_${NAME}"
             --build-generator "${CMAKE_GENERATOR}"
             ${cmakeGenOpts}
             --build-config "$<CONFIG>"
             --build-options
             ${cmakeDefinitionOpts}
             ${cmakeToolchainOpts}
             --test-command ${CMAKE_CTEST_COMMAND} -C $<CONFIG> -V)
    set_tests_properties(${NAME} PROPERTIES
                         FIXTURES_REQUIRED apps
                         LABELS apps
                         ENVIRONMENT "HL_TARGET=${Halide_TARGET}"
                         SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")
endfunction()

# Actually add the apps as tests
add_app_test(bgu)
add_app_test(bilateral_grid)
add_app_test(blur)
add_app_test(c_backend)
add_app_test(camera_pipe)
add_app_test(conv_layer)
add_app_test(cuda_mat_mul)
#add_app_test(glsl)  # TODO(#4937): bugged; not built by Makefile
add_app_test(harris)
add_app_test(hist)
add_app_test(iir_blur)
add_app_test(interpolate)
add_app_test(lens_blur)
add_app_test(linear_algebra)
add_app_test(local_laplacian)
add_app_test(max_filter)
add_app_test(nl_means)
add_app_test(resize)
add_app_test(stencil_chain)
add_app_test(unsharp)
add_app_test(wavelet)

# Add the autoschedulers.
# TODO(#4053): refactor into separate modules
if (TARGET_WEBASSEMBLY AND Halide_TARGET MATCHES "wasm")
    # Don't attempt to build these for wasm yet.
else ()
    add_subdirectory(autoscheduler)
    add_subdirectory(gradient_autoscheduler)
endif ()
