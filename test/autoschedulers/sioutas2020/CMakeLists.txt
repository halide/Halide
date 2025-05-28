if (NOT "NVPTX" IN_LIST Halide_LLVM_COMPONENTS)
    message(STATUS "Not testing sioutas2020 because PTX is not enabled for this build of Halide")
    return()
endif ()

##
# Tests and demos
##

add_executable(sioutas2020_test_apps_autoscheduler test.cpp)
target_link_libraries(
    sioutas2020_test_apps_autoscheduler
    PRIVATE
    Halide::Test
    Halide::Tools
    ${CMAKE_DL_LIBS}
)
add_dependencies(sioutas2020_test_apps_autoscheduler Halide::Sioutas2020)

add_test(
    NAME sioutas2020_test_apps_autoscheduler
    COMMAND sioutas2020_test_apps_autoscheduler $<TARGET_FILE:Halide_Sioutas2020>
)
set_tests_properties(
    sioutas2020_test_apps_autoscheduler
    PROPERTIES
    LABELS "sioutas2020;autoschedulers_cuda;multithreaded;rfactor"
    ENVIRONMENT_MODIFICATION "LD_LIBRARY_PATH=path_list_prepend:$<TARGET_FILE_DIR:Halide_Sioutas2020>"
)
