cmake_minimum_required(VERSION 3.28)
project(auto_viz)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Generator
add_halide_generator(auto_viz_demo.generator SOURCES auto_viz_demo_generator.cpp)

# Filters: one library per (schedule, direction) pair, all built with tracing
# enabled so the pipelines can be visualized with HalideTraceViz.
set(variants "")
foreach (schedule IN ITEMS naive lessnaive complex)
    add_halide_library(
        auto_viz_demo_${schedule}_up
        FROM auto_viz_demo.generator
        GENERATOR auto_viz_demo
        FEATURES trace_all
        PARAMS schedule_type=${schedule} upsample=true
    )
    add_halide_library(
        auto_viz_demo_${schedule}_down
        FROM auto_viz_demo.generator
        GENERATOR auto_viz_demo
        FEATURES trace_all
        PARAMS schedule_type=${schedule} upsample=false
    )
    list(APPEND variants auto_viz_demo_${schedule}_up auto_viz_demo_${schedule}_down)
endforeach ()

# Main executable
add_executable(auto_viz_demo auto_viz_demo.cpp)
target_link_libraries(auto_viz_demo PRIVATE Halide::ImageIO ${variants})

# Test that the app actually works!

if (WIN32)
    set(_auto_viz_trace_sink "NUL")
else ()
    set(_auto_viz_trace_sink "/dev/null")
endif ()

set(IMAGE ${CMAKE_CURRENT_LIST_DIR}/../images/rgb_small.png)
if (EXISTS ${IMAGE})
    configure_file(${IMAGE} rgb_small.png COPYONLY)
    foreach (schedule IN ITEMS naive lessnaive complex)
        add_test(
            NAME auto_viz_demo_${schedule}_up
            COMMAND auto_viz_demo rgb_small.png out_${schedule}_up.png -s ${schedule} -f 4.0
        )
        add_test(
            NAME auto_viz_demo_${schedule}_down
            COMMAND auto_viz_demo rgb_small.png out_${schedule}_down.png -s ${schedule} -f 0.25
        )
        set_tests_properties(
            auto_viz_demo_${schedule}_up auto_viz_demo_${schedule}_down
            PROPERTIES
            LABELS auto_viz
            ENVIRONMENT "HL_TRACE_FILE=${_auto_viz_trace_sink}"
            SKIP_REGULAR_EXPRESSION "\\[SKIP\\]"
        )
    endforeach ()
endif ()
