if (NOT TARGET Halide::Li2018)
    message(STATUS "Disabling li2018 tests for static Halide")
    return()
endif ()

add_halide_generator(li2018_demo_gradient.generator
                     SOURCES demo_generator.cpp)

add_halide_library(li2018_demo_gradient FROM li2018_demo_gradient.generator
                   TARGETS cmake
                   GENERATOR demo
                   FUNCTION_NAME demo
                   AUTOSCHEDULER Halide::Li2018
                   REGISTRATION DEMO_REGISTRATION_FILE)

add_executable(li2018_demo_gradient_autoscheduler ${DEMO_REGISTRATION_FILE})
target_link_libraries(li2018_demo_gradient_autoscheduler PRIVATE li2018_demo_gradient Halide::RunGenMain)

# demo_generator.cpp
add_halide_test(li2018_demo_gradient_autoscheduler
                COMMAND li2018_demo_gradient_autoscheduler --benchmarks=all --benchmark_min_time=1 --estimate_all --success
                GROUPS li2018 autoschedulers_cpu autoschedulers_gpu auto_schedule multithreaded)

tests(GROUPS li2018 autoschedulers_cpu autoschedulers_gpu auto_schedule
      SOURCES
      test.cpp
      ARGS $<TARGET_FILE:Halide::Li2018>)
add_dependencies(li2018_test Halide::Li2018)

if (WITH_PYTHON_BINDINGS)
    if (TARGET_WEBASSEMBLY AND Halide_TARGET MATCHES "webgpu")
        message(WARNING "li2018_gradient_autoscheduler_test_py is not supported with WebGPU.")
    else()
        find_package(Python3 REQUIRED COMPONENTS Interpreter Development.Module)

        add_test(
            NAME li2018_gradient_autoscheduler_test_py
            COMMAND ${Halide_PYTHON_LAUNCHER} "$<TARGET_FILE:Python3::Interpreter>" "${CMAKE_CURRENT_SOURCE_DIR}/test.py" $<TARGET_FILE:Halide::Li2018>
        )

        set(PYTHONPATH "$<TARGET_FILE_DIR:Halide::Python>/..")
        list(TRANSFORM PYTHONPATH PREPEND "PYTHONPATH=path_list_prepend:")

        set_tests_properties(li2018_gradient_autoscheduler_test_py PROPERTIES
                             DEPENDS li2018_test
                             LABELS "li2018;autoschedulers_cpu;autoschedulers_gpu;auto_schedule"
                             ENVIRONMENT_MODIFICATION "${PYTHONPATH}")
    endif()
endif ()
