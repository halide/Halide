cmake_minimum_required(VERSION 3.22)

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

    _Halide_try_load_generators()

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
            endif()

            # Make a fake target here that we can attach the Python source to,
            # so that we can extract 'em in add_halide_library()
            add_custom_target(${TARGET} ALL)
            set_property(TARGET ${TARGET} PROPERTY Halide_PYTHON_GENERATOR_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/${ARG_SOURCES}")

            # TODO: what do we need to do for PACKAGE_NAME PACKAGE_NAMESPACE EXPORT_FILE in this case?
        else ()
            add_executable(${TARGET} ${ARG_SOURCES})
            add_executable(${gen} ALIAS ${TARGET})
            target_link_libraries(${TARGET} PRIVATE Halide::Generator ${ARG_LINK_LIBRARIES})

            if (NOT ARG_NO_DEFAULT_FLAGS AND NOT Halide_NO_DEFAULT_FLAGS)
                # For crosscompiling builds, the Halide headers will be included using -isystem,
                # which will cause all warnings to be ignored. This is not helpful, since
                # we *want* deprecation warnings to be propagated. So we must set
                # NO_SYSTEM_FROM_IMPORTED in order for it to be seen.
                set_target_properties(${TARGET} PROPERTIES NO_SYSTEM_FROM_IMPORTED YES)
                target_compile_options(${TARGET} PRIVATE
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
        endif()

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

# NOTE: this function must only be called by add_halide_generator
# since it reads from its scope.
function(_Halide_try_load_generators)
    # Don't repeatedly run the search for the tools package.
    if (NOT DEFINED ${ARG_PACKAGE_NAME}_FOUND)
        # Some toolchains, like Emscripten, try to disable finding packages
        # outside their sysroots, but we always want to find the native
        # generators. Setting CMAKE_FIND_ROOT_PATH_BOTH here overrides
        # the toolchain search preference. This is okay since a user can
        # always override this call by setting ${ARG_PACKAGE_NAME}_ROOT.
        find_package(${ARG_PACKAGE_NAME} QUIET
                     CMAKE_FIND_ROOT_PATH_BOTH)

        # Communicate found information to the caller
        set(${ARG_PACKAGE_NAME}_FOUND "${${ARG_PACKAGE_NAME}_FOUND}" PARENT_SCOPE)

        if (NOT ${ARG_PACKAGE_NAME}_FOUND AND CMAKE_CROSSCOMPILING AND NOT CMAKE_CROSSCOMPILING_EMULATOR)
            message(WARNING
                    "'${ARG_PACKAGE_NAME}' was not found and it looks like you "
                    "are cross-compiling without an emulator. This is likely to "
                    "fail. Please set -D${ARG_PACKAGE_NAME}_ROOT=... at the CMake "
                    "command line to the build directory of a host-built ${PROJECT_NAME}.")
        endif ()
    endif ()
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

    # "hash table" of extra outputs to extensions
    set(ASSEMBLY_extension ".s")
    set(BITCODE_extension ".bc")
    set(COMPILER_LOG_extension ".halide_compiler_log")
    set(FEATURIZATION_extension ".featurization")
    set(FUNCTION_INFO_HEADER_extension ".function_info.h")
    set(LLVM_ASSEMBLY_extension ".ll")
    set(PYTHON_EXTENSION_extension ".py.cpp")
    set(PYTORCH_WRAPPER_extension ".pytorch.h")
    set(REGISTRATION_extension ".registration.cpp")
    set(SCHEDULE_extension ".schedule.h")
    set(STMT_extension ".stmt")
    set(STMT_HTML_extension ".stmt.html")

    ##
    # Parse the arguments and set defaults for missing values.
    ##

    set(options C_BACKEND GRADIENT_DESCENT)
    set(oneValueArgs FROM GENERATOR FUNCTION_NAME NAMESPACE USE_RUNTIME AUTOSCHEDULER HEADER ${extra_output_names} NO_THREADS NO_DL_LIBS)
    set(multiValueArgs TARGETS FEATURES PARAMS PLUGINS)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT "${ARG_UNPARSED_ARGUMENTS}" STREQUAL "")
        message(AUTHOR_WARNING "Arguments to add_halide_library were not recognized: ${ARG_UNPARSED_ARGUMENTS}")
    endif ()

    if (NOT ARG_FROM)
        message(FATAL_ERROR "Missing FROM argument specifying a Halide generator target")
    endif ()

    if (NOT TARGET ${ARG_FROM})
        # FROM is usually an unqualified name; if we are crosscompiling, we might need a
        # fully-qualified name, so add the default package name and retry
        set(FQ_ARG_FROM "${PROJECT_NAME}::halide_generators::${ARG_FROM}")
        if (NOT TARGET ${FQ_ARG_FROM})
            message(FATAL_ERROR "Unable to locate FROM as either ${ARG_FROM} or ${FQ_ARG_FROM}")
        endif ()
        set(ARG_FROM "${FQ_ARG_FROM}")
    endif()

    get_property(py_src TARGET ${ARG_FROM} PROPERTY Halide_PYTHON_GENERATOR_SOURCE)
    if (py_src)
        # TODO: Python Generators need work to support crosscompiling (https://github.com/halide/Halide/issues/7014)
        if (NOT TARGET Halide::Python)
            message(FATAL_ERROR "This version of Halide was built without support for Python bindings; rebuild using WITH_PYTHON_BINDINGS=ON to use this rule with Python Generators.")
        endif ()
        if (NOT TARGET Python3::Interpreter)
            message(FATAL_ERROR "You must call find_package(Python3) in your CMake code in order to use this rule with Python Generators.")
        endif ()
        set(PYTHONPATH "$<TARGET_FILE_DIR:Halide::Python>/..")
        set(GENERATOR_CMD ${CMAKE_COMMAND} -E env PYTHONPATH=${PYTHONPATH} ${Python3_EXECUTABLE} $<SHELL_PATH:${py_src}>)
        set(GENERATOR_CMD_DEPS ${ARG_FROM} Halide::Python ${py_src})
    else()
        set(GENERATOR_CMD "${ARG_FROM}")
        set(GENERATOR_CMD_DEPS ${ARG_FROM})
        _Halide_place_dll(${ARG_FROM})
    endif()

    if (ARG_C_BACKEND)
        if (ARG_USE_RUNTIME)
            message(AUTHOR_WARNING "The C backend does not use a runtime.")
        endif ()
        if (ARG_TARGETS)
            message(AUTHOR_WARNING "The C backend sources will be compiled with the current CMake toolchain.")
        endif ()
    endif ()

    set(gradient_descent "$<BOOL:${ARG_GRADIENT_DESCENT}>")

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
            set(ARG_TARGETS "${Halide_CMAKE_TARGET}")
        endif ()
    endif ()

    list(TRANSFORM ARG_TARGETS REPLACE "cmake" "${Halide_CMAKE_TARGET}")

    list(APPEND ARG_FEATURES no_runtime)
    list(JOIN ARG_FEATURES "-" ARG_FEATURES)
    list(TRANSFORM ARG_TARGETS APPEND "-${ARG_FEATURES}")

    ##
    # Set up the runtime library, if needed
    ##

    if (ARG_C_BACKEND)
        # The C backend does not provide a runtime, so just supply headers.
        set(ARG_USE_RUNTIME Halide::Runtime)
    elseif (NOT ARG_USE_RUNTIME)
        # If we're not using an existing runtime, create one.

        # To forward NO_THREADS/NO_DL_LIBS args to add_halide_runtime()
        if (DEFINED ARG_NO_THREADS)
            set(CALL_ARG_NO_THREADS NO_THREADS ${ARG_NO_THREADS})
        endif ()
        if (DEFINED ARG_NO_DL_LIBS)
            set(CALL_ARG_NO_DL_LIBS NO_DL_LIBS ${ARG_NO_DL_LIBS})
        endif ()

        add_halide_runtime("${TARGET}.runtime" TARGETS ${ARG_TARGETS} FROM ${ARG_FROM} ${CALL_ARG_NO_THREADS} ${CALL_ARG_NO_DL_LIBS})
        set(ARG_USE_RUNTIME "${TARGET}.runtime")
    elseif (NOT TARGET ${ARG_USE_RUNTIME})
        message(FATAL_ERROR "Invalid runtime target ${ARG_USE_RUNTIME}")
    else ()
        _Halide_add_targets_to_runtime(${ARG_USE_RUNTIME} TARGETS ${ARG_TARGETS})
    endif ()

    ##
    # Determine which outputs the generator call will emit.
    ##

    _Halide_get_platform_details(
            is_crosscompiling
            object_suffix
            static_library_suffix
            ${ARG_TARGETS})

    # Always emit a C header
    set(generator_outputs c_header)
    set(generator_output_files "${TARGET}.h")
    if (ARG_HEADER)
        set(${ARG_HEADER} "${TARGET}.h" PARENT_SCOPE)
    endif ()

    # Then either a C source, a set of object files, or a cross-compiled static library.
    if (ARG_C_BACKEND)
        list(APPEND generator_outputs c_source)
        set(generator_sources "${TARGET}.halide_generated.cpp")
    elseif (is_crosscompiling)
        # When cross-compiling, we need to use a static, imported library
        list(APPEND generator_outputs static_library)
        set(generator_sources "${TARGET}${static_library_suffix}")
    else ()
        # When compiling for the current CMake toolchain, create a native
        list(APPEND generator_outputs object)
        list(LENGTH ARG_TARGETS len)
        if (len EQUAL 1)
            set(generator_sources "${TARGET}${object_suffix}")
        else ()
            set(generator_sources ${ARG_TARGETS})
            list(TRANSFORM generator_sources PREPEND "${TARGET}-")
            list(TRANSFORM generator_sources APPEND "${object_suffix}")
            list(APPEND generator_sources "${TARGET}_wrapper${object_suffix}")
        endif ()
    endif ()
    list(APPEND generator_output_files ${generator_sources})

    # Add in extra outputs using the table defined at the start of this function
    foreach (out IN LISTS extra_output_names)
        if (ARG_${out})
            set(${ARG_${out}} "${TARGET}${${out}_extension}" PARENT_SCOPE)
            list(APPEND generator_output_files "${TARGET}${${out}_extension}")
            string(TOLOWER "${out}" out)
            list(APPEND generator_outputs ${out})
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

    if (is_crosscompiling)
        add_library("${TARGET}" STATIC IMPORTED GLOBAL)
        set_target_properties("${TARGET}" PROPERTIES
                              IMPORTED_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/${generator_sources}")
    else ()
        add_library("${TARGET}" STATIC ${generator_sources})
        set_target_properties("${TARGET}" PROPERTIES
                              POSITION_INDEPENDENT_CODE ON
                              LINKER_LANGUAGE CXX)
        _Halide_fix_xcode("${TARGET}")
    endif ()

    # Load the plugins and setup dependencies
    set(generator_plugins "")
    if (ARG_PLUGINS)
        foreach (p IN LISTS ARG_PLUGINS)
            list(APPEND generator_plugins "$<TARGET_FILE:${p}>")
        endforeach ()
        # $<JOIN:> gets confused about quoting. Just use list(JOIN) here instead.
        list(JOIN generator_plugins $<COMMA> generator_plugins_list)
        set(generator_plugins -p ${generator_plugins_list})
    endif ()

    add_custom_command(OUTPUT ${generator_output_files}
                       COMMAND ${GENERATOR_CMD}
                       -n "${TARGET}"
                       -d "${gradient_descent}"
                       -g "${ARG_GENERATOR}"
                       -f "${ARG_FUNCTION_NAME}"
                       -e "$<JOIN:${generator_outputs},$<COMMA>>"
                       ${generator_plugins}
                       -o .
                       "target=$<JOIN:${ARG_TARGETS},$<COMMA>>"
                       ${ARG_PARAMS}
                       DEPENDS ${GENERATOR_CMD_DEPS} ${ARG_PLUGINS}
                       VERBATIM)

    list(TRANSFORM generator_output_files PREPEND "${CMAKE_CURRENT_BINARY_DIR}/")
    add_custom_target("${TARGET}.update" ALL DEPENDS ${generator_output_files})

    add_dependencies("${TARGET}" "${TARGET}.update")

    target_include_directories("${TARGET}" INTERFACE "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>")
    target_link_libraries("${TARGET}" INTERFACE "${ARG_USE_RUNTIME}")

    # Save some info for add_halide_python_extension_library() in case it is used for this target.
    set_property(TARGET "${TARGET}" PROPERTY Halide_LIBRARY_RUNTIME_TARGET "${ARG_USE_RUNTIME}")
    set_property(TARGET "${TARGET}" PROPERTY Halide_LIBRARY_FUNCTION_NAME "${ARG_FUNCTION_NAME}")
    if ("python_extension" IN_LIST generator_outputs)
        set_property(TARGET "${TARGET}" PROPERTY Halide_LIBRARY_PYTHON_EXTENSION_CPP "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.py.cpp")
    endif ()
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
    _Halide_gengen_ensure()
    add_custom_command(OUTPUT ${pyext_module_definition_src}
                       COMMAND _Halide_gengen -r "${pyext_runtime_name}" -e python_extension -o "${CMAKE_CURRENT_BINARY_DIR}" target=host
                       DEPENDS _Halide_gengen
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

    add_custom_command(TARGET ${GEN} POST_BUILD
                       COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Halide::Halide> $<TARGET_FILE_DIR:${GEN}>)
    set_property(TARGET ${GEN} PROPERTY Halide_GENERATOR_HAS_POST_BUILD 1)
endfunction()

##
# Function for creating a standalone runtime from a generator.
##

function(add_halide_runtime RT)
    set(options "")
    set(oneValueArgs FROM NO_THREADS NO_DL_LIBS)
    set(multiValueArgs TARGETS)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})


    # If no TARGETS argument, use Halide_TARGET instead
    if (NOT ARG_TARGETS)
        set(ARG_TARGETS "${Halide_TARGET}")
    endif ()

    # Ensure all targets are tagged with "no_runtime",
    # so that GCD calculation doesn't get confused.
    list(TRANSFORM ARG_TARGETS APPEND "-no_runtime")

    if (ARG_FROM)
        # Try to use generator which is available. This is essential for cross-compilation
        # where we cannot use host compiler to build generator only for runtime.

        # Need to check if the ones for python extension, which is not actually an executable
        get_target_property(target_type ${ARG_FROM} TYPE)
        get_target_property(aliased ${ARG_FROM} ALIASED_TARGET)
        if (target_type STREQUAL "EXECUTABLE" AND NOT aliased)
            add_executable(_Halide_gengen ALIAS ${ARG_FROM})
        endif()
    endif()

    # The default of NO_THREADS/NO_DL_LIBS is OFF unless Halide_RUNTIME_NO_THREADS/NO_DL_LIBS is defined globally
    if (NOT DEFINED ARG_NO_THREADS)
        set(ARG_NO_THREADS ${Halide_RUNTIME_NO_THREADS})
    endif ()
    if (NOT DEFINED ARG_NO_DL_LIBS)
        set(ARG_NO_DL_LIBS ${Halide_RUNTIME_NO_DL_LIBS})
    endif ()

    # Ensure _Halide_gengen is defined
    _Halide_gengen_ensure()

    _Halide_get_platform_details(
            is_crosscompiling
            object_suffix
            static_library_suffix
            ${ARG_TARGETS})

    if (is_crosscompiling)
        set(GEN_OUTS "${RT}${static_library_suffix}")
        set(GEN_ARGS "")
    else ()
        set(GEN_OUTS "${RT}${object_suffix}")
        set(GEN_ARGS -e object)
    endif ()

    add_custom_command(OUTPUT ${GEN_OUTS}
                       COMMAND _Halide_gengen -r "${RT}" -o . ${GEN_ARGS}
                       # Defers reading the list of targets for which to generate a common runtime to CMake _generation_ time.
                       # This prevents issues where a lower GCD is required by a later Halide library linking to this runtime.
                       target=$<JOIN:$<TARGET_PROPERTY:${RT},Halide_RT_TARGETS>,$<COMMA>>
                       DEPENDS _Halide_gengen
                       VERBATIM)

    if (is_crosscompiling)
        add_custom_target("${RT}.update" DEPENDS "${GEN_OUTS}")

        add_library("${RT}" STATIC IMPORTED GLOBAL)
        add_dependencies("${RT}" "${RT}.update")

        set_target_properties("${RT}" PROPERTIES
                              IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/${GEN_OUTS})
    else ()
        add_library("${RT}" STATIC ${GEN_OUTS})
        set_target_properties("${RT}" PROPERTIES LINKER_LANGUAGE CXX)
        _Halide_fix_xcode("${RT}")
    endif ()

    # Take care of the runtime/toolchain which doesn't have Threads or DL libs
    if (NOT ARG_NO_THREADS AND NOT TARGET Threads::Threads)
        find_package(Threads REQUIRED)
    endif ()
    target_link_libraries("${RT}" INTERFACE
                          Halide::Runtime
                          $<$<NOT:$<BOOL:${ARG_NO_THREADS}>>:Threads::Threads>
                          $<$<NOT:$<BOOL:${ARG_NO_DL_LIBS}>>:${CMAKE_DL_LIBS}>)
    _Halide_add_targets_to_runtime("${RT}" TARGETS ${ARG_TARGETS})
endfunction()

function(_Halide_get_platform_details OUT_XC OUT_OBJ OUT_STATIC)
    if ("${ARGN}" MATCHES "host")
        set(ARGN "${Halide_HOST_TARGET}")
    endif ()

    if ("${ARGN}" MATCHES "windows")
        # Otherwise, all targets are windows, so Halide emits .obj files
        set(${OUT_OBJ} ".obj" PARENT_SCOPE)
        set(${OUT_STATIC} ".lib" PARENT_SCOPE)
    else ()
        # All other targets use .a
        set(${OUT_OBJ} ".o" PARENT_SCOPE)
        set(${OUT_STATIC} ".a" PARENT_SCOPE)
    endif ()

    # Well-formed targets must either start with "host" or a target triple.
    if ("${ARGN}" MATCHES "host")
        set(halide_triple ${Halide_HOST_TARGET})
    else ()
        string(REGEX REPLACE "^([^-]+-[^-]+-[^-]+).*$" "\\1" halide_triple "${ARGN}")
    endif ()

    if (NOT Halide_CMAKE_TARGET STREQUAL halide_triple)
        set("${OUT_XC}" 1 PARENT_SCOPE)
    else ()
        set("${OUT_XC}" 0 PARENT_SCOPE)
    endif ()
endfunction()

##
# Utility for finding GPU libraries that are needed by
# the runtime when listed in the Halide target string.
##

function(_Halide_add_targets_to_runtime TARGET)
    cmake_parse_arguments(ARG "" "" "TARGETS" ${ARGN})

    # Remove features that should not be attached to a runtime
    # TODO: The fact that removing profile fixes a duplicate symbol linker error on Windows smells like a bug.
    list(TRANSFORM ARG_TARGETS REPLACE "-(user_context|no_asserts|no_bounds_query|no_runtime|profile)" "")
    set_property(TARGET "${TARGET}" APPEND PROPERTY Halide_RT_TARGETS "${ARG_TARGETS}")

    _Halide_target_link_gpu_libs(${TARGET} INTERFACE ${ARG_TARGETS})
endfunction()

function(_Halide_target_link_gpu_libs TARGET VISIBILITY)
    # TODO(https://github.com/halide/Halide/issues/5633): verify that this is correct & necessary for OpenGLCompute
    if ("${ARGN}" MATCHES "openglcompute")
        if ("${ARGN}" MATCHES "egl")
            find_package(OpenGL REQUIRED COMPONENTS OpenGL EGL)
            target_link_libraries(${TARGET} ${VISIBILITY} OpenGL::OpenGL OpenGL::EGL)
        else ()
            if ("${ARGN}" MATCHES "linux" OR ("${ARGN}" MATCHES "host" AND Halide_HOST_TARGET MATCHES "linux"))
                find_package(X11 REQUIRED)
                target_link_libraries(${TARGET} ${VISIBILITY} X11::X11)
            endif ()

            find_package(OpenGL REQUIRED)
            target_link_libraries(${TARGET} ${VISIBILITY} OpenGL::GL)
        endif ()
    endif ()

    if ("${ARGN}" MATCHES "metal")
        find_library(FOUNDATION_LIBRARY Foundation REQUIRED)
        find_library(METAL_LIBRARY Metal REQUIRED)
        target_link_libraries(${TARGET} ${VISIBILITY} "${FOUNDATION_LIBRARY}" "${METAL_LIBRARY}")
    endif ()

    if ("${ARGN}" MATCHES "webgpu")
        if (WEBGPU_NATIVE_LIB)
            target_link_libraries(${TARGET} INTERFACE ${WEBGPU_NATIVE_LIB})
        endif ()
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
    endif()
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

function(_Halide_gengen_ensure)
    # Create a Generator that is GenGen.cpp and nothing else; all it can do is generate a runtime.
    if (NOT TARGET _Halide_gengen)

        # add_executable requires at least one source file for some
        # configs (e.g. Xcode), because, uh, reasons, so we'll create
        # an empty one here to satisfy it
        set(empty "${CMAKE_CURRENT_BINARY_DIR}/_Halide_gengen.empty.cpp")
        if (NOT EXISTS "${empty}")
            file(WRITE "${empty}" "/* nothing */\n")
        endif ()

        add_executable(_Halide_gengen "${empty}")
        target_link_libraries(_Halide_gengen PRIVATE Halide::Generator)
        _Halide_place_dll(_Halide_gengen)
    endif ()
endfunction()

