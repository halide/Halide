cmake_minimum_required(VERSION 3.28)
project(gaussian_blur)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Generator
add_halide_generator(gaussian_blur.generator SOURCES gaussian_blur_generator.cpp)

# Filters
add_halide_library(gaussian_blur_direct FROM gaussian_blur.generator
                   GENERATOR gaussian_blur_direct)

set(VARIANTS "")
foreach (U IN ITEMS 2 3 4)
    foreach (D IN ITEMS 1 2 3)
        foreach (F IN ITEMS 2 4 8 16)
            add_halide_library(gaussian_blur_${U}_${D}_${F} FROM gaussian_blur.generator
                               GENERATOR gaussian_blur
                               USE_RUNTIME gaussian_blur_direct.runtime
                               PARAMS upsample_order=${U} downsample_order=${D} factor=${F})
            list(APPEND VARIANTS gaussian_blur_${U}_${D}_${F})
        endforeach ()
    endforeach ()
endforeach ()


# Generate combined header blurs.h that includes all variant headers
set(BLURS_H "${CMAKE_CURRENT_BINARY_DIR}/blurs.h")
set(BLURS_H_CONTENT "")
string(APPEND BLURS_H_CONTENT "#include \"gaussian_blur_direct.h\"\n")
foreach (V IN LISTS VARIANTS)
    string(APPEND BLURS_H_CONTENT "#include \"${V}.h\"\n")
endforeach ()
file(GENERATE OUTPUT "${BLURS_H}" CONTENT "${BLURS_H_CONTENT}")

# Main executable
add_executable(gaussian_blur_filter filter.cpp)
target_link_libraries(gaussian_blur_filter
                      PRIVATE
                      Halide::ImageIO
                      gaussian_blur_direct
                      ${VARIANTS})
target_include_directories(gaussian_blur_filter PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# Test that the app actually works!
set(IMAGE ${CMAKE_CURRENT_LIST_DIR}/../images/gray.png)
if (EXISTS ${IMAGE})
    configure_file(${IMAGE} gray.png COPYONLY)
    add_test(NAME gaussian_blur_filter COMMAND gaussian_blur_filter gray.png 10)
    set_tests_properties(gaussian_blur_filter PROPERTIES
                         LABELS gaussian_blur
                         PASS_REGULAR_EXPRESSION "Success!"
                         SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")
endif ()
