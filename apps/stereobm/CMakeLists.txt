cmake_minimum_required(VERSION 3.28)
project(stereobm)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

option(STEREOBM_BUILD_OPENCV "Build the OpenCV cv::StereoBM comparison for stereobm" ON)
message(STATUS "STEREOBM_BUILD_OPENCV is ${STEREOBM_BUILD_OPENCV}")

# Find Halide and optionally OpenCV
find_package(Halide REQUIRED)
if (STEREOBM_BUILD_OPENCV)
    find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgproc imgcodecs calib3d)
endif ()

# Generator
add_halide_generator(stereobm.generator SOURCES stereobm_generator.cpp LINK_LIBRARIES Halide::Tools)

# We set parameters here to feed into both OpenCV and our generator, so that they are consistent.
set(STEREOBM_WINSIZE 9)
set(STEREOBM_DEPTH 16)
set(STEREOBM_FILTERCAP 31)
set(STEREOBM_THRESHOLD 10)
set(STEREOBM_UNIQUENESS 0)
set(STEREOBM_MINDISP 0)
set(STEREOBM_TILESIZE 64)

set(STEREOBM_PARAM_DEFS
    WINSIZE=${STEREOBM_WINSIZE}
    DEPTH=${STEREOBM_DEPTH}
    PREFILTER_CAP=${STEREOBM_FILTERCAP}
    TEXTURE_THRESHOLD=${STEREOBM_THRESHOLD}
    UNIQUENESS_RATIO=${STEREOBM_UNIQUENESS}
    MIN_DISP=${STEREOBM_MINDISP}
)

add_halide_library(
    stereobm
    FROM stereobm.generator
    PARAMS
    winsize=${STEREOBM_WINSIZE}
    depth=${STEREOBM_DEPTH}
    tilesize=${STEREOBM_TILESIZE}
    threshold=${STEREOBM_THRESHOLD}
    mindisp=${STEREOBM_MINDISP}
    uniqueness_ratio=${STEREOBM_UNIQUENESS}
    filtercap=${STEREOBM_FILTERCAP}
)

# Always build the variant without OpenCV.
add_executable(stereobm_process process.cpp)
target_link_libraries(stereobm_process PRIVATE Halide::ImageIO stereobm)
target_compile_definitions(stereobm_process PRIVATE STEREOBM_BUILD_OPENCV=0 ${STEREOBM_PARAM_DEFS})

# Build the OpenCV variant if available.
if (STEREOBM_BUILD_OPENCV)
    add_executable(stereobm_process_opencv process.cpp)
    target_link_libraries(stereobm_process_opencv PRIVATE Halide::ImageIO stereobm ${OpenCV_LIBS})
    target_compile_definitions(
        stereobm_process_opencv
        PRIVATE STEREOBM_BUILD_OPENCV=1 ${STEREOBM_PARAM_DEFS}
    )
endif ()

# Test that the app actually works!
set(IMAGEL ${CMAKE_CURRENT_LIST_DIR}/../images/aloeL.png)
set(IMAGER ${CMAKE_CURRENT_LIST_DIR}/../images/aloeR.png)

if (EXISTS ${IMAGEL} AND EXISTS ${IMAGER})
    configure_file(${IMAGEL} aloeL.png COPYONLY)
    configure_file(${IMAGER} aloeR.png COPYONLY)

    add_test(NAME stereobm_process COMMAND stereobm_process aloeL.png aloeR.png out_nocv.png)
    set_tests_properties(
        stereobm_process
        PROPERTIES
        LABELS stereobm
        PASS_REGULAR_EXPRESSION "Success!"
        SKIP_REGULAR_EXPRESSION "\\[SKIP\\]"
    )

    if (STEREOBM_BUILD_OPENCV)
        add_test(
            NAME stereobm_process_opencv
            COMMAND stereobm_process_opencv aloeL.png aloeR.png out.png
        )
        set_tests_properties(
            stereobm_process_opencv
            PROPERTIES
            LABELS stereobm
            PASS_REGULAR_EXPRESSION "Success!"
            SKIP_REGULAR_EXPRESSION "\\[SKIP\\]"
        )
    endif ()
endif ()
