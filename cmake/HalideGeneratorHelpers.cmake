cmake_minimum_required(VERSION 3.28)

option(Halide_NO_DEFAULT_FLAGS "When enabled, suppresses recommended flags in add_halide_generator" OFF)

include(${CMAKE_CURRENT_LIST_DIR}/HalideTargetHelpers.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/TargetExportScript.cmake)

define_property(TARGET PROPERTY Halide_RT_TARGETS
                BRIEF_DOCS "On a Halide runtime target, lists the targets the runtime backs"
                FULL_DOCS "On a Halide runtime target, lists the targets the runtime backs")

define_property(TARGET PROPERTY Halide_GENERATOR_HAS_POST_BUILD
                BRIEF_DOCS "On a Halide generator target, true if Halide.dll copy command has already been added."
                FULL_DOCS "On a Halide generator target, true if Halide.dll copy command has already been added.")

define_property(TARGET PROPERTY Halide_PYTHON_GENERATOR_SOURCE
                BRIEF_DOCS "Used to store the source file(s) for a Python Generator"
                FULL_DOCS "Used to store the source file(s) for a Python Generator")

define_property(TARGET PROPERTY Halide_LIBRARY_RUNTIME_TARGET
                BRIEF_DOCS "On a Halide library target, the runtime it uses."
                FULL_DOCS "On a Halide library target, the runtime it uses.")

define_property(TARGET PROPERTY Halide_LIBRARY_PYTHON_EXTENSION_CPP
                BRIEF_DOCS "On a Halide library target, the .py.cpp generated for it (absent if none)."
                FULL_DOCS "On a Halide library target, the .py.cpp generated for it (absent if none).")

define_property(TARGET PROPERTY Halide_LIBRARY_FUNCTION_NAME
                BRIEF_DOCS "On a Halide library target, the FUNCTION_NAME used."
                FULL_DOCS "On a Halide library target, the FUNCTION_NAME used.")

##
# Function to simplify writing the CMake rules for creating a generator executable
# that follows our recommended cross-compiling workflow.
##

function(add_halide_generator TARGET)
    set(options "")
    set(oneValueArgs PACKAGE_NAME PACKAGE_NAMESPACE EXPORT_FILE PYSTUB)
    set(multiValueArgs SOURCES LINK_LIBRARIES)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT ARG_PACKAGE_NAME)
        set(ARG_PACKAGE_NAME "${PROJECT_NAME}-halide_generators")
    endif ()

    if (NOT ARG_PACKAGE_NAMESPACE)
        set(ARG_PACKAGE_NAMESPACE "${PROJECT_NAME}::halide_generators::")
    endif ()

    if (NOT ARG_EXPORT_FILE)
        file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/cmake")
        set(ARG_EXPORT_FILE "${PROJECT_BINARY_DIR}/cmake/${ARG_PACKAGE_NAME}Config.cmake")
    endif ()

    if (NOT ARG_SOURCES)
        set(ARG_SOURCES "${ARG_UNPARSED_ARGUMENTS}")
    endif ()

    _Halide_try_load_generators("${ARG_PACKAGE_NAME}")

    # Communicate found information to the caller
    set(${ARG_PACKAGE_NAME}_FOUND "${${ARG_PACKAGE_NAME}_FOUND}" PARENT_SCOPE)

    set(gen "${ARG_PACKAGE_NAMESPACE}${TARGET}")
    if (NOT TARGET "${gen}")
        if (NOT TARGET "${ARG_PACKAGE_NAME}")
            add_custom_target("${ARG_PACKAGE_NAME}")
        endif ()

        if (NOT Halide_FOUND)
            find_package(Halide REQUIRED)
        endif ()

        if (ARG_SOURCES MATCHES ".py$")
            if (ARG_LINK_LIBRARIES)
                message(FATAL_ERROR "You cannot specify LINK_LIBRARIES in conjunction with Python source code.")
            endif ()
            if (ARG_PYSTUB)
                message(FATAL_ERROR "You cannot specify PYSTUB in conjunction with Python source code (only C++ Generators can produce PyStubs).")
            endif ()

            list(LENGTH ARG_SOURCES len)
            if (NOT len EQUAL 1)
                message(FATAL_ERROR "Python Generators must specify exactly one source file.")
            endif ()

            # Make a fake target here that we can attach the Python source to,
            # so that we can extract 'em in add_halide_library()
            add_custom_target(${TARGET} ALL)
            set_property(TARGET ${TARGET} PROPERTY Halide_PYTHON_GENERATOR_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/${ARG_SOURCES}")

            # TODO: what do we need to do for PACKAGE_NAME PACKAGE_NAMESPACE EXPORT_FILE in this case?
        else ()
            add_executable(${TARGET} ${ARG_SOURCES})
            add_executable(${gen} ALIAS ${TARGET})
            target_link_libraries(${TARGET} PRIVATE Halide::Generator ${ARG_LINK_LIBRARIES})

            _Halide_place_dll(${TARGET})

            if (NOT ARG_NO_DEFAULT_FLAGS AND NOT Halide_NO_DEFAULT_FLAGS)
                # For crosscompiling builds, the Halide headers will be included using -isystem,
                # which will cause all warnings to be ignored. This is not helpful, since
                # we *want* deprecation warnings to be propagated. So we must set
                # NO_SYSTEM_FROM_IMPORTED in order for it to be seen.
                set_target_properties(${TARGET} PROPERTIES NO_SYSTEM_FROM_IMPORTED YES)
                target_compile_options(
                    ${TARGET} PRIVATE
                    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wdeprecated-declarations>
                    $<$<CXX_COMPILER_ID:MSVC>:/w14996>  # 4996: compiler encountered deprecated declaration
                )
            endif ()

            add_dependencies("${ARG_PACKAGE_NAME}" ${TARGET})
            export(TARGETS ${TARGET}
                   NAMESPACE ${ARG_PACKAGE_NAMESPACE}
                   APPEND FILE "${ARG_EXPORT_FILE}")
        endif ()
    endif ()

    if (ARG_PYSTUB)
        set(GEN_NAME ${ARG_PYSTUB})
        set(MODULE_NAME ${ARG_PYSTUB}_pystub)
        # Generate a small C++ file that includes the boilerplate code needed to
        # register a PyInit function that has the stub glue code.
        string(CONCAT stub_text
               "#include <Python.h>\n"
               "#include \"Halide.h\"\n"
               "HALIDE_GENERATOR_PYSTUB(${GEN_NAME}, ${MODULE_NAME})\n")

        set(stub_file "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.${GEN_NAME}.${MODULE_NAME}.py_stub_generated.cpp")
        if (NOT EXISTS "${stub_file}")
            file(WRITE "${stub_file}" "${stub_text}")
        endif ()

        Python3_add_library(${TARGET}_pystub MODULE WITH_SOABI "${stub_file}" ${ARG_SOURCES})
        set_target_properties(${TARGET}_pystub PROPERTIES
                              CXX_VISIBILITY_PRESET hidden
                              VISIBILITY_INLINES_HIDDEN ON
                              POSITION_INDEPENDENT_CODE ON)
        target_link_libraries(${TARGET}_pystub PRIVATE Halide::PyStubs Halide::Halide ${ARG_LINK_LIBRARIES})
        set_target_properties(${TARGET}_pystub PROPERTIES OUTPUT_NAME ${MODULE_NAME})
        _Halide_target_export_single_symbol(${TARGET}_pystub "PyInit_${MODULE_NAME}")
    endif ()
endfunction()

function(_Halide_try_load_generators package_name)
    # Don't repeatedly run the search for the tools package.
    if (NOT DEFINED ${package_name}_FOUND)
        # Some toolchains, like Emscripten, try to disable finding packages
        # outside their sysroots, but we always want to find the native
        # generators. Setting CMAKE_FIND_ROOT_PATH_BOTH here overrides
        # the toolchain search preference. This is okay since a user can
        # always override this call by setting ${package_name}_ROOT.
        find_package(${package_name} QUIET CMAKE_FIND_ROOT_PATH_BOTH)

        # Communicate found information to the caller
        set(${package_name}_FOUND "${${package_name}_FOUND}" PARENT_SCOPE)

        if (NOT ${package_name}_FOUND AND CMAKE_CROSSCOMPILING AND NOT CMAKE_CROSSCOMPILING_EMULATOR)
            message(WARNING
                    "'${package_name}' was not found and it looks like you "
                    "are cross-compiling without an emulator. This is likely to "
                    "fail. Please set -D${package_name}_ROOT=... at the CMake "
                    "command line to the build directory of a host-built ${PROJECT_NAME}.")
        endif ()
    endif ()
endfunction()

function(_Halide_invoke_generator)
    cmake_parse_arguments(
        PARSE_ARGV 0 ARG ""
        "BASE_NAME;FUNCTION_NAME;GENERATOR;GRADIENT_DESCENT;TYPE"
        "COMMAND;DEPENDS;EXTRA_OUTPUTS;PARAMS;PLUGINS;TARGETS"
    )

    ## "hash table" of extra outputs to extensions
    set(assembly_extension ".s")
    set(bitcode_extension ".bc")
    set(compiler_log_extension ".halide_compiler_log")
    set(featurization_extension ".featurization")
    set(function_info_header_extension ".function_info.h")
    set(llvm_assembly_extension ".ll")
    set(python_extension_extension ".py.cpp")
    set(pytorch_wrapper_extension ".pytorch.h")
    set(registration_extension ".registration.cpp")
    set(schedule_extension ".schedule.h")
    set(stmt_extension ".stmt")
    set(stmt_html_extension ".stmt.html")

    ## Validate plugins
    foreach (plugin IN LISTS ARG_PLUGINS)
        if (NOT TARGET "${plugin}")
            message(FATAL_ERROR "Plugin `${plugin}` is not a target.")
        endif ()
    endforeach ()

    ## Always omit the runtime
    list(TRANSFORM ARG_TARGETS APPEND "-no_runtime")

    ## Resolve plugins
    if (ARG_PLUGINS)
        list(TRANSFORM ARG_PLUGINS REPLACE "(.+)" "$<TARGET_FILE:\\1>" OUTPUT_VARIABLE plugins_args)
        list(JOIN plugins_args "," plugins_args)
        list(PREPEND plugins_args -p)
    else ()
        set(plugins_args "")
    endif ()

    ## Gather platform information
    _Halide_get_platform_details(
        UNUSED
        object_suffix
        static_library_suffix
        "${ARG_TARGETS}"
    )

    ## Check the type to determine outputs
    set(outputs c_header)
    set(output_files "${ARG_BASE_NAME}.h")

    if (ARG_TYPE STREQUAL "c_source")
        list(APPEND outputs "${ARG_TYPE}")
        list(APPEND output_files "${ARG_BASE_NAME}.halide_generated.cpp")
    elseif (ARG_TYPE STREQUAL "object")
        list(LENGTH ARG_TARGETS len)
        if (len EQUAL 1)
            list(APPEND outputs "${ARG_TYPE}")
            list(APPEND output_files "${ARG_BASE_NAME}${object_suffix}")
        else ()
            foreach (t IN LISTS ARG_TARGETS)
                list(APPEND outputs "${ARG_TYPE}")
                list(APPEND output_files "${ARG_BASE_NAME}-${t}${object_suffix}")
            endforeach ()
            list(APPEND outputs "${ARG_TYPE}")
            list(APPEND output_files "${TARGET}_wrapper${object_suffix}")
        endif ()
    elseif (ARG_TYPE STREQUAL "static_library")
        list(APPEND outputs "${ARG_TYPE}")
        list(APPEND output_files "${ARG_BASE_NAME}${static_library_suffix}")
    else ()
        message(FATAL_ERROR "`${ARG_TYPE}` not one of: c_source, object, static_library")
    endif ()

    foreach (output IN LISTS ARG_EXTRA_OUTPUTS)
        list(APPEND outputs "${output}")
        list(APPEND output_files "${ARG_BASE_NAME}${${output}_extension}")
    endforeach ()

    ## Run the generator
    add_custom_command(
        OUTPUT ${output_files}
        COMMAND ${ARG_COMMAND}
        -n "${ARG_BASE_NAME}"
        -d "$<BOOL:${ARG_GRADIENT_DESCENT}>"
        -g "${ARG_GENERATOR}"
        -f "${ARG_FUNCTION_NAME}"
        -e "$<LOWER_CASE:$<JOIN:$<REMOVE_DUPLICATES:${outputs}>,$<COMMA>>>"
        ${plugins_args}
        -o .
        "target=$<JOIN:${ARG_TARGETS},$<COMMA>>"
        ${ARG_PARAMS}
        DEPENDS ${ARG_DEPENDS} ${ARG_PLUGINS}
        VERBATIM
    )

    ## Populate output variables
    list(TRANSFORM output_files PREPEND "${CMAKE_CURRENT_BINARY_DIR}/")
    set(OUT_FILES ${output_files} PARENT_SCOPE)

    foreach (out IN LISTS outputs)
        set("OUT_${out}" "")
    endforeach ()

    foreach (out file IN ZIP_LISTS outputs output_files)
        list(APPEND "OUT_${out}" "${file}")
    endforeach ()

    foreach (out IN LISTS outputs)
        set("OUT_${out}" "${OUT_${out}}" PARENT_SCOPE)
    endforeach ()
endfunction()

function(_Halide_library_from_generator TARGET)
    cmake_parse_arguments(
        PARSE_ARGV 1 ARG ""
        "FUNCTION_NAME;GENERATOR;GRADIENT_DESCENT;TYPE;USE_RUNTIME"
        "COMMAND;DEPENDS;EXTRA_OUTPUTS;PARAMS;PLUGINS;TARGETS"
    )

    # Invoke the generator to create the library sources
    # Sets OUT_FILES, OUT_c_header, OUT_<extra-output>, etc.
    _Halide_invoke_generator(
        BASE_NAME "${TARGET}"
        COMMAND ${ARG_COMMAND}
        DEPENDS ${ARG_DEPENDS}
        EXTRA_OUTPUTS ${ARG_EXTRA_OUTPUTS}
        FUNCTION_NAME "${ARG_FUNCTION_NAME}"
        GENERATOR "${ARG_GENERATOR}"
        GRADIENT_DESCENT "${ARG_GRADIENT_DESCENT}"
        PARAMS ${ARG_PARAMS}
        PLUGINS ${ARG_PLUGINS}
        TARGETS ${ARG_TARGETS}
        TYPE ${ARG_TYPE}
    )

    # Create the filter's library target
    if (ARG_TYPE STREQUAL "static_library")
        add_library("${TARGET}" STATIC IMPORTED GLOBAL)
        set_target_properties("${TARGET}" PROPERTIES IMPORTED_LOCATION "${OUT_${ARG_TYPE}}")
    else ()
        add_library("${TARGET}" STATIC ${OUT_${ARG_TYPE}})
        set_property(TARGET "${TARGET}" PROPERTY POSITION_INDEPENDENT_CODE ON)
        set_property(TARGET "${TARGET}" PROPERTY LINKER_LANGUAGE CXX)

        if (NOT Halide_NO_DEFAULT_FLAGS)
            # Silence many useless warnings in generated C++ code compilation
            target_compile_options(
                "${TARGET}" PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-psabi>
            )
        endif ()
        _Halide_fix_xcode("${TARGET}")
    endif ()

    add_custom_target("${TARGET}.update" DEPENDS ${OUT_FILES})
    add_dependencies("${TARGET}" "${TARGET}.update")

    target_link_libraries("${TARGET}" INTERFACE "${ARG_USE_RUNTIME}")
    add_dependencies("${TARGET}" "${ARG_USE_RUNTIME}")

    if (NOT ARG_TYPE STREQUAL "c_source")
        _Halide_add_targets_to_runtime("${ARG_USE_RUNTIME}" TARGETS ${ARG_TARGETS})
    endif ()

    target_sources("${TARGET}" INTERFACE
                   FILE_SET HEADERS
                   BASE_DIRS "${CMAKE_CURRENT_BINARY_DIR}"
                   FILES "${OUT_c_header}")

    # Propagate output variables
    foreach (output IN LISTS ARG_EXTRA_OUTPUTS ITEMS c_header)
        set("OUT_${output}" "${OUT_${output}}" PARENT_SCOPE)
    endforeach ()
endfunction()

function(_Halide_lipo TARGET)
    set(merged_libs ${ARGN})
    list(TRANSFORM merged_libs REPLACE "^(.+)$" "$<TARGET_FILE:\\1>"
         OUTPUT_VARIABLE merged_libs_files)
    list(TRANSFORM merged_libs REPLACE "^(.+)$" "$<COMPILE_ONLY:\\1>"
         OUTPUT_VARIABLE merged_libs_targets)

    find_program(LIPO lipo REQUIRED)
    add_custom_command(
        TARGET "${TARGET}" POST_BUILD
        COMMAND "${LIPO}" -create ${merged_libs_files} "$<TARGET_FILE:${TARGET}>" -output "$<TARGET_FILE:${TARGET}>"
        DEPENDS ${merged_libs}
        VERBATIM
    )

    target_link_libraries("${TARGET}" INTERFACE ${merged_libs_targets})
endfunction()

##
# Function to simplify writing the CMake rules for invoking a generator executable
# and getting a usable CMake library out of it.
##

function(add_halide_library TARGET)
    ##
    # Set up argument parsing for extra outputs.
    ##

    # See Module.cpp for list of extra outputs. The following outputs intentionally do not appear:
    # - `c_header` is always generated
    # - `c_source` is selected by C_BACKEND
    # - `object` is selected for CMake-target-compile
    # - `static_library` is selected for cross-compile
    # - `cpp_stub` is not available
    set(extra_output_names
        ASSEMBLY
        BITCODE
        COMPILER_LOG
        FEATURIZATION
        FUNCTION_INFO_HEADER
        LLVM_ASSEMBLY
        PYTHON_EXTENSION
        PYTORCH_WRAPPER
        REGISTRATION
        SCHEDULE
        STMT
        STMT_HTML)

    ##
    # Parse the arguments and set defaults for missing values.
    ##

    set(features_args FEATURES)
    foreach (arch IN ITEMS x86 arm powerpc hexagon wasm riscv)
        foreach (bits IN ITEMS 32 64)
            foreach (os IN ITEMS linux windows osx android ios qurt noos fuchsia wasmrt)
                list(APPEND features_args "FEATURES[${arch}-${bits}-${os}]")
            endforeach ()
        endforeach ()
    endforeach ()

    set(options C_BACKEND GRADIENT_DESCENT)
    set(oneValueArgs FROM GENERATOR FUNCTION_NAME NAMESPACE USE_RUNTIME AUTOSCHEDULER HEADER ${extra_output_names} NO_THREADS NO_DL_LIBS)
    set(multiValueArgs TARGETS PARAMS PLUGINS ${features_args})
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT "${ARG_UNPARSED_ARGUMENTS}" STREQUAL "")
        message(AUTHOR_WARNING "Arguments to add_halide_library were not recognized: ${ARG_UNPARSED_ARGUMENTS}")
    endif ()

    if (NOT ARG_FROM)
        message(FATAL_ERROR "Missing FROM argument specifying a Halide generator target")
    endif ()

    if (ARG_C_BACKEND AND ARG_TARGETS)
        message(AUTHOR_WARNING "The C backend sources will be compiled with the current CMake toolchain.")
    endif ()

    if (NOT TARGET ${ARG_FROM})
        # FROM is usually an unqualified name; if we are crosscompiling, we might need a
        # fully-qualified name, so add the default package name and retry
        set(FQ_ARG_FROM "${PROJECT_NAME}::halide_generators::${ARG_FROM}")
        if (NOT TARGET ${FQ_ARG_FROM})
            message(FATAL_ERROR "Unable to locate FROM as either ${ARG_FROM} or ${FQ_ARG_FROM}")
        endif ()
        set(ARG_FROM "${FQ_ARG_FROM}")
    endif ()

    get_property(py_src TARGET ${ARG_FROM} PROPERTY Halide_PYTHON_GENERATOR_SOURCE)
    if (py_src)
        # TODO: Python Generators need work to support crosscompiling (https://github.com/halide/Halide/issues/7014)
        if (NOT TARGET Halide::Python)
            message(FATAL_ERROR "This version of Halide was built without support for Python bindings; rebuild using WITH_PYTHON_BINDINGS=ON to use this rule with Python Generators.")
        endif ()

        if (NOT TARGET Python3::Interpreter)
            message(FATAL_ERROR "You must call find_package(Python3) in your CMake code in order to use this rule with Python Generators.")
        endif ()

        set(GENERATOR_CMD
            ${CMAKE_COMMAND} -E env "PYTHONPATH=$<PATH:GET_PARENT_PATH,$<TARGET_FILE_DIR:Halide::Python>>" --
            ${Halide_PYTHON_LAUNCHER} "$<TARGET_FILE:Python3::Interpreter>" $<SHELL_PATH:${py_src}>
        )
        set(GENERATOR_CMD_DEPS ${ARG_FROM} Halide::Python ${py_src})
    else ()
        set(GENERATOR_CMD "${ARG_FROM}")
        set(GENERATOR_CMD_DEPS ${ARG_FROM})
    endif ()

    if (NOT ARG_GENERATOR)
        set(ARG_GENERATOR "${TARGET}")
    endif ()

    if (NOT ARG_FUNCTION_NAME)
        set(ARG_FUNCTION_NAME "${TARGET}")
    endif ()

    if (ARG_NAMESPACE)
        set(ARG_FUNCTION_NAME "${ARG_NAMESPACE}::${ARG_FUNCTION_NAME}")
    endif ()

    # If no TARGETS argument, use Halide_TARGET instead
    if (NOT ARG_TARGETS)
        set(ARG_TARGETS "${Halide_TARGET}")
    endif ()

    # If still no TARGET, try to use host, but if that would
    # cross-compile, then default to 'cmake' and warn.
    if (NOT ARG_TARGETS)
        if (Halide_HOST_TARGET STREQUAL Halide_CMAKE_TARGET)
            set(ARG_TARGETS host)
        else ()
            message(AUTHOR_WARNING
                    "Targets must be manually specified to add_halide_library when cross-compiling. "
                    "The default 'host' target ${Halide_HOST_TARGET} differs from the active CMake "
                    "target ${Halide_CMAKE_TARGET}. Using ${Halide_CMAKE_TARGET} to compile ${TARGET}. "
                    "This might result in performance degradation from missing arch flags (eg. avx).")
            set(ARG_TARGETS cmake)
        endif ()
    endif ()

    _Halide_validate_multitarget(common_triple ${ARG_TARGETS})

    _Halide_get_platform_details(
        is_crosscompiling
        object_suffix
        static_library_suffix
        "${common_triple}"
    )

    ##
    # Set up the runtime library, if needed
    ##

    if (ARG_C_BACKEND)
        # The C backend does not provide a runtime, so just supply headers.
        if (ARG_USE_RUNTIME)
            message(AUTHOR_WARNING "The C backend does not use a runtime.")
        endif ()
        set(ARG_USE_RUNTIME Halide::Runtime)
    elseif (NOT ARG_USE_RUNTIME)
        # If we're not using an existing runtime, create one.
        set(runtime_args "")
        if (DEFINED ARG_NO_THREADS)
            list(APPEND runtime_args NO_THREADS "${ARG_NO_THREADS}")
        endif ()
        if (DEFINED ARG_NO_DL_LIBS)
            list(APPEND runtime_args NO_DL_LIBS "${ARG_NO_DL_LIBS}")
        endif ()

        add_halide_runtime(
            "${TARGET}.runtime" FROM ${ARG_FROM}
            NO_DEFAULT_TARGETS TARGETS ${ARG_TARGETS}
            ${runtime_args}
        )

        set(ARG_USE_RUNTIME "${TARGET}.runtime")
    elseif (NOT TARGET ${ARG_USE_RUNTIME})
        message(FATAL_ERROR "Invalid runtime target ${ARG_USE_RUNTIME}")
    else ()
        _Halide_add_targets_to_runtime(${ARG_USE_RUNTIME} TARGETS ${ARG_TARGETS})
    endif ()

    ##
    # Determine which outputs the generator call will emit.
    ##

    if (ARG_C_BACKEND)
        set(library_type c_source)
    elseif (is_crosscompiling)
        set(library_type static_library)
    else ()
        set(library_type object)
    endif ()

    # Add in extra outputs using the table defined at the start of this function
    set(extra_outputs "")
    foreach (out IN LISTS extra_output_names)
        if (ARG_${out})
            string(TOLOWER "${out}" out)
            list(APPEND extra_outputs ${out})
        endif ()
    endforeach ()

    ##
    # Attach an autoscheduler if the user requested it
    ##

    if (ARG_AUTOSCHEDULER)
        if ("${ARG_AUTOSCHEDULER}" MATCHES "::")
            if (NOT TARGET "${ARG_AUTOSCHEDULER}")
                message(FATAL_ERROR "Autoscheduler ${ARG_AUTOSCHEDULER} does not exist.")
            endif ()

            # Convention: if the argument names a target like "Namespace::Scheduler" then
            # it is assumed to be a MODULE target providing a scheduler named "Scheduler".
            list(APPEND ARG_PLUGINS "${ARG_AUTOSCHEDULER}")
            string(REGEX REPLACE ".*::(.*)" "\\1" ARG_AUTOSCHEDULER "${ARG_AUTOSCHEDULER}")
        elseif (NOT ARG_PLUGINS)
            message(AUTHOR_WARNING "AUTOSCHEDULER set to a scheduler name but no plugins were loaded")
        endif ()
        list(PREPEND ARG_PARAMS "autoscheduler=${ARG_AUTOSCHEDULER}")
    endif ()

    ##
    # Main library target for filter.
    ##

    set(generator_args
        COMMAND ${GENERATOR_CMD}
        DEPENDS ${GENERATOR_CMD_DEPS}
        EXTRA_OUTPUTS ${extra_outputs}
        FUNCTION_NAME "${ARG_FUNCTION_NAME}"
        GENERATOR "${ARG_GENERATOR}"
        GRADIENT_DESCENT "${ARG_GRADIENT_DESCENT}"
        PARAMS ${ARG_PARAMS}
        PLUGINS ${ARG_PLUGINS}
        TYPE "${library_type}"
        USE_RUNTIME "${ARG_USE_RUNTIME}"
    )

    list(JOIN ARG_FEATURES "-" ARG_FEATURES)

    list(LENGTH Halide_CMAKE_TARGET num_targets)
    if (common_triple STREQUAL "cmake" AND num_targets GREATER 1)
        if (ARG_C_BACKEND)
            message(FATAL_ERROR "The C backend is not available in multi-platform builds.")
        endif ()

        set(merged_base "")
        set(merged_libs "")

        foreach (triple IN LISTS Halide_CMAKE_TARGET)
            set(features_arch "ARG_FEATURES[${triple}]")
            set(features_arch "${${features_arch}}")
            if (features_arch)
                list(TRANSFORM features_arch PREPEND "${triple}-"
                     OUTPUT_VARIABLE targets_arch)
            else ()
                set(targets_arch "${triple}")
            endif ()

            list(TRANSFORM targets_arch APPEND "-${ARG_FEATURES}")
            list(TRANSFORM targets_arch REPLACE "-$" "")

            if (NOT merged_base)
                set(this_lib "${TARGET}")
                set(merged_base "${this_lib}")
            else ()
                set(this_lib "${TARGET}-${triple}")
                list(APPEND merged_libs "${this_lib}")
            endif ()

            # TODO: accumulate OUT_s. Overwrites OUT_ from the previous call.
            _Halide_library_from_generator("${this_lib}" ${generator_args}
                                           TARGETS ${targets_arch})
        endforeach ()

        _Halide_lipo("${merged_base}" ${merged_libs})
    else ()
        list(TRANSFORM ARG_TARGETS REPLACE "cmake" "${Halide_CMAKE_TARGET}")
        if (ARG_FEATURES)
            list(TRANSFORM ARG_TARGETS APPEND "-${ARG_FEATURES}")
        endif ()
        # Sets OUT_FILES, OUT_c_header, OUT_<extra-output>, etc.
        _Halide_library_from_generator(
            "${TARGET}" ${generator_args} TARGETS ${ARG_TARGETS})
    endif ()

    # Save some info for add_halide_python_extension_library() in case it is used for this target.
    set_property(TARGET "${TARGET}" PROPERTY Halide_LIBRARY_RUNTIME_TARGET "${ARG_USE_RUNTIME}")
    set_property(TARGET "${TARGET}" PROPERTY Halide_LIBRARY_FUNCTION_NAME "${ARG_FUNCTION_NAME}")
    if ("python_extension" IN_LIST extra_outputs)
        set_property(TARGET "${TARGET}" PROPERTY Halide_LIBRARY_PYTHON_EXTENSION_CPP "${OUT_python_extension}")
    endif ()

    # Propagate outputs
    if (ARG_HEADER)
        set(${ARG_HEADER} "${OUT_c_header}")
    endif ()

    foreach (output IN LISTS extra_outputs)
        string(TOUPPER "ARG_${output}" outvar_arg)
        if (${outvar_arg})
            set("${${outvar_arg}}" ${OUT_${output}} PARENT_SCOPE)
        endif ()
    endforeach ()
endfunction()

function(_Halide_validate_multitarget OUT_TRIPLE)
    list(LENGTH ARGN len)
    if (len LESS 1)
        message(FATAL_ERROR "Must supply at least one target")
    endif ()

    set(triple "")
    set(all_features "")
    foreach (target IN LISTS ARGN)
        if (target MATCHES "^(host|cmake|[^-]+-[^-]+-[^-]+)(-[^-]+)*$")
            set(this_triple "${CMAKE_MATCH_1}")
            list(APPEND all_features ${CMAKE_MATCH_2})
            if (NOT triple)
                set(triple "${this_triple}")
            elseif (NOT this_triple STREQUAL triple)
                message(FATAL_ERROR "Multi-target entry `${target}` does not match earlier triple `${triple}`")
            endif ()
        else ()
            message(FATAL_ERROR "TARGET `${target}` is malformed")
        endif ()
    endforeach ()

    list(LENGTH Halide_CMAKE_TARGET len)
    if (len GREATER 1 AND NOT all_features STREQUAL "")
        message(
            FATAL_ERROR
            "Multiarch builds cannot include features in the target list. Use FEATURES[arch] instead."
            "Halide_CMAKE_TARGET=${Halide_CMAKE_TARGET} and saw TARGETS ${ARGN}."
        )
    endif ()

    set(${OUT_TRIPLE} "${triple}" PARENT_SCOPE)
endfunction()

function(add_halide_python_extension_library TARGET)
    set(options "")
    set(oneValueArgs MODULE_NAME)
    set(multiValueArgs HALIDE_LIBRARIES)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT ARG_MODULE_NAME)
        set(ARG_MODULE_NAME "${TARGET}")
    endif ()

    if (NOT ARG_HALIDE_LIBRARIES)
        message(FATAL_ERROR "HALIDE_LIBRARIES must be specified")
    endif ()

    set(runtimes "")
    set(pycpps "")
    set(function_names "")  # space-separated X-macros
    foreach (lib IN LISTS ARG_HALIDE_LIBRARIES)
        if (NOT TARGET "${lib}")
            message(FATAL_ERROR "${lib} is not a valid target")
        endif ()

        get_property(runtime_used TARGET ${lib} PROPERTY Halide_LIBRARY_RUNTIME_TARGET)
        if (NOT runtime_used)
            message(FATAL_ERROR "${lib} does not appear to have a Halide Runtime specified")
        endif ()
        list(APPEND runtimes ${runtime_used})

        get_property(function_name TARGET ${lib} PROPERTY Halide_LIBRARY_FUNCTION_NAME)
        if (NOT function_name)
            message(FATAL_ERROR "${lib} does not appear to have a Function name specified")
        endif ()
        # Strip C++ namespace(s), if any
        string(REGEX REPLACE ".*::(.*)" "\\1" function_name "${function_name}")
        string(APPEND function_names " X(${function_name})")

        get_property(pycpp TARGET ${lib} PROPERTY Halide_LIBRARY_PYTHON_EXTENSION_CPP)
        if (NOT pycpp)
            message(FATAL_ERROR "${lib} must be built with PYTHON_EXTENSION specified in order to use it with add_halide_python_extension_library()")
        endif ()
        list(APPEND pycpps ${pycpp})
    endforeach ()

    list(REMOVE_DUPLICATES runtimes)
    list(LENGTH runtimes len)
    if (NOT len EQUAL 1)
        message(FATAL_ERROR "${TARGET} requires all libraries to use the same Halide Runtime, but saw ${len}: ${runtimes}")
    endif ()

    set(pyext_runtime_name ${TARGET}_module_definition)
    set(pyext_module_definition_src "${CMAKE_CURRENT_BINARY_DIR}/${pyext_runtime_name}.py.cpp")

    add_custom_command(OUTPUT ${pyext_module_definition_src}
                       COMMAND Halide::GenRT -r "${pyext_runtime_name}" -e python_extension -o "${CMAKE_CURRENT_BINARY_DIR}" target=host
                       DEPENDS Halide::GenRT
                       VERBATIM)

    Python3_add_library(${TARGET} MODULE WITH_SOABI ${pycpps} ${pyext_module_definition_src})
    target_link_libraries(${TARGET} PRIVATE ${ARG_HALIDE_LIBRARIES})
    target_compile_definitions(${TARGET} PRIVATE
                               # Skip the default module-definition code in each file
                               HALIDE_PYTHON_EXTENSION_OMIT_MODULE_DEFINITION
                               # Gotta explicitly specify the module name and function(s) for this mode
                               HALIDE_PYTHON_EXTENSION_MODULE_NAME=${ARG_MODULE_NAME}
                               "HALIDE_PYTHON_EXTENSION_FUNCTIONS=${function_names}")
    set_target_properties(${TARGET} PROPERTIES OUTPUT_NAME ${ARG_MODULE_NAME})
    _Halide_target_export_single_symbol(${TARGET} "PyInit_${ARG_MODULE_NAME}")
endfunction()

##
# Function for ensuring that Halide.dll is visible to the generator
##

function(_Halide_place_dll GEN)
    if (NOT WIN32)
        return()
    endif ()

    # Short circuit so that Halide::Halide isn't checked when importing a generator from another CMake project
    get_property(is_imported TARGET ${GEN} PROPERTY IMPORTED)
    if (is_imported)
        return()
    endif ()

    get_property(has_post_build TARGET ${GEN} PROPERTY Halide_GENERATOR_HAS_POST_BUILD)
    if (has_post_build)
        return()
    endif ()

    # Here GEN is not IMPORTED, which means that it must be linked
    # to Halide::Halide and therefore Halide::Halide must exist.
    get_property(halide_type TARGET Halide::Halide PROPERTY TYPE)
    if (NOT halide_type STREQUAL "SHARED_LIBRARY")
        return()
    endif ()

    add_custom_command(
        TARGET ${GEN} POST_BUILD
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/MutexCopy.ps1"
        -src "$<TARGET_FILE:Halide::Halide>"
        -dstDir "$<TARGET_FILE_DIR:${GEN}>"
        VERBATIM
    )
    set_property(TARGET ${GEN} PROPERTY Halide_GENERATOR_HAS_POST_BUILD 1)
endfunction()

##
# Function for creating a standalone runtime from a generator.
##

function(add_halide_runtime RT)
    set(options NO_DEFAULT_TARGETS)
    set(oneValueArgs FROM NO_THREADS NO_DL_LIBS)
    set(multiValueArgs TARGETS)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    # If no TARGETS argument, use Halide_TARGET instead
    if (NOT ARG_TARGETS)
        set(ARG_TARGETS "${Halide_TARGET}")
    endif ()

    # Try to use an available generator which is available. This is
    # essential for cross-compilation where we cannot use host compiler
    # to build generator only for runtime.
    set(genrt Halide::GenRT)
    if (TARGET ${ARG_FROM})
        # Need to check if the ones for python extension, which is not actually
        # an executable
        get_target_property(target_type ${ARG_FROM} TYPE)
        get_target_property(aliased ${ARG_FROM} ALIASED_TARGET)
        if (target_type STREQUAL "EXECUTABLE" AND NOT aliased)
            set(genrt "${ARG_FROM}")
        endif ()
    endif ()

    # The default of NO_THREADS/NO_DL_LIBS is OFF unless Halide_RUNTIME_NO_THREADS/NO_DL_LIBS is defined globally
    if (NOT DEFINED ARG_NO_THREADS)
        set(ARG_NO_THREADS ${Halide_RUNTIME_NO_THREADS})
    endif ()
    if (NOT DEFINED ARG_NO_DL_LIBS)
        set(ARG_NO_DL_LIBS ${Halide_RUNTIME_NO_DL_LIBS})
    endif ()

    _Halide_validate_multitarget(common_triple ${ARG_TARGETS})

    _Halide_get_platform_details(
        is_crosscompiling
        object_suffix
        static_library_suffix
        "${common_triple}")

    # We defer reading the list of targets for which to generate a common
    # runtime to CMake _generation_ time. This prevents issues where a lower
    # GCD is required by a later Halide library linking to this runtime.
    set(target_list "$<TARGET_GENEX_EVAL:${RT},$<TARGET_PROPERTY:${RT},Halide_RT_TARGETS>>")

    # Remove features that should not be attached to a runtime
    # TODO: The fact that removing profile fixes a duplicate symbol linker error on Windows smells like a bug.
    set(target_list "$<LIST:TRANSFORM,${target_list},REPLACE,-(user_context|no_asserts|no_bounds_query|no_runtime|profile),>")

    if (is_crosscompiling)
        set(GEN_OUTS "${RT}${static_library_suffix}")
        add_custom_command(
            OUTPUT "${GEN_OUTS}"
            COMMAND "${genrt}" -r "${RT}" -o .
            "target=$<JOIN:$<REMOVE_DUPLICATES:${target_list}>,$<COMMA>>"
            DEPENDS "${genrt}"
            VERBATIM)
        add_custom_target("${RT}.update" DEPENDS "${GEN_OUTS}")

        add_library("${RT}" STATIC IMPORTED GLOBAL)
        add_dependencies("${RT}" "${RT}.update")

        set_target_properties("${RT}" PROPERTIES
                              IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/${GEN_OUTS})
    else ()
        list(LENGTH Halide_CMAKE_TARGET num_targets)
        if (common_triple STREQUAL "cmake" AND num_targets GREATER 1)
            set(base_rt "")
            set(arch_rt "")
            foreach (triple IN LISTS Halide_CMAKE_TARGET)
                set(arch_target_list "$<LIST:TRANSFORM,${target_list},REPLACE,cmake,${triple}>")
                set(arch_target_list "$<FILTER:${arch_target_list},INCLUDE,^${triple}>")

                if (NOT base_rt)
                    set(this_rt "${RT}")
                    set(base_rt "${this_rt}")
                else ()
                    set(this_rt "${RT}-${triple}")
                    list(APPEND arch_rt "${this_rt}")
                endif ()

                add_custom_command(
                    OUTPUT "${this_rt}${object_suffix}"
                    COMMAND "${genrt}" -r "${this_rt}" -o . -e object
                    "target=$<JOIN:$<REMOVE_DUPLICATES:${arch_target_list}>,$<COMMA>>"
                    DEPENDS "${genrt}"
                    VERBATIM)

                add_library("${this_rt}" STATIC "${this_rt}${object_suffix}")
                set_target_properties("${this_rt}" PROPERTIES LINKER_LANGUAGE CXX)
                _Halide_fix_xcode("${this_rt}")
            endforeach ()

            _Halide_lipo("${base_rt}" ${arch_rt})
        else ()
            set(target_list "$<LIST:TRANSFORM,${target_list},REPLACE,cmake,${Halide_CMAKE_TARGET}>")
            add_custom_command(
                OUTPUT "${RT}${object_suffix}"
                COMMAND "${genrt}" -r "${RT}" -o . -e object
                "target=$<JOIN:$<REMOVE_DUPLICATES:${target_list}>,$<COMMA>>"
                DEPENDS "${genrt}"
                VERBATIM)
            add_library("${RT}" STATIC "${RT}${object_suffix}")
            set_target_properties("${RT}" PROPERTIES LINKER_LANGUAGE CXX)
            _Halide_fix_xcode("${RT}")
        endif ()
    endif ()

    # Take care of the runtime/toolchain which doesn't have Threads or DL libs
    target_link_libraries("${RT}" INTERFACE Halide::Runtime)
    if (NOT ARG_NO_THREADS)
        find_package(Threads REQUIRED)
        target_link_libraries("${RT}" INTERFACE Threads::Threads)
    endif ()
    if (NOT ARG_NO_DL_LIBS)
        target_link_libraries("${RT}" INTERFACE ${CMAKE_DL_LIBS})
    endif ()

    if (NOT ARG_NO_DEFAULT_TARGETS)
        _Halide_add_targets_to_runtime("${RT}" TARGETS ${ARG_TARGETS})
    endif ()
endfunction()

function(_Halide_get_platform_details OUT_XC OUT_OBJ OUT_STATIC triple)
    if ("${triple}" MATCHES "host")
        set(triple "${Halide_HOST_TARGET}")
    endif ()

    if ("${triple}" MATCHES "cmake")
        set(triple "${Halide_CMAKE_TARGET}")
    endif ()

    list(SORT triple)
    list(SORT Halide_CMAKE_TARGET)
    string(COMPARE NOTEQUAL "${triple}" "${Halide_CMAKE_TARGET}" ${OUT_XC})
    set(${OUT_XC} "${${OUT_XC}}" PARENT_SCOPE)

    if ("${triple}" MATCHES "windows")
        set(${OUT_OBJ} ".obj" PARENT_SCOPE)
        set(${OUT_STATIC} ".lib" PARENT_SCOPE)
    else ()
        # All other OSes use .a
        set(${OUT_OBJ} ".o" PARENT_SCOPE)
        set(${OUT_STATIC} ".a" PARENT_SCOPE)
    endif ()
endfunction()

##
# Utility for finding GPU libraries that are needed by
# the runtime when listed in the Halide target string.
##

function(_Halide_add_targets_to_runtime TARGET)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "TARGETS")

    if (NOT TARGET "${TARGET}")
        message(FATAL_ERROR "not a target: ${TARGET}")
    endif ()

    get_property(aliased TARGET "${TARGET}" PROPERTY ALIASED_TARGET)
    if (aliased)
        set(TARGET "${aliased}")
    endif ()

    set_property(TARGET "${TARGET}" APPEND PROPERTY Halide_RT_TARGETS "${ARG_TARGETS}")
    _Halide_target_link_gpu_libs(${TARGET} INTERFACE ${ARG_TARGETS})
endfunction()

function(_Halide_target_link_gpu_libs TARGET VISIBILITY)
    if ("${ARGN}" MATCHES "metal")
        find_library(FOUNDATION_LIBRARY Foundation REQUIRED)
        find_library(METAL_LIBRARY Metal REQUIRED)
        target_link_libraries(${TARGET} ${VISIBILITY} "${FOUNDATION_LIBRARY}" "${METAL_LIBRARY}")
    endif ()

    if ("${ARGN}" MATCHES "webgpu" AND NOT "${ARGN}" MATCHES "wasm")
        find_package(Halide_WebGPU REQUIRED)
        target_link_libraries(${TARGET} ${VISIBILITY} Halide::WebGPU)
    endif ()
endfunction()

##
# Function for working around Xcode backend bugs
##

function(_Halide_fix_xcode TARGET)
    if (CMAKE_GENERATOR STREQUAL "Xcode")
        # Xcode generator requires at least one source file to work correctly.
        # Touching the empty file unconditionally would cause the archiver to
        # re-run every time CMake re-runs, even if nothing actually changed.
        set(empty_file "${CMAKE_CURRENT_BINARY_DIR}/Halide_${TARGET}_empty.cpp")
        if (NOT EXISTS "${empty_file}")
            file(TOUCH "${empty_file}")
        endif ()
        target_sources("${TARGET}" PRIVATE "${empty_file}")
    endif ()
endfunction()

function(_Halide_target_export_single_symbol TARGET SYMBOL)
    if (NOT EXISTS "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.${SYMBOL}.ldscript.apple")
        file(WRITE
             "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.${SYMBOL}.ldscript.apple"
             "_${SYMBOL}\n")
    endif ()
    if (NOT EXISTS "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.${SYMBOL}.ldscript")
        file(WRITE
             "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.${SYMBOL}.ldscript"
             "{ global: ${SYMBOL}; local: *; };\n")
    endif ()
    target_export_script(
        ${TARGET}
        APPLE_LD "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.${SYMBOL}.ldscript.apple"
        GNU_LD "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.${SYMBOL}.ldscript"
    )
endfunction()
