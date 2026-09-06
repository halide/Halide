cmake_minimum_required(VERSION 3.28)
project(HelloWasm)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# WASM-native dependencies
find_package(Threads REQUIRED)

# Halide and the generator package are both host-built config packages, not
# part of the Emscripten sysroot the toolchain file restricts find_package()
# to by default.
find_package(Halide REQUIRED CMAKE_FIND_ROOT_PATH_BOTH)

# Import the generator built by the host-side sub-project.
find_package(HelloWasm-halide_generators REQUIRED CMAKE_FIND_ROOT_PATH_BOTH)
set(GEN_TARGET HelloWasm::halide_generators::reaction_diffusion_generator)

# Keep the static assets next to the build outputs they reference (CMake
# practice: nothing gets written back into the source tree).
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/../index.html index.html COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/../main.js main.js COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/../sw.js sw.js COPYONLY)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/js")

# No explicit TARGETS on any add_halide_library call below: auto-resolved
# from the ambient CMAKE_SYSTEM_NAME=Emscripten/CMAKE_SYSTEM_PROCESSOR=x86 the
# Emscripten toolchain file sets, to wasm-32-wasmrt.
foreach (simd IN ITEMS "" "_simd")
    foreach (threads IN ITEMS "" "_threads")
        set(config "index${simd}${threads}")

        set(features "")
        if (simd)
            list(APPEND features wasm_simd128)
        endif ()
        set(is_threads false)
        if (threads)
            list(APPEND features wasm_threads)
            set(is_threads true)
        endif ()

        # FILE_BASE_NAME/OUTPUT_DIR: core.cpp #includes the fixed name
        # "reaction_diffusion_<gen>.h" regardless of variant, so each variant
        # needs its own output directory to avoid collisions (FILE_BASE_NAME
        # would otherwise default to the per-variant TARGET name instead).
        foreach (gen IN ITEMS init update render)
            add_halide_library(
                ${config}_${gen}
                FROM ${GEN_TARGET}
                GENERATOR reaction_diffusion_${gen}
                FUNCTION_NAME reaction_diffusion_${gen}
                FILE_BASE_NAME reaction_diffusion_${gen}
                OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/${config}"
                FEATURES ${features}
                PARAMS threads=${is_threads}
            )
        endforeach ()

        add_executable(${config} core.cpp)
        target_link_libraries(${config} PRIVATE ${config}_init ${config}_update ${config}_render)
        target_compile_options(${config} PRIVATE --use-port=sdl2)
        # -O3 here (not just relying on CMAKE_BUILD_TYPE=Release's compile
        # flags) because Emscripten's link step does its own codegen/Binaryen
        # optimization passes that CMake's default per-config linker flags
        # don't cover.
        target_link_options(${config} PRIVATE --use-port=sdl2 -sWASM=1 -sTOTAL_MEMORY=512MB -O3)
        if (is_threads)
            target_link_libraries(${config} PRIVATE Threads::Threads)
            target_link_options(${config} PRIVATE -matomics)
        endif ()
    endforeach ()
endforeach ()
