cmake_minimum_required(VERSION 3.16)
project(HelloWasm)

enable_testing()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

find_package(Halide REQUIRED)
set(halide_includes "$<TARGET_PROPERTY:Halide::Runtime,INTERFACE_INCLUDE_DIRECTORIES>")

find_program(EMCC emcc HINTS "$ENV{EMSDK}/upstream/emscripten")
if (NOT EMCC)
    message(FATAL_ERROR "Could not find emscripten/emcc!")
endif ()

configure_file(index.html index.html COPYONLY)

set(EMCC_FLAGS -s WASM=1 -s USE_SDL=2 -s TOTAL_MEMORY=512MB -O3)
set(EMCC_FLAGS_threads -pthread -matomics)

add_halide_generator(reaction_diffusion_generator SOURCES reaction_diffusion_generator.cpp)

set(features_simd -wasm_simd128)
set(features_threads -wasm_threads)
set(params_threads threads=true)
set(params threads=false)
foreach (simd IN ITEMS "" "_simd")
    foreach (threads IN ITEMS "" "_threads")
        set(config "wasm${simd}${threads}")
        set(js_file "js/index${simd}${threads}.js")

        set(target "wasm-32-wasmrt${features${simd}}${features${threads}}")

        foreach (gen IN ITEMS init update render)
            add_halide_library(${config}_${gen} FROM reaction_diffusion_generator
                               GENERATOR reaction_diffusion_${gen}
                               FUNCTION_NAME reaction_diffusion_${gen}
                               PARAMS ${params${threads}}
                               TARGETS ${target})
        endforeach ()

        set(pipeline_includes "$<TARGET_PROPERTY:${config}_init,INTERFACE_INCLUDE_DIRECTORIES>")
        add_custom_command(OUTPUT ${js_file}
                           COMMAND "${EMCC}"
                           "$<SHELL_PATH:${CMAKE_CURRENT_SOURCE_DIR}/core.cpp>"
                           "$<TARGET_FILE:${config}_init>"
                           "$<TARGET_FILE:${config}_update>"
                           "$<TARGET_FILE:${config}_render>"
                           "$<TARGET_FILE:${config}_init.runtime>"
                           ${EMCC_FLAGS}
                           ${EMCC_FLAGS${threads}}
                           "-I$<JOIN:${halide_includes},;-I>"
                           "-I$<JOIN:${pipeline_includes},;-I>"
                           -o "${js_file}"
                           VERBATIM
                           DEPENDS ${config}_init ${config}_update ${config}_render
                           COMMAND_EXPAND_LISTS)

        add_custom_target(${config} ALL DEPENDS ${js_file})
    endforeach ()
endforeach ()

foreach (gen IN ITEMS init update render)
    add_halide_library(reaction_diffusion_${gen} FROM reaction_diffusion_generator
                       REGISTRATION ${gen}_cpp
                       PARAMS threads=true)
endforeach ()

add_executable(run ${init_cpp} ${update_cpp} ${render_cpp})
target_link_libraries(run PRIVATE Halide::RunGenMain reaction_diffusion_init reaction_diffusion_update reaction_diffusion_render)

add_test(NAME init_bench COMMAND run --benchmarks=all --benchmark_min_time=1 --name=reaction_diffusion_init --output_extents=[1024,1024] --parsable_output)
add_test(NAME update_bench COMMAND run --benchmarks=all --benchmark_min_time=1 --name=reaction_diffusion_update --output_extents=[1024,1024] frame=0 mouse_x=0 mouse_y=0 state=random:0:[1024,1024,3] --parsable_output)
add_test(NAME render_bench COMMAND run --benchmarks=all --benchmark_min_time=1 --name=reaction_diffusion_render --output_extents=[1024,1024]  state=random:0:[1024,1024,3] --parsable_output)
