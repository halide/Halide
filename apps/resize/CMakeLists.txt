if(WIN32)
    # Building this app for x86 in VS seems to have issues
    # https://github.com/halide/Halide/pull/2740
    return()
endif()

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

add_executable(resize resize.cpp)
halide_use_image_io(resize)

halide_generator(resize.generator SRCS resize_generator.cpp)
foreach(VARIANT ${VARIANTS})
    string(REPLACE "_" ";" VLIST ${VARIANT})
    list(GET VLIST 0 INTERP)
    list(GET VLIST 1 TYPE)
    list(GET VLIST 2 DIR)
    string(REPLACE "up" "true" DIR ${DIR})
    string(REPLACE "down" "false" DIR ${DIR})
    halide_library_from_generator(resize_${VARIANT}
                                  GENERATOR resize.generator
                                  GENERATOR_ARGS interpolation_type=${INTERP} input.type=${TYPE} upsample=${DIR})
    target_link_libraries(resize PRIVATE resize_${VARIANT})
endforeach()

# Make the small input used to test upsampling with our highest-quality downsampling method
set(RGBORIG "${CMAKE_CURRENT_SOURCE_DIR}/../images/rgb.png")
set(RGBSMALL "${CMAKE_BINARY_DIR}/rgb_small.png")
add_custom_command(
    OUTPUT "${RGBSMALL}"
    DEPENDS resize
    COMMAND resize "${RGBORIG}" "${RGBSMALL}" -i lanczos -t float32 -f 0.125
)
add_custom_target(rgbsmall DEPENDS "${RGBSMALL}")

# Make a resize_all target that will run resize for each variant
add_custom_target(resize_all)
foreach(VARIANT ${VARIANTS})
    string(REPLACE "_" ";" VLIST ${VARIANT})
    list(GET VLIST 0 INTERP)
    list(GET VLIST 1 TYPE)
    list(GET VLIST 2 DIR)
    if("${DIR}" STREQUAL "up")
        set(F 4.0)
        set(INPUT "${RGBSMALL}")
    else()
        set(F 0.5)
        set(INPUT "${RGBORIG}")
    endif()
    set(OUT "${CMAKE_BINARY_DIR}/out_${VARIANT}.png")
    add_custom_command(
        OUTPUT "${OUT}"
        DEPENDS rgbsmall
        COMMAND resize "${INPUT}" "${OUT}" -i ${INTERP} -t ${TYPE} -f ${F}
    )
    add_custom_target(out_${VARIANT} DEPENDS "${OUT}")
    add_dependencies(resize_all out_${VARIANT})
endforeach()
