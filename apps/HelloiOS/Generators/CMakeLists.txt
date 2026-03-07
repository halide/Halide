cmake_minimum_required(VERSION 3.28)
project(HelloiOS-Generators LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

find_package(Halide REQUIRED)

add_halide_generator(
    reaction_diffusion_2_generator
    SOURCES reaction_diffusion_2_generator.cpp
)

set(_all_kernels "")
set(platforms iphoneos iphonesimulator)
set(targets arm-64-ios arm-64-ios-simulator)
foreach (platform target IN ZIP_LISTS platforms targets)
    foreach (stage IN ITEMS init update render)
        add_halide_library(reaction_diffusion_2_${stage}_${platform}
                           FROM reaction_diffusion_2_generator
                           GENERATOR reaction_diffusion_2_${stage}
                           FUNCTION_NAME reaction_diffusion_2_${stage}
                           TARGETS ${target}
                           FEATURES user_context)

        add_halide_library(reaction_diffusion_2_metal_${stage}_${platform}
                           FROM reaction_diffusion_2_generator
                           GENERATOR reaction_diffusion_2_${stage}
                           FUNCTION_NAME reaction_diffusion_2_metal_${stage}
                           TARGETS ${target}
                           FEATURES metal user_context)

        list(APPEND _all_kernels
             reaction_diffusion_2_${stage}_${platform}
             reaction_diffusion_2_metal_${stage}_${platform})
    endforeach ()
endforeach ()

# Package all generated kernels into an xcframework for the native Xcode project.
set(_staging "${CMAKE_CURRENT_BINARY_DIR}/_xcfw_staging")
set(_xcfw "${CMAKE_CURRENT_BINARY_DIR}/HalideKernels.xcframework")

set(_xcfw_commands
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${_staging}" "${_xcfw}")
set(_xcfw_create_args "")

set(_halide_inc "$<TARGET_PROPERTY:Halide::Runtime,INTERFACE_INCLUDE_DIRECTORIES>")

foreach (platform IN LISTS platforms)
    list(APPEND _xcfw_commands
         COMMAND ${CMAKE_COMMAND} -E make_directory "${_staging}/${platform}/Headers")

    set(_platform_libs "")
    foreach (stage IN ITEMS init update render)
        foreach (variant IN ITEMS "" "metal_")
            set(_base "reaction_diffusion_2_${variant}${stage}")
            list(APPEND _platform_libs
                 "${CMAKE_CURRENT_BINARY_DIR}/${_base}_${platform}.a"
                 "${CMAKE_CURRENT_BINARY_DIR}/${_base}_${platform}.runtime.a")
            list(APPEND _xcfw_commands
                 COMMAND ${CMAKE_COMMAND} -E copy
                 "${CMAKE_CURRENT_BINARY_DIR}/${_base}_${platform}.h"
                 "${_staging}/${platform}/Headers/${_base}.h")
        endforeach ()
    endforeach ()

    # Copy Halide runtime headers into this platform slice.
    foreach (_hdr IN ITEMS HalideBuffer.h HalideRuntime.h HalideRuntimeMetal.h)
        list(APPEND _xcfw_commands
             COMMAND ${CMAKE_COMMAND} -E copy
             "${_halide_inc}/${_hdr}"
             "${_staging}/${platform}/Headers/${_hdr}")
    endforeach ()

    list(APPEND _xcfw_commands
         COMMAND libtool -static -o "${_staging}/${platform}/libHalideKernels.a" ${_platform_libs})
    list(APPEND _xcfw_create_args
         -library "${_staging}/${platform}/libHalideKernels.a"
         -headers "${_staging}/${platform}/Headers")
endforeach ()

list(APPEND _xcfw_commands
     COMMAND xcodebuild -create-xcframework ${_xcfw_create_args} -output "${_xcfw}")

add_custom_target(halide_kernels ALL
                  DEPENDS ${_all_kernels}
                  ${_xcfw_commands}
                  COMMENT "Packaging HalideKernels.xcframework")
set_property(TARGET halide_kernels PROPERTY FOLDER "Halide Internal")
