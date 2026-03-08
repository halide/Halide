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

foreach (simulator IN ITEMS "" "-simulator")
    foreach (stage IN ITEMS init update render)
        add_halide_library(reaction_diffusion_2_${stage}${simulator}
                           FROM reaction_diffusion_2_generator
                           GENERATOR reaction_diffusion_2_${stage}
                           FUNCTION_NAME reaction_diffusion_2_${stage}
                           FILE_BASE_NAME reaction_diffusion_2_${stage}
                           OUTPUT_DIR "arm-64-ios${simulator}"
                           TARGETS "arm-64-ios${simulator}"
                           FEATURES user_context)

        add_halide_library(reaction_diffusion_2_metal_${stage}${simulator}
                           FROM reaction_diffusion_2_generator
                           GENERATOR reaction_diffusion_2_${stage}
                           FUNCTION_NAME reaction_diffusion_2_metal_${stage}
                           FILE_BASE_NAME reaction_diffusion_2_metal_${stage}
                           OUTPUT_DIR "arm-64-ios${simulator}"
                           TARGETS "arm-64-ios${simulator}"
                           FEATURES metal user_context)
    endforeach ()
endforeach ()

add_halide_xcframework(HalideKernels LIBRARIES ALL)
