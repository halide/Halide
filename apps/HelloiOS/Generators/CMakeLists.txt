cmake_minimum_required(VERSION 3.28)
project(HelloiOS-Generators LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

find_package(HalideHelpers REQUIRED)

add_halide_generator(
    reaction_diffusion_2_generator
    SOURCES reaction_diffusion_2_generator.cpp
)

set(_all_kernel_updates "")
set(platforms iphoneos iphonesimulator)
set(targets arm-64-ios arm-64-ios-simulator)
foreach (_suffix _target IN ZIP_LISTS platforms targets)
    foreach (stage IN ITEMS init update render)
        add_halide_library(reaction_diffusion_2_${stage}_${_suffix}
                           FROM reaction_diffusion_2_generator
                           GENERATOR reaction_diffusion_2_${stage}
                           FUNCTION_NAME reaction_diffusion_2_${stage}
                           TARGETS ${_target}
                           FEATURES user_context)

        add_halide_library(reaction_diffusion_2_metal_${stage}_${_suffix}
                           FROM reaction_diffusion_2_generator
                           GENERATOR reaction_diffusion_2_${stage}
                           FUNCTION_NAME reaction_diffusion_2_metal_${stage}
                           TARGETS ${_target}
                           FEATURES metal user_context)

        list(APPEND _all_kernel_updates
             reaction_diffusion_2_${stage}_${_suffix}.update
             reaction_diffusion_2_${stage}_${_suffix}.runtime.update
             reaction_diffusion_2_metal_${stage}_${_suffix}.update
             reaction_diffusion_2_metal_${stage}_${_suffix}.runtime.update)
    endforeach ()
endforeach ()

# The IMPORTED library targets from add_halide_library are not part of the
# default build, so we create an explicit target to drive generation.
add_custom_target(halide_kernels ALL DEPENDS ${_all_kernel_updates})

# Group internal targets so they don't clutter the Xcode project navigator.
set_target_properties(
    halide_kernels ${_all_kernel_updates} "${PROJECT_NAME}-halide_generators"
    PROPERTIES
    FOLDER "Halide Internal"
)
