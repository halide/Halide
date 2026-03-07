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

foreach (stage IN ITEMS init update render)
    add_halide_library(reaction_diffusion_2_${stage}
                       FROM reaction_diffusion_2_generator
                       GENERATOR reaction_diffusion_2_${stage}
                       FUNCTION_NAME reaction_diffusion_2_${stage}
                       FEATURES user_context)

    add_halide_library(reaction_diffusion_2_metal_${stage}
                       FROM reaction_diffusion_2_generator
                       GENERATOR reaction_diffusion_2_${stage}
                       FUNCTION_NAME reaction_diffusion_2_metal_${stage}
                       FEATURES metal user_context)
endforeach ()

# The IMPORTED library targets from add_halide_library are not part of the
# default build, so we create an explicit target to drive generation.
set(_all_kernel_updates "")
foreach (stage IN ITEMS init update render)
    list(APPEND _all_kernel_updates
         reaction_diffusion_2_${stage}.update
         reaction_diffusion_2_${stage}.runtime.update
         reaction_diffusion_2_metal_${stage}.update
         reaction_diffusion_2_metal_${stage}.runtime.update)
endforeach ()
add_custom_target(halide_kernels ALL DEPENDS ${_all_kernel_updates})

# Group internal targets so they don't clutter the Xcode project navigator.
set_target_properties(
    halide_kernels ${_all_kernel_updates} "${PROJECT_NAME}-halide_generators"
    PROPERTIES
    FOLDER "Halide Internal"
)
