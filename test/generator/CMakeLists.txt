##
# Convenience methods for defining test cases in this directory.
##

##
# Some tests are not available when compiling for WASM.
##

if (TARGET_WEBASSEMBLY AND Halide_TARGET MATCHES "wasm")
    set(_USING_WASM 1)
else()
    set(_USING_WASM 0)
endif()

# Emit two halide_library targets, one with the default backend with the given name,
# and (optionally) one with the C++ backend with the name NAME_cpp. (The CPP one defaults to being
# emitted, but can be skipped if OMIT_C_BACKEND is specified.)

# Arguments are (mostly) identical to add_halide_library(), except:
# - OMIT_C_BACKEND option to skip the CPP backend
# - We always generate FUNCTION_INFO_HEADER
#
function(_add_halide_libraries TARGET)
    set(options GRADIENT_DESCENT OMIT_C_BACKEND)
    set(oneValueArgs FROM GENERATOR_NAME FUNCTION_NAME NAMESPACE USE_RUNTIME AUTOSCHEDULER)
    set(multiValueArgs ENABLE_IF TARGETS FEATURES PARAMS PLUGINS EXTERNS)
    cmake_parse_arguments(args "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (args_ENABLE_IF AND NOT (${args_ENABLE_IF}))
        return()
    endif ()

    # Passing on a no-value arg in CMake is unpleasant
    if (args_GRADIENT_DESCENT)
        set(GRADIENT_DESCENT_OPT "GRADIENT_DESCENT")
    else()
        set(GRADIENT_DESCENT_OPT "")
    endif()

    # Fill in default values for some arguments, as needed
    if (NOT args_FROM)
        add_halide_generator("${TARGET}.generator"
                             SOURCES "${TARGET}_generator.cpp")
        set(args_FROM "${TARGET}.generator")
    endif()
    if (NOT args_GENERATOR_NAME)
        set(args_GENERATOR_NAME "${TARGET}")
    endif()
    if (NOT args_FUNCTION_NAME)
        set(args_FUNCTION_NAME "${TARGET}")
    endif()

    set(TARGET_CPP "${TARGET}_cpp")

    add_halide_library(${TARGET}
                       ${GRADIENT_DESCENT_OPT}
                       FROM "${args_FROM}"
                       GENERATOR "${args_GENERATOR_NAME}"
                       FUNCTION_NAME "${args_FUNCTION_NAME}"
                       NAMESPACE "${args_NAMESPACE}"
                       USE_RUNTIME "${args_USE_RUNTIME}"
                       AUTOSCHEDULER "${args_AUTOSCHEDULER}"
                       TARGETS "${args_TARGETS}"
                       FEATURES "${args_FEATURES}"
                       PARAMS "${args_PARAMS}"
                       PLUGINS "${args_PLUGINS}"
                       FUNCTION_INFO_HEADER function_info_header_out)
    if (args_EXTERNS)
        target_link_libraries(${TARGET} INTERFACE ${args_EXTERNS})
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
                           GENERATOR "${args_GENERATOR_NAME}"
                           FUNCTION_NAME "${args_FUNCTION_NAME}"
                           NAMESPACE "${args_NAMESPACE}"
                           AUTOSCHEDULER "${args_AUTOSCHEDULER}"
                           USE_RUNTIME "${args_USE_RUNTIME}"
                           # No: see comment above
                           # TARGETS "${args_TARGETS}"
                           FEATURES "${args_FEATURES}"
                           PARAMS "${args_PARAMS}"
                           PLUGINS "${args_PLUGINS}"
                           FUNCTION_INFO_HEADER function_info_header_cpp_out)
        if (args_EXTERNS)
            target_link_libraries(${TARGET_CPP} INTERFACE ${args_EXTERNS})
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
endfunction()

function(_add_one_aot_test TARGET)
    set(options "")
    set(oneValueArgs "")
    set(multiValueArgs SRCS DEPS GROUPS INCLUDES DEFINES)
    cmake_parse_arguments(args "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    add_executable("${TARGET}" ${args_SRCS})
    target_compile_definitions("${TARGET}" PRIVATE ${args_DEFINES})
    target_include_directories("${TARGET}" PRIVATE ${args_INCLUDES} "${Halide_SOURCE_DIR}/test/common" "${Halide_SOURCE_DIR}/tools")
    target_link_libraries("${TARGET}" PRIVATE ${args_DEPS})
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
    if (TARGET_NVPTX AND Halide_TARGET MATCHES "cuda")
        target_link_libraries("${TARGET}" PRIVATE CUDA::cuda_driver CUDA::cudart)
    endif ()
    if (TARGET_NVPTX AND Halide_TARGET MATCHES "opencl")
        target_link_libraries("${TARGET}" PRIVATE OpenCL::OpenCL)
    endif ()
    add_halide_test("${TARGET}" GROUPS generator ${args_GROUPS})
endfunction()

function(_add_halide_aot_tests NAME)
    set(options OMIT_C_BACKEND)
    set(oneValueArgs "")
    set(multiValueArgs ENABLE_IF GROUPS INCLUDES HALIDE_LIBRARIES HALIDE_RUNTIME DEPS)
    cmake_parse_arguments(args "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (args_ENABLE_IF AND NOT (${args_ENABLE_IF}))
        return()
    endif ()

    # If HALIDE_LIBRARIES is omitted, assume we want just a single halide_library target
    # with a matching name
    if (NOT args_HALIDE_LIBRARIES)
        set(args_HALIDE_LIBRARIES "${NAME}")
    endif()

    # If no Halide runtime specified, use one from the first halide_library in the list
    if (NOT args_HALIDE_RUNTIME)
        list(GET args_HALIDE_LIBRARIES 0 HR)
        set(args_HALIDE_RUNTIME "${HR}.runtime")
    endif()

    set(TARGET "generator_aot_${NAME}")
    set(TARGET_CPP "generator_aotcpp_${NAME}")

    if (${_USING_WASM})
        add_wasm_executable("${TARGET}"
                            SRCS "${NAME}_aottest.cpp"
                            DEPS ${args_HALIDE_LIBRARIES} ${args_HALIDE_RUNTIME} ${args_DEPS}
                            INCLUDES
                            ${args_INCLUDES}
                            "${Halide_BINARY_DIR}/include"
                            "${Halide_SOURCE_DIR}/test/common"
                            "${Halide_SOURCE_DIR}/tools"
                            "${CMAKE_CURRENT_BINARY_DIR}")
        add_wasm_halide_test("${TARGET}" GROUPS generator "${args_GROUPS}")
    else()
        _add_one_aot_test("${TARGET}"
                          SRCS "${NAME}_aottest.cpp"
                          DEPS ${args_HALIDE_LIBRARIES} ${args_HALIDE_RUNTIME} ${args_DEPS}
                          INCLUDES ${args_INCLUDES}
                          GROUPS ${args_GROUPS})
        if (NOT args_OMIT_C_BACKEND)
            set(HALIDE_LIBRARIES_CPP "")
            foreach (hl IN LISTS args_HALIDE_LIBRARIES)
                list(APPEND HALIDE_LIBRARIES_CPP "${hl}_cpp")
            endforeach()
            _add_one_aot_test("${TARGET_CPP}"
                              SRCS "${NAME}_aottest.cpp"
                              DEPS ${HALIDE_LIBRARIES_CPP} ${args_HALIDE_RUNTIME} ${args_DEPS}
                              INCLUDES ${args_INCLUDES}
                              GROUPS ${args_GROUPS})
        endif()
    endif()
endfunction()

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
_add_halide_libraries(acquire_release)
_add_halide_aot_tests(acquire_release)

# TODO: what are these?
# configure_jittest.cpp
# example_jittest.cpp
# registration_test.cpp
# rungen_test.cpp

# alias_aottest.cpp
# alias_generator.cpp
set(EXTRA_ALIAS_LIBS alias_with_offset_42 alias_Adams2019 alias_Li2018 alias_Mullapudi2016)
_add_halide_libraries(alias)
foreach (LIB IN LISTS EXTRA_ALIAS_LIBS)
    _add_halide_libraries(${LIB}
                          FROM alias.generator
                          GENERATOR_NAME ${LIB}
                          PLUGINS Halide::Adams2019 Halide::Li2018 Halide::Mullapudi2016)
endforeach ()
_add_halide_aot_tests(alias
                      HALIDE_LIBRARIES alias ${EXTRA_ALIAS_LIBS})

# argvcall_aottest.cpp
# argvcall_generator.cpp
_add_halide_libraries(argvcall)
_add_halide_aot_tests(argvcall)

# async_parallel_aottest.cpp
# async_parallel_generator.cpp
_add_halide_libraries(async_parallel FEATURES user_context)
_add_halide_aot_tests(async_parallel
                      # Requires threading support, not yet available for wasm tests
                      ENABLE_IF NOT ${_USING_WASM}
                      GROUPS multithreaded)

# autograd_aottest.cpp
# autograd_generator.cpp
_add_halide_libraries(autograd)
_add_halide_libraries(autograd_grad
                      GRADIENT_DESCENT
                      FROM autograd.generator
                      GENERATOR_NAME autograd
                      FUNCTION_NAME autograd_grad
                      AUTOSCHEDULER Halide::Mullapudi2016
                      PLUGINS Halide::Mullapudi2016)
_add_halide_aot_tests(autograd
                      ENABLE_IF TARGET Halide::Mullapudi2016 AND NOT ${_USING_WASM}
                      HALIDE_LIBRARIES autograd autograd_grad
                      GROUPS multithreaded)

# abstractgeneratortest_aottest.cpp
# abstractgeneratortest_generator.cpp
_add_halide_libraries(abstractgeneratortest)
_add_halide_aot_tests(abstractgeneratortest)

# bit_operations_aottest.cpp
# bit_operations_generator.cpp
_add_halide_libraries(bit_operations)
_add_halide_aot_tests(bit_operations)

# blur2x2_aottest.cpp
# blur2x2_generator.cpp
_add_halide_libraries(blur2x2)
_add_halide_aot_tests(blur2x2)

# buffer_copy_aottest.cpp
# buffer_copy_generator.cpp
_add_halide_libraries(buffer_copy)
_add_halide_aot_tests(buffer_copy)

# can_use_target_aottest.cpp
# can_use_target_generator.cpp
_add_halide_libraries(can_use_target)
_add_halide_aot_tests(can_use_target)

# cleanup_on_error_aottest.cpp
# cleanup_on_error_generator.cpp
_add_halide_libraries(cleanup_on_error)
_add_halide_aot_tests(cleanup_on_error
                      # Alas, this test requires direct access to
                      # internal header runtime/device_interface.h
                      INCLUDES "${Halide_SOURCE_DIR}/src/runtime")

# configure_aottest.cpp
# configure_generator.cpp
_add_halide_libraries(configure)
_add_halide_aot_tests(configure)

# TODO: this could be made to work under wasm with some tweaking, it's just build-rule complexity
if (NOT ${_USING_WASM})
    # cxx_mangling_externs.cpp
    add_library(cxx_mangling_externs STATIC cxx_mangling_externs.cpp)

    # cxx_mangling_aottest.cpp
    # cxx_mangling_generator.cpp
    _add_halide_libraries(cxx_mangling
                          FUNCTION_NAME HalideTest::AnotherNamespace::cxx_mangling
                          FEATURES c_plus_plus_name_mangling
                          EXTERNS cxx_mangling_externs)
    _add_halide_aot_tests(cxx_mangling)
    if (TARGET_NVPTX AND Halide_TARGET MATCHES "cuda")
        add_halide_library(cxx_mangling_gpu
                           FROM cxx_mangling.generator
                           GENERATOR cxx_mangling
                           FUNCTION_NAME HalideTest::cxx_mangling_gpu
                           FEATURES c_plus_plus_name_mangling cuda)
        target_link_libraries(generator_aot_cxx_mangling PRIVATE cxx_mangling_gpu)
        target_link_libraries(generator_aotcpp_cxx_mangling PRIVATE cxx_mangling_gpu)
    endif ()

    # cxx_mangling_define_extern_externs.cpp
    add_library(cxx_mangling_define_extern_externs STATIC cxx_mangling_define_extern_externs.cpp)
    # Ensure that cxx_mangling.h is availabe for cxx_mangling_define_extern_externs
    target_link_libraries(cxx_mangling_define_extern_externs PRIVATE cxx_mangling)
    add_dependencies(cxx_mangling_define_extern_externs cxx_mangling)

    # cxx_mangling_define_extern_aottest.cpp
    # cxx_mangling_define_extern_generator.cpp
    _add_halide_libraries(cxx_mangling_define_extern
                          FUNCTION_NAME "HalideTest::cxx_mangling_define_extern"
                          FEATURES c_plus_plus_name_mangling user_context
                          EXTERNS cxx_mangling_define_extern_externs cxx_mangling)
    _add_halide_aot_tests(cxx_mangling_define_extern
                          HALIDE_LIBRARIES cxx_mangling_define_extern cxx_mangling)
endif ()  # (NOT ${_USING_WASM})

# define_extern_opencl_aottest.cpp
# define_extern_opencl_generator.cpp
_add_halide_libraries(define_extern_opencl)
_add_halide_aot_tests(define_extern_opencl)

# embed_image_aottest.cpp
# embed_image_generator.cpp
_add_halide_libraries(embed_image)
_add_halide_aot_tests(embed_image)

# error_codes_aottest.cpp
# error_codes_generator.cpp
_add_halide_libraries(error_codes)
_add_halide_aot_tests(error_codes)

# example_aottest.cpp
# example_generator.cpp
_add_halide_libraries(example)
_add_halide_aot_tests(example GROUPS multithreaded)

# extern_output_aottest.cpp
# extern_output_generator.cpp
_add_halide_libraries(extern_output)
_add_halide_aot_tests(extern_output GROUPS multithreaded)

# float16_t_aottest.cpp
# float16_t_generator.cpp
_add_halide_libraries(float16_t)
_add_halide_aot_tests(float16_t)

# gpu_multi_context_threaded_aottest.cpp
# gpu_multi_context_threaded_generator.cpp
# (Doesn't build/link properly under wasm, and isn't useful there anyway)
if (NOT ${_USING_WASM})
    add_halide_generator(gpu_multi_context_threaded.generator
                         SOURCES gpu_multi_context_threaded_generator.cpp)
    _add_halide_libraries(gpu_multi_context_threaded_add
                          FROM gpu_multi_context_threaded.generator
                          FEATURES user_context)
    _add_halide_libraries(gpu_multi_context_threaded_mul
                          FROM gpu_multi_context_threaded.generator
                          FEATURES user_context)
    _add_halide_aot_tests(gpu_multi_context_threaded
                          HALIDE_LIBRARIES gpu_multi_context_threaded_add gpu_multi_context_threaded_mul)
endif ()

# gpu_object_lifetime_aottest.cpp
# gpu_object_lifetime_generator.cpp
_add_halide_libraries(gpu_object_lifetime FEATURES debug)
_add_halide_aot_tests(gpu_object_lifetime)

# gpu_only_aottest.cpp
# gpu_only_generator.cpp
_add_halide_libraries(gpu_only)
_add_halide_aot_tests(gpu_only)

# gpu_texture_aottest.cpp
# gpu_texture_generator.cpp
_add_halide_libraries(gpu_texture)
_add_halide_aot_tests(gpu_texture)

# image_from_array_aottest.cpp
# image_from_array_generator.cpp
_add_halide_libraries(image_from_array)
_add_halide_aot_tests(image_from_array)

# mandelbrot_aottest.cpp
# mandelbrot_generator.cpp
_add_halide_libraries(mandelbrot)
_add_halide_aot_tests(mandelbrot GROUPS multithreaded)

# memory_profiler_mandelbrot_aottest.cpp
# memory_profiler_mandelbrot_generator.cpp
# Requires profiler support (which requires threading), not yet available for wasm tests or the C backend
# (https://github.com/halide/Halide/issues/7272)
_add_halide_libraries(memory_profiler_mandelbrot
                      ENABLE_IF NOT ${_USING_WASM}
                      OMIT_C_BACKEND
                      FEATURES profile)
_add_halide_aot_tests(memory_profiler_mandelbrot
                      ENABLE_IF NOT ${_USING_WASM}
                      OMIT_C_BACKEND
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
if (${_USING_WASM})
    # wasm doesn't support multitargets
    # TODO: currently, Halide_CMAKE_TARGET == Halide_HOST_TARGET when building for Emscripten; we should fix this
    set(MDT_TARGETS ${Halide_TARGET})
else()
    set(MDT_TARGETS cmake-no_bounds_query cmake)
endif()

_add_halide_libraries(metadata_tester
                      PARAMS ${metadata_tester_params}
                      TARGETS ${MDT_TARGETS})
_add_halide_libraries(metadata_tester_ucon
                      FROM metadata_tester.generator
                      GENERATOR_NAME metadata_tester
                      FEATURES user_context
                      PARAMS ${metadata_tester_params})
_add_halide_aot_tests(metadata_tester
                      HALIDE_LIBRARIES metadata_tester metadata_tester_ucon)

# msan_aottest.cpp
# msan_generator.cpp
_add_halide_libraries(msan
                      FEATURES msan
                      # Broken for C++ backend (https://github.com/halide/Halide/issues/7273)
                      OMIT_C_BACKEND)
_add_halide_aot_tests(msan
                      OMIT_C_BACKEND
                      GROUPS multithreaded)

# (Doesn't build/link properly on windows / under wasm)
if (NOT Halide_TARGET MATCHES "windows" AND NOT CMAKE_SYSTEM_NAME MATCHES "Windows" AND NOT ${_USING_WASM})
    # sanitizercoverage_aottest.cpp
    # sanitizercoverage_generator.cpp
    _add_halide_libraries(sanitizercoverage
                          # sanitizercoverage relies on LLVM-specific hooks, so it will never work with the C backend
                          OMIT_C_BACKEND
                          FEATURES sanitizer_coverage)
    _add_halide_aot_tests(sanitizercoverage OMIT_C_BACKEND)
endif ()

# multitarget_aottest.cpp
# multitarget_generator.cpp
_add_halide_libraries(multitarget
                       # Multitarget doesn't apply to WASM...
                       ENABLE_IF NOT ${_USING_WASM}
                       # ... or to the C backend
                       OMIT_C_BACKEND
                       TARGETS cmake-no_bounds_query cmake
                       FEATURES c_plus_plus_name_mangling
                       FUNCTION_NAME HalideTest::multitarget)
_add_halide_aot_tests(multitarget
                      ENABLE_IF NOT ${_USING_WASM}
                      OMIT_C_BACKEND)

# nested_externs_aottest.cpp
# nested_externs_generator.cpp
add_halide_generator(nested_externs.generator SOURCES nested_externs_generator.cpp)
set(NESTED_EXTERNS_LIBS nested_externs_root nested_externs_inner nested_externs_combine nested_externs_leaf)
foreach (LIB IN LISTS NESTED_EXTERNS_LIBS)
    _add_halide_libraries(${LIB} FROM nested_externs.generator GENERATOR_NAME ${LIB})
endforeach ()
_add_halide_aot_tests(nested_externs
                      HALIDE_LIBRARIES ${NESTED_EXTERNS_LIBS})

# opencl_runtime_aottest.cpp
# opencl_runtime_generator.cpp
_add_halide_libraries(opencl_runtime)
_add_halide_aot_tests(opencl_runtime)

# output_assign_aottest.cpp
# output_assign_generator.cpp
_add_halide_libraries(output_assign)
_add_halide_aot_tests(output_assign)

# pyramid_aottest.cpp
# pyramid_generator.cpp
_add_halide_libraries(pyramid PARAMS levels=10 )
_add_halide_aot_tests(pyramid GROUPS multithreaded)

# rdom_input_aottest.cpp
# rdom_input_generator.cpp
_add_halide_libraries(rdom_input)
_add_halide_aot_tests(rdom_input)

# string_param_aottest.cpp
# string_param_generator.cpp
_add_halide_libraries(string_param PARAMS "rpn_expr=5 y * x +")
_add_halide_aot_tests(string_param)

# stubtest_aottest.cpp
# stubtest_generator.cpp
# stubtest_jittest.cpp
# TODO: stubs not supported in CMake

# stubuser_aottest.cpp
# stubuser_generator.cpp
# TODO: stubs not supported in CMake

# shuffler_aottest.cpp
# shuffler_generator.cpp
_add_halide_libraries(shuffler)
_add_halide_aot_tests(shuffler)

# templated_aottest.cpp
# templated_generator.cpp
_add_halide_libraries(templated)
_add_halide_aot_tests(templated)

# tiled_blur_aottest.cpp
# tiled_blur_generator.cpp
_add_halide_libraries(tiled_blur)
_add_halide_aot_tests(tiled_blur
                      HALIDE_LIBRARIES tiled_blur blur2x2)

# user_context_aottest.cpp
# user_context_generator.cpp
_add_halide_libraries(user_context FEATURES user_context)
_add_halide_aot_tests(user_context GROUPS multithreaded)

# user_context_insanity_aottest.cpp
# user_context_insanity_generator.cpp
_add_halide_libraries(user_context_insanity FEATURES user_context)
_add_halide_aot_tests(user_context_insanity GROUPS multithreaded)

# variable_num_threads_aottest.cpp
# variable_num_threads_generator.cpp
_add_halide_libraries(variable_num_threads
                      # Requires threading support, not yet available for wasm tests
                      ENABLE_IF NOT ${_USING_WASM})
_add_halide_aot_tests(variable_num_threads
                      # Requires threading support, not yet available for wasm tests
                      ENABLE_IF NOT ${_USING_WASM}
                      GROUPS multithreaded)
