cmake_minimum_required(VERSION 3.16)
project(xc LANGUAGES NONE)

enable_testing()

if (NOT CMAKE_CROSSCOMPILING)
    # There is a bug with CTest's build-and-test that forwards environment
    # variables, such as those set by the toolchain, to child processes. By
    # not enabling CXX in the cross-compiling scenario, we can work around
    # this bug. The alternative would be to not use build-and-test in the
    # testing code, but it's used in every other location. This approach
    # also has the benefit of being marginally faster because it doesn't
    # waste a few seconds detecting a compiler that won't be used.
    # https://gitlab.kitware.com/cmake/cmake/-/issues/22043
    enable_language(CXX)

    # Do things the easy way when not cross compiling
    add_subdirectory(generators)
    add_subdirectory(add)
else ()
    # When cross compiling, use ExternalProject to stage building the
    # generators with a host toolchain before passing the resulting
    # package to the library build, which will use the target toolchain.
    include(ExternalProject)

    set(xc_HOST_TOOLCHAIN_FILE ""
        CACHE FILEPATH "Toolchain file to use when compiling generators")

    ExternalProject_Add(
            generators
            SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generators"
            INSTALL_COMMAND ""
            BUILD_ALWAYS YES
            CMAKE_ARGS
            -DCMAKE_TOOLCHAIN_FILE=${xc_HOST_TOOLCHAIN_FILE}
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    )

    ExternalProject_Get_Property(generators BINARY_DIR)

    ExternalProject_Add(
            add
            SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/add"
            INSTALL_COMMAND ""
            BUILD_ALWAYS YES
            CMAKE_ARGS
            -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -Dxc-generators_ROOT=${BINARY_DIR}
    )

    ExternalProject_Add_StepDependencies(add configure generators)

    ExternalProject_Get_Property(add BINARY_DIR)

    add_test(NAME run-tests
             COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
             WORKING_DIRECTORY ${BINARY_DIR})
endif ()
