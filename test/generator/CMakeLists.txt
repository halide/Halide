##
# Convenience method for defining test cases in this directory.
##

function(halide_define_aot_test NAME)
    set(options OMIT_DEFAULT_GENERATOR)
    set(oneValueArgs)
    set(multiValueArgs EXTRA_LIBS)
    cmake_parse_arguments(args "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    set(TARGET "generator_aot_${NAME}")
    add_executable("${TARGET}" "${NAME}_aottest.cpp")
    target_include_directories("${TARGET}" PRIVATE "${Halide_SOURCE_DIR}/test/common")
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
endfunction(halide_define_aot_test)

##
# Common extra functionality needed by certain tests.
##

add_library(cxx_mangling_externs STATIC cxx_mangling_externs.cpp)
add_library(cxx_mangling_define_extern_externs STATIC cxx_mangling_define_extern_externs.cpp)

##
# Create targets for all the generators as well as library targets for their default configurations
##

set(GENERATOR_SRCS
    acquire_release_generator.cpp
    alias_generator.cpp
    argvcall_generator.cpp
    async_parallel_generator.cpp
    autograd_generator.cpp
    bit_operations_generator.cpp
    blur2x2_generator.cpp
    buffer_copy_generator.cpp
    buildmethod_generator.cpp
    can_use_target_generator.cpp
    cleanup_on_error_generator.cpp
    configure_generator.cpp
    cxx_mangling_define_extern_generator.cpp
    cxx_mangling_generator.cpp
    define_extern_opencl_generator.cpp
    embed_image_generator.cpp
    error_codes_generator.cpp
    example_generator.cpp
    extern_output_generator.cpp
    external_code_generator.cpp
    float16_t_generator.cpp
    gpu_object_lifetime_generator.cpp
    gpu_only_generator.cpp
    image_from_array_generator.cpp
    mandelbrot_generator.cpp
    matlab_generator.cpp
    memory_profiler_mandelbrot_generator.cpp
    metadata_tester_generator.cpp
    msan_generator.cpp
    multitarget_generator.cpp
    nested_externs_generator.cpp
    opencl_runtime_generator.cpp
    output_assign_generator.cpp
    pyramid_generator.cpp
    rdom_input_generator.cpp
    string_param_generator.cpp
    tiled_blur_generator.cpp
    user_context_generator.cpp
    user_context_insanity_generator.cpp
    variable_num_threads_generator.cpp
    )

# Special configuration for various tests.
set(FN_NAME_cxx_mangling HalideTest::AnotherNamespace::cxx_mangling)
set(FEATURES_cxx_mangling c_plus_plus_name_mangling)

set(FN_NAME_cxx_mangling_define_extern HalideTest::cxx_mangling_define_extern)
set(FEATURES_cxx_mangling_define_extern c_plus_plus_name_mangling user_context)

set(FEATURES_gpu_object_lifetime debug)

set(FEATURES_matlab matlab)

set(FEATURES_memory_profiler_mandelbrot profile)

set(PARAMS_metadata_tester
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
    array_outputs.size=2
    array_outputs7.size=2
    array_outputs8.size=2
    array_outputs9.size=2
    )

set(FEATURES_msan msan)

set(Halide_TARGET_multitarget host host-no_bounds_query)
set(FEATURES_multitarget c_plus_plus_name_mangling)
set(FN_NAME_multitarget HalideTest::multitarget)

set(PARAMS_pyramid levels=10)

set(PARAMS_string_param rpn_expr="5 y * x +")

set(FEATURES_user_context user_context)
set(FEATURES_user_context_insanity user_context)

# Create the generator / filter-library targets.
foreach (FILE IN LISTS GENERATOR_SRCS)
    string(REPLACE "_generator.cpp" "" GEN_NAME "${FILE}")

    add_executable("${GEN_NAME}.generator" "${FILE}")
    target_link_libraries("${GEN_NAME}.generator" PRIVATE Halide::Generator)

    add_halide_library(${GEN_NAME}
                       FROM "${GEN_NAME}.generator"
                       PARAMS "${PARAMS_${GEN_NAME}}"
                       TARGETS "${Halide_TARGET_${GEN_NAME}}"
                       FUNCTION_NAME "${FN_NAME_${GEN_NAME}}"
                       FEATURES "${FEATURES_${GEN_NAME}}"
                       ${GRADS_${GEN_NAME}})
endforeach ()

target_link_libraries(external_code.generator PRIVATE external_code_generator_deps)

##
# Define a nontrivial dependency for external_code.generator
##

set(EXTERNAL_CPP "${CMAKE_CURRENT_SOURCE_DIR}/external_code_extern.cpp")

set(EC32 "external_code_extern_bitcode_32")
add_custom_command(OUTPUT "${EC32}.bc"
                   DEPENDS "${EXTERNAL_CPP}"
                   VERBATIM
                   COMMAND clang -O3 -c -m32 -target le32-unknown-nacl-unknown -emit-llvm "$<SHELL_PATH:${EXTERNAL_CPP}>" -o "${EC32}.bc")
add_custom_command(OUTPUT "${EC32}.cpp"
                   DEPENDS "${EC32}.bc" binary2cpp
                   COMMAND binary2cpp external_code_extern_bitcode_32 < "${EC32}.bc" > "${EC32}.cpp")

set(EC64 "external_code_extern_bitcode_64")
add_custom_command(OUTPUT "${EC64}.bc"
                   DEPENDS "${EXTERNAL_CPP}" binary2cpp
                   VERBATIM
                   COMMAND clang -O3 -c -m64 -target le64-unknown-unknown-unknown -emit-llvm "$<SHELL_PATH:${EXTERNAL_CPP}>" -o "${EC64}.bc")
add_custom_command(OUTPUT "${EC64}.cpp"
                   DEPENDS "${EC64}.bc" binary2cpp
                   COMMAND binary2cpp external_code_extern_bitcode_64 < "${EC64}.bc" > "${EC64}.cpp")

set(ECCPP "external_code_extern_cpp_source")
add_custom_command(OUTPUT "${ECCPP}.cpp"
                   DEPENDS "${EXTERNAL_CPP}" binary2cpp
                   VERBATIM
                   COMMAND binary2cpp external_code_extern_cpp_source < "$<SHELL_PATH:${EXTERNAL_CPP}>" > "${ECCPP}.cpp")

add_library(external_code_generator_deps OBJECT "${EC32}.cpp" "${EC64}.cpp" "${ECCPP}.cpp")

##
# Create targets for the default AOT tests
##

halide_define_aot_test(argvcall)
halide_define_aot_test(buffer_copy)
halide_define_aot_test(can_use_target)
#halide_define_aot_test(cleanup_on_error)  # TODO: requires access to internal header runtime/device_interface.h
halide_define_aot_test(configure)
halide_define_aot_test(embed_image)
halide_define_aot_test(error_codes)
halide_define_aot_test(example)
halide_define_aot_test(external_code)
halide_define_aot_test(float16_t)
halide_define_aot_test(gpu_object_lifetime)
halide_define_aot_test(gpu_only)
halide_define_aot_test(image_from_array)
halide_define_aot_test(mandelbrot)
halide_define_aot_test(memory_profiler_mandelbrot)
halide_define_aot_test(msan)
halide_define_aot_test(multitarget)
halide_define_aot_test(opencl_runtime)
halide_define_aot_test(output_assign)
halide_define_aot_test(pyramid)
halide_define_aot_test(string_param)
halide_define_aot_test(user_context)
halide_define_aot_test(user_context_insanity)
halide_define_aot_test(variable_num_threads)

##
# Create targets for the AOT tests with custom rules
##

# Acquire-Release test requires CUDA
halide_define_aot_test(acquire_release)
if (TARGET_PTX AND Halide_TARGET MATCHES "cuda")
    include(AddCudaToTarget)
    add_cuda_to_target(generator_aot_acquire_release PRIVATE)
endif ()
if (TARGET_PTX AND Halide_TARGET MATCHES "opencl")
    find_package(OpenCL REQUIRED)
    target_link_libraries(generator_aot_acquire_release PRIVATE OpenCL::OpenCL)
endif ()

# Alias generator test
add_halide_library(alias_with_offset_42
                   FROM alias.generator
                   GENERATOR alias_with_offset_42)
halide_define_aot_test(alias EXTRA_LIBS alias_with_offset_42)

# Autograd generator test
halide_define_aot_test(autograd)
if (TARGET generator_aot_autograd)
    add_halide_library(autograd_grad
                       GRADIENT_DESCENT
                       FROM autograd.generator
                       GENERATOR autograd
                       PARAMS "auto_schedule=true")
    target_link_libraries(generator_aot_autograd PRIVATE autograd_grad)
endif()

# CXX mangling generator test.
halide_define_aot_test(cxx_mangling)
if (TARGET cxx_mangling)
    target_link_libraries(cxx_mangling INTERFACE cxx_mangling_externs)
    if (TARGET_PTX AND Halide_TARGET MATCHES "cuda")
        add_halide_library(cxx_mangling_gpu
                           FROM cxx_mangling.generator
                           GENERATOR cxx_mangling
                           FUNCTION_NAME HalideTest::cxx_mangling_gpu
                           FEATURES c_plus_plus_name_mangling cuda)
        target_link_libraries(generator_aot_cxx_mangling PRIVATE cxx_mangling_gpu)
    endif ()
endif ()

# CXX mangling with extern generator test
halide_define_aot_test(cxx_mangling_define_extern)
if (TARGET cxx_mangling_define_extern)
    target_link_libraries(cxx_mangling_define_extern INTERFACE cxx_mangling_externs cxx_mangling_define_extern_externs)
    target_link_libraries(cxx_mangling_define_extern_externs PRIVATE cxx_mangling_externs cxx_mangling)
endif ()

# Define extern for OpenCL
halide_define_aot_test(define_extern_opencl)
if (TARGET_PTX AND Halide_TARGET MATCHES "opencl")
    find_package(OpenCL REQUIRED)
    target_link_libraries(generator_aot_define_extern_opencl PRIVATE OpenCL::OpenCL)
endif ()

# Matlab generator test
halide_define_aot_test(matlab)
if (TARGET generator_aot_matlab)
  set_target_properties(generator_aot_matlab PROPERTIES ENABLE_EXPORTS True)
endif ()

# Metadata tester
add_halide_library(metadata_tester_ucon
                   FROM metadata_tester.generator
                   GENERATOR metadata_tester
                   FEATURES user_context
                   PARAMS ${PARAMS_metadata_tester})
halide_define_aot_test(metadata_tester EXTRA_LIBS metadata_tester_ucon)

# Nested externs test
add_halide_library(nested_externs_root FROM nested_externs.generator)
add_halide_library(nested_externs_inner FROM nested_externs.generator)
add_halide_library(nested_externs_combine FROM nested_externs.generator)
add_halide_library(nested_externs_leaf FROM nested_externs.generator)
halide_define_aot_test(nested_externs
                       OMIT_DEFAULT_GENERATOR
                       EXTRA_LIBS nested_externs_root nested_externs_inner nested_externs_combine nested_externs_leaf)

# Tiled blur
halide_define_aot_test(tiled_blur EXTRA_LIBS blur2x2)
