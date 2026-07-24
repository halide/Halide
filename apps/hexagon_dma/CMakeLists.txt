cmake_minimum_required(VERSION 3.28)
project(hexagon_dma)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# The pipelines use the hexagon_dma scheduling directives, which require the
# hexagon_dma target feature. They are exercised on the host by linking against
# mock_dma_implementation.cpp, which stands in for the real Hexagon DMA driver.

# These pipelines don't touch any GPU/device APIs, so always build them for a
# plain "host-hexagon_dma" target rather than inheriting the ambient
# Halide_TARGET. CI configures Halide_TARGET to things like host-cuda or
# host-vulkan-... to exercise GPU apps, and combining hexagon_dma with an
# unrelated device API breaks target auto-detection (e.g. querying the host's
# CUDA compute capability JIT-compiles the whole combined runtime, which then
# fails to link against hexagon_dma's driver-only symbols).

# Build the full RO+RW schedule matrix when requested (mirrors the Makefile's
# SCHEDULE_ALL). By default we build only the RW async/split/split_async
# schedules, which is what `make all` produces.
option(HEXAGON_DMA_SCHEDULE_ALL "Build the full RO+RW schedule matrix" OFF)

set(rw_schedules async split split_async)
set(ro_schedules "")
if (HEXAGON_DMA_SCHEDULE_ALL)
    list(PREPEND rw_schedules basic fold)
    set(ro_schedules basic fold async split split_async)
endif ()

# The pipeline function names use "basic", but the generator's schedule
# GeneratorParam spells that value "none".
function(_dma_sched_value out label)
    if (label STREQUAL "basic")
        set(${out} none PARENT_SCOPE)
    else ()
        set(${out} ${label} PARENT_SCOPE)
    endif ()
endfunction()

##
# YUV pipelines (nv12 = 8-bit, p010 = 16-bit)
##

add_halide_generator(pipeline_yuv_linear_basic.generator SOURCES pipeline_yuv_linear_basic.cpp)

set(nv12_types input_y.type=uint8 input_uv.type=uint8 output_y.type=uint8 output_uv.type=uint8)
set(p010_types input_y.type=uint16 input_uv.type=uint16 output_y.type=uint16 output_uv.type=uint16)

set(yuv_libs "")
foreach (type_name IN ITEMS nv12 p010)
    foreach (label IN LISTS rw_schedules)
        _dma_sched_value(sched ${label})
        add_halide_library(
            pipeline_${type_name}_linear_rw_${label}
            FROM pipeline_yuv_linear_basic.generator
            GENERATOR pipeline_yuv_linear_basic
            TARGETS host
            FEATURES hexagon_dma
            PARAMS schedule=${sched} use_dma_for_output=true ${${type_name}_types}
        )
        list(APPEND yuv_libs pipeline_${type_name}_linear_rw_${label})
    endforeach ()
    foreach (label IN LISTS ro_schedules)
        _dma_sched_value(sched ${label})
        add_halide_library(
            pipeline_${type_name}_linear_ro_${label}
            FROM pipeline_yuv_linear_basic.generator
            GENERATOR pipeline_yuv_linear_basic
            TARGETS host
            FEATURES hexagon_dma
            PARAMS schedule=${sched} use_dma_for_output=false ${${type_name}_types}
        )
        list(APPEND yuv_libs pipeline_${type_name}_linear_ro_${label})
    endforeach ()
endforeach ()

##
# RAW interleaved pipeline (fixed uint8)
##

add_halide_generator(
    pipeline_raw_linear_interleaved_basic.generator
    SOURCES pipeline_raw_linear_interleaved_basic.cpp
)

set(raw_libs "")
foreach (label IN LISTS rw_schedules)
    _dma_sched_value(sched ${label})
    add_halide_library(
        pipeline_raw_linear_interleaved_rw_${label}
        FROM pipeline_raw_linear_interleaved_basic.generator
        GENERATOR pipeline_raw_linear_interleaved_basic
        TARGETS host
        FEATURES hexagon_dma
        PARAMS schedule=${sched} use_dma_for_output=true
    )
    list(APPEND raw_libs pipeline_raw_linear_interleaved_rw_${label})
endforeach ()
foreach (label IN LISTS ro_schedules)
    _dma_sched_value(sched ${label})
    add_halide_library(
        pipeline_raw_linear_interleaved_ro_${label}
        FROM pipeline_raw_linear_interleaved_basic.generator
        GENERATOR pipeline_raw_linear_interleaved_basic
        TARGETS host
        FEATURES hexagon_dma
        PARAMS schedule=${sched} use_dma_for_output=false
    )
    list(APPEND raw_libs pipeline_raw_linear_interleaved_ro_${label})
endforeach ()

##
# Processing executables (run on the host against the mock DMA driver)
##

# mock_dma_implementation.cpp stands in for the real Hexagon DMA driver by
# providing weak definitions of its entry points, using the GCC/Clang-only
# WEAK == __attribute__((weak)) from src/runtime/hexagon_dma_pool.h and
# src/runtime/mini_hexagon_dma.h. MSVC has no equivalent for these headers'
# use of __attribute__, so just skip the mock-driver-backed executables and
# tests there; the pipelines themselves still build fine.
if (NOT MSVC)
    add_executable(
        process_yuv_linear_basic process_yuv_linear_basic.cpp mock_dma_implementation.cpp
    )
    target_link_libraries(process_yuv_linear_basic PRIVATE Halide::Tools ${yuv_libs})

    add_executable(
        process_raw_linear_interleaved_basic
        process_raw_linear_interleaved_basic.cpp
        mock_dma_implementation.cpp
    )
    target_link_libraries(process_raw_linear_interleaved_basic PRIVATE Halide::Tools ${raw_libs})

    if (HEXAGON_DMA_SCHEDULE_ALL)
        target_compile_definitions(process_yuv_linear_basic PRIVATE SCHEDULE_ALL)
        target_compile_definitions(process_raw_linear_interleaved_basic PRIVATE SCHEDULE_ALL)
    endif ()

    # Test that the apps actually work!
    add_test(NAME process_yuv_linear_basic COMMAND process_yuv_linear_basic 256 128 async rw nv12)
    add_test(
        NAME process_raw_linear_interleaved_basic
        COMMAND process_raw_linear_interleaved_basic 256 128 async rw
    )
    set_tests_properties(
        process_yuv_linear_basic process_raw_linear_interleaved_basic
        PROPERTIES
        LABELS hexagon_dma
        PASS_REGULAR_EXPRESSION "Success!"
        SKIP_REGULAR_EXPRESSION "\\[SKIP\\]"
    )
endif ()
