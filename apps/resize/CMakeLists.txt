cmake_minimum_required(VERSION 3.16)
project(resize)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Generator
add_halide_generator(resize.generator SOURCES resize_generator.cpp)

# Filters
list(APPEND VARIANTS
     box_float32_up
     box_float32_down
     box_uint16_up
     box_uint16_down
     box_uint8_up
     box_uint8_down
     linear_float32_up
     linear_float32_down
     linear_uint16_up
     linear_uint16_down
     linear_uint8_up
     linear_uint8_down
     cubic_float32_up
     cubic_float32_down
     cubic_uint16_up
     cubic_uint16_down
     cubic_uint8_up
     cubic_uint8_down
     lanczos_float32_up
     lanczos_float32_down
     lanczos_uint16_up
     lanczos_uint16_down
     lanczos_uint8_up
     lanczos_uint8_down)

foreach (VARIANT IN LISTS VARIANTS)
    string(REPLACE "_" ";" VLIST ${VARIANT})
    list(GET VLIST 0 INTERP)
    list(GET VLIST 1 TYPE)
    list(GET VLIST 2 DIR)
    string(REPLACE "up" "true" DIR ${DIR})
    string(REPLACE "down" "false" DIR ${DIR})
    add_halide_library(resize_${VARIANT} FROM resize.generator
                       GENERATOR resize
                       PARAMS interpolation_type=${INTERP} input.type=${TYPE} upsample=${DIR})
endforeach ()

# Main executable
add_executable(resize resize.cpp)
list(TRANSFORM VARIANTS PREPEND "resize_" OUTPUT_VARIABLE FILTERS)
target_link_libraries(resize
                      PRIVATE
                      Halide::ImageIO
                      ${FILTERS})

# Test that the app actually works!
set(IMAGE ${CMAKE_CURRENT_LIST_DIR}/../images/rgb.png)
if (EXISTS ${IMAGE})
    configure_file(${IMAGE} rgb.png COPYONLY)

    add_test(NAME resize_initial_downsample
             COMMAND resize rgb.png rgb_small.png -i lanczos -t float32 -f 0.125)

    set_tests_properties(resize_initial_downsample PROPERTIES
                         FIXTURES_SETUP rgb_small
                         LABELS resize
                         SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")


    foreach (VARIANT IN LISTS VARIANTS)
        string(REPLACE "_" ";" VLIST ${VARIANT})
        list(GET VLIST 0 INTERP)
        list(GET VLIST 1 TYPE)
        list(GET VLIST 2 DIR)
        if ("${DIR}" STREQUAL "up")
            set(F 4.0)
            set(INPUT rgb_small.png)
        else ()
            set(F 0.5)
            set(INPUT rgb.png)
        endif ()
        add_test(NAME resize_${VARIANT}
                 COMMAND resize ${INPUT} out_${VARIANT}.png -i ${INTERP} -t ${TYPE} -f ${F})
        set_tests_properties(resize_${VARIANT}
                             PROPERTIES FIXTURES_REQUIRED rgb_small
                             LABELS resize
                             PASS_REGULAR_EXPRESSION "Success!"
                             SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")
    endforeach ()
endif ()
