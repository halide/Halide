cmake_minimum_required(VERSION 3.28)
project(HelloWasm)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

find_package(Halide REQUIRED)

add_halide_generator(reaction_diffusion_generator SOURCES reaction_diffusion_generator.cpp)

foreach (gen IN ITEMS init update render)
    add_halide_library(
        reaction_diffusion_${gen}
        FROM reaction_diffusion_generator
        REGISTRATION ${gen}_cpp
        PARAMS threads=true
    )
endforeach ()

add_executable(run ${init_cpp} ${update_cpp} ${render_cpp})
target_link_libraries(
    run
    PRIVATE
    Halide::RunGenMain reaction_diffusion_init reaction_diffusion_update reaction_diffusion_render
)

enable_testing()

add_test(
    NAME init_bench
    COMMAND
        run --benchmarks=all --benchmark_min_time=1 --name=reaction_diffusion_init
        --output_extents=[1024,1024] --parsable_output
)
add_test(
    NAME update_bench
    COMMAND
        run --benchmarks=all --benchmark_min_time=1 --name=reaction_diffusion_update
        --output_extents=[1024,1024] frame=0 mouse_x=0 mouse_y=0 state=random:0:[1024,1024,3]
        --parsable_output
)
add_test(
    NAME render_bench
    COMMAND
        run --benchmarks=all --benchmark_min_time=1 --name=reaction_diffusion_render
        --output_extents=[1024,1024] state=random:0:[1024,1024,3] --parsable_output
)
