##
# Convenience method for defining test cases in this directory.
##

function(halide_define_aot_test NAME)
    set(options OMIT_DEFAULT_GENERATOR)
    set(oneValueArgs FUNCTION_NAME)
    set(multiValueArgs GEN_DEPS EXTRA_LIBS ENABLE_IF FEATURES PARAMS GEN_TARGET)
    cmake_parse_arguments(args "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (args_ENABLE_IF AND NOT (${args_ENABLE_IF}))
        return()
    endif ()

    add_executable("${NAME}.generator" "${NAME}_generator.cpp")
    target_link_libraries("${NAME}.generator" PRIVATE Halide::Generator ${args_GEN_DEPS})

    set(TARGET "generator_aot_${NAME}")
    set(DEPS ${args_EXTRA_LIBS})
    if (NOT args_OMIT_DEFAULT_GENERATOR)
        add_halide_library(${NAME}
                           FROM "${NAME}.generator"
                           PARAMS "${args_PARAMS}"
                           TARGETS "${args_GEN_TARGET}"
                           FUNCTION_NAME "${args_FUNCTION_NAME}"
                           FEATURES "${args_FEATURES}")
        list(APPEND DEPS ${NAME} ${NAME}.runtime)
    endif ()

    if (TARGET_WEBASSEMBLY AND Halide_TARGET MATCHES "wasm")
        add_wasm_executable("${TARGET}"
                            SRCS "${NAME}_aottest.cpp"
                            DEPS "${DEPS}"
                            INCLUDES
                            "${Halide_BINARY_DIR}/include"
                            "${Halide_SOURCE_DIR}/test/common"
                            "${Halide_SOURCE_DIR}/tools"
                            "${CMAKE_CURRENT_BINARY_DIR}")

        add_wasm_halide_test("${TARGET}" GROUPS generator)
    else ()
        add_executable("${TARGET}" "${NAME}_aottest.cpp")
        target_include_directories("${TARGET}" PRIVATE
            "${Halide_SOURCE_DIR}/test/common"
            "${Halide_SOURCE_DIR}/tools")
        if (NOT args_OMIT_DEFAULT_GENERATOR)
            target_link_libraries(${TARGET} PRIVATE ${NAME})
        endif ()
        if (args_EXTRA_LIBS)
            target_link_libraries(${TARGET} PRIVATE ${args_EXTRA_LIBS})
        endif ()

        # TODO(#4938): remove need for these definitions
        if ("${Halide_TARGET}" MATCHES "opencl")
            target_compile_definitions("${TARGET}" PRIVATE TEST_OPENCL)
        endif ()
        if ("${Halide_TARGET}" MATCHES "metal")
            target_compile_definitions("${TARGET}" PRIVATE TEST_METAL)
        endif ()
        if ("${Halide_TARGET}" MATCHES "cuda")
            target_compile_definitions("${TARGET}" PRIVATE TEST_CUDA)
        endif ()
        add_halide_test("${TARGET}" GROUPS generator)
    endif ()
endfunction()

##
# Define a nontrivial dependency for external_code.generator
##

# external_code_extern.cpp
set(EXTERNAL_CPP "${CMAKE_CURRENT_SOURCE_DIR}/external_code_extern.cpp")

set(EC32 "external_code_extern_bitcode_32")
add_custom_command(OUTPUT "${EC32}.bc"
                   COMMAND clang -O3 -c -m32 -target le32-unknown-nacl-unknown -emit-llvm "$<SHELL_PATH:${EXTERNAL_CPP}>" -o "${EC32}.bc"
                   DEPENDS "${EXTERNAL_CPP}"
                   VERBATIM)
add_custom_command(OUTPUT "${EC32}.cpp"
                   COMMAND binary2cpp external_code_extern_bitcode_32 < "${EC32}.bc" > "${EC32}.cpp"
                   DEPENDS "${EC32}.bc" binary2cpp
                   VERBATIM)

set(EC64 "external_code_extern_bitcode_64")
add_custom_command(OUTPUT "${EC64}.bc"
                   COMMAND clang -O3 -c -m64 -target le64-unknown-unknown-unknown -emit-llvm "$<SHELL_PATH:${EXTERNAL_CPP}>" -o "${EC64}.bc"
                   DEPENDS "${EXTERNAL_CPP}" binary2cpp
                   VERBATIM)
add_custom_command(OUTPUT "${EC64}.cpp"
                   COMMAND binary2cpp external_code_extern_bitcode_64 < "${EC64}.bc" > "${EC64}.cpp"
                   DEPENDS "${EC64}.bc" binary2cpp
                   VERBATIM)

set(ECCPP "external_code_extern_cpp_source")
add_custom_command(OUTPUT "${ECCPP}.cpp"
                   COMMAND binary2cpp external_code_extern_cpp_source < "$<SHELL_PATH:${EXTERNAL_CPP}>" > "${ECCPP}.cpp"
                   DEPENDS "${EXTERNAL_CPP}" binary2cpp
                   VERBATIM)

add_library(external_code_generator_deps OBJECT "${EC32}.cpp" "${EC64}.cpp" "${ECCPP}.cpp")

##
# Some tests are not available when compiling for WASM.
##

set(USING_WASM (TARGET_WEBASSEMBLY AND Halide_TARGET MATCHES "wasm"))

if (NOT ${USING_WASM})
    # cxx_mangling_externs.cpp
    add_library(cxx_mangling_externs STATIC cxx_mangling_externs.cpp)

    # cxx_mangling_define_extern_externs.cpp
    add_library(cxx_mangling_define_extern_externs STATIC cxx_mangling_define_extern_externs.cpp)
    target_link_libraries(cxx_mangling_define_extern_externs PRIVATE cxx_mangling)
endif ()

if (TARGET_NVPTX AND Halide_TARGET MATCHES "cuda")
    include(AddCudaToTarget)
endif ()
if (TARGET_NVPTX AND Halide_TARGET MATCHES "opencl")
    find_package(OpenCL REQUIRED)
endif ()

##
# Create targets for the AOT tests
##

# acquire_release_aottest.cpp
# acquire_release_generator.cpp
# acquire_release_generator.cpp
halide_define_aot_test(acquire_release)
if (TARGET_NVPTX AND Halide_TARGET MATCHES "cuda")
    add_cuda_to_target(generator_aot_acquire_release PRIVATE)
endif ()
if (TARGET_NVPTX AND Halide_TARGET MATCHES "opencl")
    target_link_libraries(generator_aot_acquire_release PRIVATE OpenCL::OpenCL)
endif ()

# TODO: what are these?
# configure_jittest.cpp
# example_jittest.cpp
# registration_test.cpp
# rungen_test.cpp

# alias_aottest.cpp
# alias_generator.cpp
halide_define_aot_test(alias EXTRA_LIBS alias_with_offset_42)
add_halide_library(alias_with_offset_42
                   FROM alias.generator
                   GENERATOR alias_with_offset_42)

# argvcall_aottest.cpp
# argvcall_generator.cpp
halide_define_aot_test(argvcall)

# async_parallel_aottest.cpp
# async_parallel_generator.cpp
halide_define_aot_test(async_parallel
                       # Requires threading support, not yet available for wasm tests
                       ENABLE_IF NOT ${USING_WASM})

# autograd_aottest.cpp
# autograd_generator.cpp
halide_define_aot_test(autograd ENABLE_IF TARGET Halide::Mullapudi2016 AND NOT ${USING_WASM})
if (TARGET generator_aot_autograd)
    add_halide_library(autograd_grad
                       GRADIENT_DESCENT
                       FROM autograd.generator
                       GENERATOR autograd
                       AUTOSCHEDULER Halide::Mullapudi2016)
    target_link_libraries(generator_aot_autograd PRIVATE autograd_grad)
endif ()

# abstractgeneratortest_aottest.cpp
# abstractgeneratortest_generator.cpp
halide_define_aot_test(abstractgeneratortest)

# bit_operations_aottest.cpp
# bit_operations_generator.cpp
halide_define_aot_test(bit_operations)

# blur2x2_aottest.cpp
# blur2x2_generator.cpp
halide_define_aot_test(blur2x2)

# buffer_copy_aottest.cpp
# buffer_copy_generator.cpp
halide_define_aot_test(buffer_copy)

# can_use_target_aottest.cpp
# can_use_target_generator.cpp
halide_define_aot_test(can_use_target)

# cleanup_on_error_aottest.cpp
# cleanup_on_error_generator.cpp
# TODO: requires access to internal header runtime/device_interface.h
# halide_define_aot_test(cleanup_on_error)

# configure_aottest.cpp
# configure_generator.cpp
halide_define_aot_test(configure)

# cxx_mangling_define_extern_aottest.cpp
# cxx_mangling_define_extern_generator.cpp
halide_define_aot_test(cxx_mangling_define_extern
                       # Needs extra deps / build rules
                       ENABLE_IF NOT ${USING_WASM}
                       FUNCTION_NAME "HalideTest::cxx_mangling_define_extern"
                       FEATURES c_plus_plus_name_mangling user_context)
if (TARGET cxx_mangling_define_extern)
    target_link_libraries(cxx_mangling_define_extern INTERFACE cxx_mangling_externs cxx_mangling_define_extern_externs)
    target_link_libraries(cxx_mangling_define_extern_externs PRIVATE cxx_mangling_externs cxx_mangling)
endif ()

# cxx_mangling_aottest.cpp
# cxx_mangling_generator.cpp
halide_define_aot_test(cxx_mangling
                       # Needs extra deps / build rules
                       ENABLE_IF NOT ${USING_WASM}
                       FUNCTION_NAME HalideTest::AnotherNamespace::cxx_mangling
                       FEATURES c_plus_plus_name_mangling)
if (TARGET cxx_mangling)
    target_link_libraries(cxx_mangling INTERFACE cxx_mangling_externs)
    if (TARGET_NVPTX AND Halide_TARGET MATCHES "cuda")
        add_halide_library(cxx_mangling_gpu
                           FROM cxx_mangling.generator
                           GENERATOR cxx_mangling
                           FUNCTION_NAME HalideTest::cxx_mangling_gpu
                           FEATURES c_plus_plus_name_mangling cuda)
        target_link_libraries(generator_aot_cxx_mangling PRIVATE cxx_mangling_gpu)
    endif ()
endif ()

# define_extern_opencl_aottest.cpp
# define_extern_opencl_generator.cpp
halide_define_aot_test(define_extern_opencl)
if (TARGET_NVPTX AND Halide_TARGET MATCHES "opencl")
    find_package(OpenCL REQUIRED)
    target_link_libraries(generator_aot_define_extern_opencl PRIVATE OpenCL::OpenCL)
endif ()

# embed_image_aottest.cpp
# embed_image_generator.cpp
halide_define_aot_test(embed_image)

# error_codes_aottest.cpp
# error_codes_generator.cpp
halide_define_aot_test(error_codes)

# example_aottest.cpp
# example_generator.cpp
halide_define_aot_test(example)

# extern_output_aottest.cpp
# extern_output_generator.cpp
halide_define_aot_test(extern_output)

# external_code_aottest.cpp
# external_code_generator.cpp
halide_define_aot_test(external_code GEN_DEPS external_code_generator_deps)

# float16_t_aottest.cpp
# float16_t_generator.cpp
halide_define_aot_test(float16_t)

# gpu_multi_context_threaded_aottest.cpp
# gpu_multi_context_threaded_generator.cpp
# (Doesn't build/link properly under wasm, and isn't useful there anyway)
if (NOT Halide_TARGET MATCHES "wasm")
  halide_define_aot_test(gpu_multi_context_threaded
                         OMIT_DEFAULT_GENERATOR
                         EXTRA_LIBS
                         gpu_multi_context_threaded_add
                         gpu_multi_context_threaded_mul)

  add_halide_library(gpu_multi_context_threaded_add FROM gpu_multi_context_threaded.generator
                     FEATURES user_context)
  add_halide_library(gpu_multi_context_threaded_mul FROM gpu_multi_context_threaded.generator
                     FEATURES user_context)

  if (TARGET_NVPTX AND Halide_TARGET MATCHES "cuda")
      add_cuda_to_target(generator_aot_gpu_multi_context_threaded PRIVATE)
  endif ()
  if (TARGET_NVPTX AND Halide_TARGET MATCHES "opencl")
      target_link_libraries(generator_aot_gpu_multi_context_threaded PRIVATE OpenCL::OpenCL)
  endif ()
endif ()

# gpu_object_lifetime_aottest.cpp
# gpu_object_lifetime_generator.cpp
halide_define_aot_test(gpu_object_lifetime FEATURES debug)

# gpu_only_aottest.cpp
# gpu_only_generator.cpp
halide_define_aot_test(gpu_only)

# gpu_texture_aottest.cpp
# gpu_texture_generator.cpp
halide_define_aot_test(gpu_texture)

# image_from_array_aottest.cpp
# image_from_array_generator.cpp
halide_define_aot_test(image_from_array)

# mandelbrot_aottest.cpp
# mandelbrot_generator.cpp
halide_define_aot_test(mandelbrot)

# memory_profiler_mandelbrot_aottest.cpp
# memory_profiler_mandelbrot_generator.cpp
halide_define_aot_test(memory_profiler_mandelbrot
                       # Requires profiler support (which requires threading), not yet available for wasm tests
                       ENABLE_IF NOT ${USING_WASM}
                       FEATURES profile)

# metadata_tester_aottest.cpp
# metadata_tester_generator.cpp
set(metadata_tester_params
    input.type=uint8 input.dim=3
    dim_only_input_buffer.type=uint8
    untyped_input_buffer.type=uint8 untyped_input_buffer.dim=3
    output.type=float32,float32 output.dim=3
    input_not_nod.type=uint8 input_not_nod.dim=3
    input_nod.dim=3
    input_not.type=uint8
    array_input.size=2
    array_i8.size=2
    array_i16.size=2
    array_i32.size=2
    array_h.size=2
    buffer_array_input2.dim=3
    buffer_array_input3.type=float32
    buffer_array_input4.dim=3
    buffer_array_input4.type=float32
    buffer_array_input5.size=2
    buffer_array_input6.size=2
    buffer_array_input6.dim=3
    buffer_array_input7.size=2
    buffer_array_input7.type=float32
    buffer_array_input8.size=2
    buffer_array_input8.dim=3
    buffer_array_input8.type=float32
    buffer_f16_untyped.type=float16
    untyped_scalar_input.type=uint8
    array_outputs.size=2
    array_outputs7.size=2
    array_outputs8.size=2
    array_outputs9.size=2)

halide_define_aot_test(metadata_tester
                       EXTRA_LIBS metadata_tester_ucon
                       PARAMS ${metadata_tester_params})
add_halide_library(metadata_tester_ucon
                   FROM metadata_tester.generator
                   GENERATOR metadata_tester
                   FEATURES user_context
                   PARAMS ${metadata_tester_params})

# msan_aottest.cpp
# msan_generator.cpp
halide_define_aot_test(msan FEATURES msan)

# (Doesn't build/link properly on windows / under wasm)
if (NOT Halide_TARGET MATCHES "windows" AND NOT CMAKE_SYSTEM_NAME MATCHES "Windows" AND NOT Halide_TARGET MATCHES "wasm")
  # sanitizercoverage_aottest.cpp
  # sanitizercoverage_generator.cpp
  halide_define_aot_test(sanitizercoverage FEATURES sanitizer_coverage)
endif()

# multitarget_aottest.cpp
# multitarget_generator.cpp
halide_define_aot_test(multitarget
                       # Multitarget doesn't apply to WASM
                       ENABLE_IF NOT ${USING_WASM}
                       GEN_TARGET cmake-no_bounds_query cmake
                       FEATURES c_plus_plus_name_mangling
                       FUNCTION_NAME HalideTest::multitarget)

# nested_externs_aottest.cpp
# nested_externs_generator.cpp
halide_define_aot_test(nested_externs
                       OMIT_DEFAULT_GENERATOR
                       EXTRA_LIBS
                       nested_externs_root
                       nested_externs_inner
                       nested_externs_combine
                       nested_externs_leaf
                       nested_externs_root.runtime)

add_halide_library(nested_externs_root FROM nested_externs.generator)
add_halide_library(nested_externs_inner FROM nested_externs.generator)
add_halide_library(nested_externs_combine FROM nested_externs.generator)
add_halide_library(nested_externs_leaf FROM nested_externs.generator)

# opencl_runtime_aottest.cpp
# opencl_runtime_generator.cpp
halide_define_aot_test(opencl_runtime)

# output_assign_aottest.cpp
# output_assign_generator.cpp
halide_define_aot_test(output_assign)

# pyramid_aottest.cpp
# pyramid_generator.cpp
halide_define_aot_test(pyramid PARAMS levels=10)

# rdom_input_aottest.cpp
# rdom_input_generator.cpp
halide_define_aot_test(rdom_input)

# string_param_aottest.cpp
# string_param_generator.cpp
halide_define_aot_test(string_param PARAMS "rpn_expr=5 y * x +")

# stubtest_aottest.cpp
# stubtest_generator.cpp
# stubtest_jittest.cpp
# stubs not supported in CMake

# stubuser_aottest.cpp
# stubuser_generator.cpp
# stubs not supported in CMake

# shuffler_aottest.cpp
# shuffler_generator.cpp
halide_define_aot_test(shuffler)

# templated_aottest.cpp
# templated_generator.cpp
halide_define_aot_test(templated)

# tiled_blur_aottest.cpp
# tiled_blur_generator.cpp
halide_define_aot_test(tiled_blur EXTRA_LIBS blur2x2)

# user_context_aottest.cpp
# user_context_generator.cpp
halide_define_aot_test(user_context FEATURES user_context)

# user_context_insanity_aottest.cpp
# user_context_insanity_generator.cpp
halide_define_aot_test(user_context_insanity FEATURES user_context)

# variable_num_threads_aottest.cpp
# variable_num_threads_generator.cpp
halide_define_aot_test(variable_num_threads
                       # Requires threading support, not yet available for wasm tests
                       ENABLE_IF NOT ${USING_WASM})

