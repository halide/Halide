##
# Convenience method for defining test cases in this directory.
##

# TODO(#4938): remove need for these definitions
function(_add_gpu_deps TARGET)
    if ("${Halide_TARGET}" MATCHES "opencl")
        target_compile_definitions("${TARGET}" PRIVATE TEST_OPENCL)
    endif ()
    if ("${Halide_TARGET}" MATCHES "metal")
        target_compile_definitions("${TARGET}" PRIVATE TEST_METAL)
    endif ()
    if ("${Halide_TARGET}" MATCHES "cuda")
        target_compile_definitions("${TARGET}" PRIVATE TEST_CUDA)
    endif ()
endfunction()

function(_add_aot_test TARGET)
    set(options "")
    set(oneValueArgs "")
    set(multiValueArgs SRCS DEPS GROUPS DEFINES)
    cmake_parse_arguments(args "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    add_executable("${TARGET}" ${args_SRCS})
    target_compile_definitions("${TARGET}" PRIVATE ${args_DEFINES})
    target_include_directories("${TARGET}" PRIVATE "${Halide_SOURCE_DIR}/test/common" "${Halide_SOURCE_DIR}/tools")
    target_link_libraries("${TARGET}" PRIVATE ${args_DEPS})
    _add_gpu_deps("${TARGET}")
    add_halide_test("${TARGET}" GROUPS generator ${args_GROUPS})
endfunction()

# Emit two halide_library targets, one with the default backend with the given name,
# and (optionally) one with the C++ backend with the name NAME_cpp. (The CPP one defaults to being
# emitted, but can be skipped if OMIT_C_BACKEND is specified.)

# Arguments are (mostly) identical to add_halide_library(), except:
# - OMIT_C_BACKEND option to skip the CPP backend
# - DEPS_OUT and DEPS_CPP_OUT as optional out-params, to receive the deps needed for using the standard / cpp outputs
# - LINK_TO_AOTTEST (if specified) will link the outputs to the relevant generator_aot_NAME/generator_aotcpp_NAME test
#   (which must be defined prior to this rule).
#
function(_add_halide_libraries TARGET)
    set(options GRADIENT_DESCENT OMIT_C_BACKEND)
    set(oneValueArgs FROM GENERATOR FUNCTION_NAME NAMESPACE USE_RUNTIME AUTOSCHEDULER HEADER EXTRA_OUTPUTS DEPS_OUT DEPS_CPP_OUT LINK_TO_AOTTEST)
    set(multiValueArgs GENERATOR_LINK_LIBRARIES TARGETS FEATURES PARAMS PLUGINS)
    cmake_parse_arguments(args "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    set(EXTRA_OUTPUTS "")
    foreach (EO IN LISTS args_EXTRA_OUTPUTS)
        list(APPEND EXTRA_OUTPUTS ${EO} ${EO}_PATH_OUT)
    endforeach()

    # Passing on a no-value arg in CMake is unpleasant
    if (args_GRADIENT_DESCENT)
        set(GRADIENT_DESCENT_OPT "GRADIENT_DESCENT")
    else()
        set(GRADIENT_DESCENT_OPT "")
    endif()

    # Fill in default values for some arguments, as needed
    if (NOT args_FROM)
        add_halide_generator("${TARGET}.generator"
                             SOURCES "${TARGET}_generator.cpp"
                             LINK_LIBRARIES ${args_GENERATOR_LINK_LIBRARIES})
        set(args_FROM "${TARGET}.generator")
    endif()
    if (NOT args_GENERATOR)
        set(args_GENERATOR "${TARGET}")
    endif()
    if (NOT args_FUNCTION_NAME)
        set(args_FUNCTION_NAME "${TARGET}")
    endif()

    set(TARGET_CPP "${TARGET}_cpp")
    set(_DEPS_CPP )
    set(_DEPS )

    add_halide_library(${TARGET}
                       ${GRADIENT_DESCENT_OPT}
                       FROM "${args_FROM}"
                       GENERATOR "${args_GENERATOR}"
                       FUNCTION_NAME "${args_FUNCTION_NAME}"
                       NAMESPACE "${args_NAMESPACE}"
                       USE_RUNTIME "${args_USE_RUNTIME}"
                       AUTOSCHEDULER "${args_AUTOSCHEDULER}"
                       USE_RUNTIME "${args_USE_RUNTIME}"
                       TARGETS "${args_TARGETS}"
                       FEATURES "${args_FEATURES}"
                       PARAMS "${args_PARAMS}"
                       PLUGINS "${args_PLUGINS}"
                       ${EXTRA_OUTPUTS})
    list(APPEND _DEPS ${TARGET} ${TARGET}.runtime)
    if (args_LINK_TO_AOTTEST)
            target_link_libraries("generator_aot_${args_LINK_TO_AOTTEST}" PRIVATE ${_DEPS})
    endif()

    if (NOT args_OMIT_C_BACKEND)
        # The C backend basically ignores TARGETS (it emits a warning that the sources
        # will be compiled with the current CMake toolchain)... but making matters worse,
        # if you specify multiple targets here, you'll fail in compile_multitarget() because
        # it wants object file to be generated. We'll just dodge all that by deliberately
        # omitting the TARGETS argument here entirely.
        add_halide_library(${TARGET_CPP}
                           C_BACKEND
                           ${GRADIENT_DESCENT_OPT}
                           FROM "${args_FROM}"
                           GENERATOR "${args_GENERATOR}"
                           FUNCTION_NAME "${args_FUNCTION_NAME}"
                           NAMESPACE "${args_NAMESPACE}"
                           USE_RUNTIME "${args_USE_RUNTIME}"
                           AUTOSCHEDULER "${args_AUTOSCHEDULER}"
                           USE_RUNTIME "${args_USE_RUNTIME}"
                           # No: see comment above
                           # TARGETS "${args_TARGETS}"
                           FEATURES "${args_FEATURES}"
                           PARAMS "${args_PARAMS}"
                           PLUGINS "${args_PLUGINS}"
                           ${EXTRA_OUTPUTS})
        list(APPEND _DEPS_CPP ${TARGET_CPP} ${TARGET}.runtime)
        if (args_LINK_TO_AOTTEST)
            target_link_libraries("generator_aotcpp_${args_LINK_TO_AOTTEST}" PRIVATE ${_DEPS_CPP})
        endif()
        # This is a bit subtle: we end up emitting NAME.h and NAME_cpp.h header files;
        # these are *identical* in content (aside from the guard macro and the filename).
        # For convenience, we use the NAME.h variant in all cases downstream (even when linking
        # with the NAME_cpp.a output), but to make that reliable, we must ensure that TARGET
        # is always generated before TARGET_CPP (so that anything depending on TARGET_CPP can rely
        # on NAME.h already being generated.) This is a bit suboptimal, but it is arguably better
        # that having conditionalized #includes in all the downstream test targets.
        add_dependencies(${TARGET_CPP} ${TARGET})
    endif()

    if (args_DEPS_OUT)
        set(${args_DEPS_OUT} ${_DEPS} PARENT_SCOPE)
    endif()
    if (args_DEPS_CPP_OUT)
        set(${args_DEPS_CPP_OUT} ${_DEPS_CPP} PARENT_SCOPE)
    endif()
endfunction()

function(halide_define_aot_test NAME)
    set(options OMIT_DEFAULT_HALIDE_LIBRARY OMIT_C_BACKEND)
    set(oneValueArgs FUNCTION_NAME)
    set(multiValueArgs GENERATOR_LINK_LIBRARIES EXTRA_LIBS ENABLE_IF FEATURES PARAMS TARGETS GROUPS EXTRA_OUTPUTS)
    cmake_parse_arguments(args "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (args_ENABLE_IF AND NOT (${args_ENABLE_IF}))
        return()
    endif ()

    # Apparently CMake has no one-liner for 'assign bool value to var'?
    set(TARGETING_WASM FALSE)
    if (TARGET_WEBASSEMBLY AND Halide_TARGET MATCHES "wasm")
        set(TARGETING_WASM FALSE)
    endif()

    # Always omit C++ backend testing when we are targeting wasm
    if (TARGETING_WASM)
        set(args_OMIT_C_BACKEND TRUE)
    endif()

    set(TARGET "generator_aot_${NAME}")
    set(TARGET_CPP "generator_aotcpp_${NAME}")

    if (TARGETING_WASM)
        add_wasm_executable("${TARGET}"
                            SRCS "${HALIDE_LIBRARY}_aottest.cpp"
                            INCLUDES
                            "${Halide_BINARY_DIR}/include"
                            "${Halide_SOURCE_DIR}/test/common"
                            "${Halide_SOURCE_DIR}/tools"
                            "${CMAKE_CURRENT_BINARY_DIR}")
        add_wasm_halide_test("${TARGET}" GROUPS generator "${args_GROUPS}")
        return()
    endif()

    _add_aot_test("${TARGET}"
                  SRCS "${NAME}_aottest.cpp"
                  DEPS ${args_EXTRA_LIBS}
                  GROUPS ${args_GROUPS})
    if (NOT args_OMIT_C_BACKEND)
        _add_aot_test("${TARGET_CPP}"
                      SRCS "${NAME}_aottest.cpp"
                      DEPS ${args_EXTRA_LIBS}
                      GROUPS ${args_GROUPS})
    endif()

    if (NOT args_OMIT_DEFAULT_HALIDE_LIBRARY)
        if (args_OMIT_C_BACKEND)
            set(OMIT_C_BACKEND_OPT "OMIT_C_BACKEND")
        else()
            set(OMIT_C_BACKEND_OPT "")
        endif()
        _add_halide_libraries("${NAME}"
                ${OMIT_C_BACKEND_OPT}
                LINK_TO_AOTTEST "${NAME}"
                EXTRA_OUTPUTS ${args_EXTRA_OUTPUTS}
                FEATURES ${args_FEATURES}
                FUNCTION_NAME ${args_FUNCTION_NAME}
                GENERATOR_LINK_LIBRARIES ${args_GENERATOR_LINK_LIBRARIES}
                TARGETS ${args_TARGETS}
                PARAMS ${args_PARAMS})
    endif()
endfunction()

##
# Some tests are not available when compiling for WASM.
##

set(USING_WASM (TARGET_WEBASSEMBLY AND Halide_TARGET MATCHES "wasm"))

if (TARGET_NVPTX AND Halide_TARGET MATCHES "cuda")
    find_package(CUDAToolkit REQUIRED)
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
    target_link_libraries(generator_aot_acquire_release PRIVATE CUDA::cuda_driver CUDA::cudart)
    target_link_libraries(generator_aotcpp_acquire_release PRIVATE CUDA::cuda_driver CUDA::cudart)
endif ()
if (TARGET_NVPTX AND Halide_TARGET MATCHES "opencl")
    target_link_libraries(generator_aot_acquire_release PRIVATE OpenCL::OpenCL)
    target_link_libraries(generator_aotcpp_acquire_release PRIVATE OpenCL::OpenCL)
endif ()

# TODO: what are these?
# configure_jittest.cpp
# example_jittest.cpp
# registration_test.cpp
# rungen_test.cpp

# alias_aottest.cpp
# alias_generator.cpp
set(ALIAS_LIBS alias_with_offset_42 alias_Adams2019 alias_Li2018 alias_Mullapudi2016)
halide_define_aot_test(alias EXTRA_LIBS ${ALIAS_LIBS})
foreach (LIB IN LISTS ALIAS_LIBS)
    _add_halide_libraries(${LIB}
                          LINK_TO_AOTTEST alias
                          FROM alias.generator
                          GENERATOR ${LIB}
                          PLUGINS Halide::Adams2019 Halide::Li2018 Halide::Mullapudi2016)
endforeach ()

# argvcall_aottest.cpp
# argvcall_generator.cpp
halide_define_aot_test(argvcall)

# async_parallel_aottest.cpp
# async_parallel_generator.cpp
halide_define_aot_test(async_parallel
                       # Requires threading support, not yet available for wasm tests
                       ENABLE_IF NOT ${USING_WASM}
                       FEATURES user_context
                       GROUPS multithreaded)

# autograd_aottest.cpp
# autograd_generator.cpp
halide_define_aot_test(autograd ENABLE_IF TARGET Halide::Mullapudi2016 AND NOT ${USING_WASM}
                       GROUPS multithreaded)
_add_halide_libraries(autograd_grad
                      LINK_TO_AOTTEST autograd
                      GRADIENT_DESCENT
                      FROM autograd.generator
                      GENERATOR autograd
                      FUNCTION_NAME autograd_grad
                      AUTOSCHEDULER Halide::Mullapudi2016
                      PLUGINS Halide::Mullapudi2016)

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
halide_define_aot_test(cleanup_on_error)
# Alas, this test requires direct access to internal header runtime/device_interface.h
target_include_directories(generator_aot_cleanup_on_error PRIVATE "${Halide_SOURCE_DIR}/src/runtime")
target_include_directories(generator_aotcpp_cleanup_on_error PRIVATE "${Halide_SOURCE_DIR}/src/runtime")

# configure_aottest.cpp
# configure_generator.cpp
halide_define_aot_test(configure)

if (NOT ${USING_WASM})

    # cxx_mangling_externs.cpp
    add_library(cxx_mangling_externs STATIC cxx_mangling_externs.cpp)

    # cxx_mangling_aottest.cpp
    # cxx_mangling_generator.cpp
    halide_define_aot_test(cxx_mangling
                           FUNCTION_NAME HalideTest::AnotherNamespace::cxx_mangling
                           FEATURES c_plus_plus_name_mangling
                           EXTRA_OUTPUTS FUNCTION_INFO_HEADER)
    target_link_libraries(cxx_mangling INTERFACE cxx_mangling_externs)
    target_link_libraries(cxx_mangling_cpp INTERFACE cxx_mangling_externs)
    if (TARGET_NVPTX AND Halide_TARGET MATCHES "cuda")
        add_halide_library(cxx_mangling_gpu
                           FROM cxx_mangling.generator
                           GENERATOR cxx_mangling
                           FUNCTION_NAME HalideTest::cxx_mangling_gpu
                           FEATURES c_plus_plus_name_mangling cuda)
        target_link_libraries(generator_aot_cxx_mangling PRIVATE cxx_mangling_gpu)
        target_link_libraries(generator_aot_cxx_mangling_cpp PRIVATE cxx_mangling_gpu)
    endif ()

    # cxx_mangling_define_extern_externs.cpp
    add_library(cxx_mangling_define_extern_externs STATIC cxx_mangling_define_extern_externs.cpp)
    target_link_libraries(cxx_mangling_define_extern_externs PRIVATE cxx_mangling)
    add_dependencies(cxx_mangling_define_extern_externs cxx_mangling)

    # cxx_mangling_define_extern_aottest.cpp
    # cxx_mangling_define_extern_generator.cpp
    halide_define_aot_test(cxx_mangling_define_extern
                           FUNCTION_NAME "HalideTest::cxx_mangling_define_extern"
                           FEATURES c_plus_plus_name_mangling user_context)
    target_link_libraries(cxx_mangling_define_extern INTERFACE cxx_mangling_define_extern_externs)
    target_link_libraries(cxx_mangling_define_extern_externs PRIVATE cxx_mangling)
    target_link_libraries(cxx_mangling_define_extern_cpp INTERFACE cxx_mangling_define_extern_externs)
    target_link_libraries(cxx_mangling_define_extern_externs PRIVATE cxx_mangling_cpp)

endif ()  # (NOT ${USING_WASM})

# define_extern_opencl_aottest.cpp
# define_extern_opencl_generator.cpp
halide_define_aot_test(define_extern_opencl)
if (TARGET_NVPTX AND Halide_TARGET MATCHES "opencl")
    find_package(OpenCL REQUIRED)
    target_link_libraries(generator_aot_define_extern_opencl PRIVATE OpenCL::OpenCL)
    target_link_libraries(generator_aotcpp_define_extern_opencl PRIVATE OpenCL::OpenCL)
endif ()

# embed_image_aottest.cpp
# embed_image_generator.cpp
halide_define_aot_test(embed_image)

# error_codes_aottest.cpp
# error_codes_generator.cpp
halide_define_aot_test(error_codes)

# example_aottest.cpp
# example_generator.cpp
halide_define_aot_test(example
                       GROUPS multithreaded)

# extern_output_aottest.cpp
# extern_output_generator.cpp
halide_define_aot_test(extern_output
                       GROUPS multithreaded)

# float16_t_aottest.cpp
# float16_t_generator.cpp
halide_define_aot_test(float16_t)

# gpu_multi_context_threaded_aottest.cpp
# gpu_multi_context_threaded_generator.cpp
# (Doesn't build/link properly under wasm, and isn't useful there anyway)
if (NOT Halide_TARGET MATCHES "wasm")
    halide_define_aot_test(gpu_multi_context_threaded
                           OMIT_DEFAULT_HALIDE_LIBRARY)

    # Explicitly define our Generator here since OMIT_DEFAULT_HALIDE_LIBRARY
    # was specified
    add_halide_generator(gpu_multi_context_threaded.generator
                         SOURCES gpu_multi_context_threaded_generator.cpp)

    _add_halide_libraries(gpu_multi_context_threaded_add
                          LINK_TO_AOTTEST gpu_multi_context_threaded
                          FROM gpu_multi_context_threaded.generator
                          FEATURES user_context)

    _add_halide_libraries(gpu_multi_context_threaded_mul
                          LINK_TO_AOTTEST gpu_multi_context_threaded
                          FROM gpu_multi_context_threaded.generator
                          FEATURES user_context)

    if (TARGET_NVPTX AND Halide_TARGET MATCHES "cuda")
        target_link_libraries(generator_aot_gpu_multi_context_threaded PRIVATE CUDA::cuda_driver CUDA::cudart)
        target_link_libraries(generator_aotcpp_gpu_multi_context_threaded PRIVATE CUDA::cuda_driver CUDA::cudart)
    endif ()
    if (TARGET_NVPTX AND Halide_TARGET MATCHES "opencl")
        target_link_libraries(generator_aot_gpu_multi_context_threaded PRIVATE OpenCL::OpenCL)
        target_link_libraries(generator_aotcpp_gpu_multi_context_threaded PRIVATE OpenCL::OpenCL)
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
halide_define_aot_test(mandelbrot
                       GROUPS multithreaded)

# memory_profiler_mandelbrot_aottest.cpp
# memory_profiler_mandelbrot_generator.cpp
halide_define_aot_test(memory_profiler_mandelbrot
                       # Requires profiler support (which requires threading), not yet available for wasm tests...
                       ENABLE_IF NOT ${USING_WASM}
                       # ... or the C backend (https://github.com/halide/Halide/issues/7272)
                       OMIT_C_BACKEND
                       FEATURES profile
                       GROUPS multithreaded)

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

# Note that metadata_tester (but not metadata_tester_ucon) is built as "multitarget" to verify that
# the metadata names are correctly emitted.
if (${USING_WASM})
    # wasm doesn't support multitargets
    # TODO: currently, Halide_CMAKE_TARGET == Halide_HOST_TARGET when building for Emscripten; we should fix this
    set(MDT_TARGETS ${Halide_TARGET})
else()
    set(MDT_TARGETS cmake-no_bounds_query cmake)
endif()

halide_define_aot_test(metadata_tester
                       EXTRA_LIBS metadata_tester_ucon
                       PARAMS ${metadata_tester_params}
                       TARGETS ${MDT_TARGETS}
                       EXTRA_OUTPUTS FUNCTION_INFO_HEADER)
_add_halide_libraries(metadata_tester_ucon
                      LINK_TO_AOTTEST metadata_tester
                      FROM metadata_tester.generator
                      GENERATOR metadata_tester
                      FEATURES user_context
                      PARAMS ${metadata_tester_params}
                      FUNCTION_INFO_HEADER metadata_tester_ucon_path)

# msan_aottest.cpp
# msan_generator.cpp
halide_define_aot_test(msan FEATURES msan
                       # Broken for C++ backend (https://github.com/halide/Halide/issues/7273)
                       OMIT_C_BACKEND
                       GROUPS multithreaded)

# (Doesn't build/link properly on windows / under wasm)
if (NOT Halide_TARGET MATCHES "windows" AND NOT CMAKE_SYSTEM_NAME MATCHES "Windows" AND NOT Halide_TARGET MATCHES "wasm")
    # sanitizercoverage_aottest.cpp
    # sanitizercoverage_generator.cpp
    halide_define_aot_test(sanitizercoverage
                           # sanitizercoverage relies on LLVM-specific hooks, so it will never work with the C backend
                           OMIT_C_BACKEND
                           FEATURES sanitizer_coverage)
endif ()

# multitarget_aottest.cpp
# multitarget_generator.cpp
halide_define_aot_test(multitarget
                       # Multitarget doesn't apply to WASM...
                       ENABLE_IF NOT ${USING_WASM}
                       # ... or to the C backend
                       OMIT_C_BACKEND
                       TARGETS cmake-no_bounds_query cmake
                       FEATURES c_plus_plus_name_mangling
                       FUNCTION_NAME HalideTest::multitarget)

# nested_externs_aottest.cpp
# nested_externs_generator.cpp
halide_define_aot_test(nested_externs
                       OMIT_DEFAULT_HALIDE_LIBRARY
                       EXTRA_LIBS
                       nested_externs_root
                       nested_externs_inner
                       nested_externs_combine
                       nested_externs_leaf
                       nested_externs_root.runtime)

# Explicitly define our Generator here since OMIT_DEFAULT_HALIDE_LIBRARY
# was specified
add_halide_generator(nested_externs.generator SOURCES nested_externs_generator.cpp)
_add_halide_libraries(nested_externs_root FROM nested_externs.generator LINK_TO_AOTTEST nested_externs)
_add_halide_libraries(nested_externs_inner FROM nested_externs.generator LINK_TO_AOTTEST nested_externs)
_add_halide_libraries(nested_externs_combine FROM nested_externs.generator LINK_TO_AOTTEST nested_externs)
_add_halide_libraries(nested_externs_leaf FROM nested_externs.generator LINK_TO_AOTTEST nested_externs)

# opencl_runtime_aottest.cpp
# opencl_runtime_generator.cpp
halide_define_aot_test(opencl_runtime)

# output_assign_aottest.cpp
# output_assign_generator.cpp
halide_define_aot_test(output_assign)

# pyramid_aottest.cpp
# pyramid_generator.cpp
halide_define_aot_test(pyramid PARAMS levels=10
                       GROUPS multithreaded)

# rdom_input_aottest.cpp
# rdom_input_generator.cpp
halide_define_aot_test(rdom_input)

# string_param_aottest.cpp
# string_param_generator.cpp
halide_define_aot_test(string_param PARAMS "rpn_expr=5 y * x +")

# stubtest_aottest.cpp
# stubtest_generator.cpp
# stubtest_jittest.cpp
# TODO: stubs not supported in CMake

# stubuser_aottest.cpp
# stubuser_generator.cpp
# TODO: stubs not supported in CMake

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
halide_define_aot_test(user_context FEATURES user_context
                       GROUPS multithreaded)

# user_context_insanity_aottest.cpp
# user_context_insanity_generator.cpp
halide_define_aot_test(user_context_insanity FEATURES user_context
                       GROUPS multithreaded)

# variable_num_threads_aottest.cpp
# variable_num_threads_generator.cpp
halide_define_aot_test(variable_num_threads
                       # Requires threading support, not yet available for wasm tests
                       ENABLE_IF NOT ${USING_WASM}
                       GROUPS multithreaded)
