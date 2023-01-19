cmake_minimum_required(VERSION 3.22)

project(HelloBaremetal)

enable_testing()

add_subdirectory(cmake-twice)
# add_subdirectory(cmake-super_build)
# add_subdirectory(cmake-external_project)

# Test that the app actually works!
set(IMAGE ${CMAKE_CURRENT_LIST_DIR}/../images/gray_small.pgm)
if (EXISTS ${IMAGE})
    configure_file(${IMAGE} gray_small.pgm COPYONLY)
    add_test(NAME add_filter
             COMMAND add_filter gray_small.pgm 16 out.pgm)
    set_tests_properties(add_filter PROPERTIES
                         LABELS HelloBaremetal
                         PASS_REGULAR_EXPRESSION "Success!"
                         SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")
endif ()
