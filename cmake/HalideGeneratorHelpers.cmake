cmake_minimum_required(VERSION 3.16)

include(${CMAKE_CURRENT_LIST_DIR}/HalideTargetHelpers.cmake)

define_property(TARGET PROPERTY Halide_RT_TARGETS
                BRIEF_DOCS "On a Halide runtime target, lists the targets the runtime backs"
                FULL_DOCS "On a Halide runtime target, lists the targets the runtime backs")

define_property(TARGET PROPERTY Halide_GENERATOR_HAS_POST_BUILD
                BRIEF_DOCS "On a Halide generator target, true if Halide.dll copy command has already been added."
                FULL_DOCS "On a Halide generator target, true if Halide.dll copy command has already been added.")

##
# Function to simplify writing the CMake rules for creating a generator executable
# that follows our recommended cross-compiling workflow.
##

function(add_halide_generator TARGET)
    set(options "")
    set(oneValueArgs PACKAGE_NAME PACKAGE_NAMESPACE EXPORT_FILE)
    set(multiValueArgs SOURCES)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT ARG_PACKAGE_NAME)
        set(ARG_PACKAGE_NAME "${PROJECT_NAME}-halide_generators")
    endif ()

    if (NOT ARG_PACKAGE_NAMESPACE)
        set(ARG_PACKAGE_NAMESPACE "${PROJECT_NAME}::halide_generators::")
    endif ()

    if (NOT ARG_EXPORT_FILE)
        file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/cmake")
        set(ARG_EXPORT_FILE "${PROJECT_BINARY_DIR}/cmake/${ARG_PACKAGE_NAME}-config.cmake")
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

        add_executable(${TARGET} ${ARG_SOURCES})
        add_executable(${gen} ALIAS ${TARGET})
        target_link_libraries(${TARGET} PRIVATE Halide::Generator)

        add_dependencies("${ARG_PACKAGE_NAME}" ${TARGET})
        export(TARGETS ${TARGET}
               NAMESPACE ${ARG_PACKAGE_NAMESPACE}
               APPEND FILE "${ARG_EXPORT_FILE}")
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

        if (NOT ${ARG_PACKAGE_NAME}_FOUND AND CMAKE_CROSSCOMPILING)
            message(WARNING
                    "${ARG_PACKAGE_NAME} were not found and it looks like you are cross-compiling. "
                    "This is likely to fail. Please set -D${ARG_PACKAGE_NAME}_ROOT=... at the CMake "
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
    set(oneValueArgs FROM GENERATOR FUNCTION_NAME NAMESPACE USE_RUNTIME AUTOSCHEDULER HEADER ${extra_output_names})
    set(multiValueArgs TARGETS FEATURES PARAMS PLUGINS)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT "${ARG_UNPARSED_ARGUMENTS}" STREQUAL "")
        message(AUTHOR_WARNING "Arguments to add_halide_library were not recognized: ${ARG_UNPARSED_ARGUMENTS}")
    endif ()

    if (NOT ARG_FROM)
        message(FATAL_ERROR "Missing FROM argument specifying a Halide generator target")
    endif ()

    _Halide_place_dll(${ARG_FROM})

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
        _Halide_add_halide_runtime("${TARGET}.runtime" FROM ${ARG_FROM}
                                   TARGETS ${ARG_TARGETS})
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

    set(autoscheduler "")
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
        set(autoscheduler -s "${ARG_AUTOSCHEDULER}")
        list(PREPEND ARG_PARAMS auto_schedule=true)
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
        set(generator_plugins -p "$<JOIN:${generator_plugins},$<COMMA>>")
    endif ()

    add_custom_command(OUTPUT ${generator_output_files}
                       COMMAND ${ARG_FROM}
                       -n "${TARGET}"
                       -d "${gradient_descent}"
                       -g "${ARG_GENERATOR}"
                       -f "${ARG_FUNCTION_NAME}"
                       -e "$<JOIN:${generator_outputs},$<COMMA>>"
                       ${generator_plugins}
                       ${autoscheduler}
                       -o .
                       "target=$<JOIN:${ARG_TARGETS},$<COMMA>>"
                       ${ARG_PARAMS}
                       DEPENDS "${ARG_FROM}" ${ARG_PLUGINS}
                       VERBATIM)

    list(TRANSFORM generator_output_files PREPEND "${CMAKE_CURRENT_BINARY_DIR}/")
    add_custom_target("${TARGET}.update" ALL DEPENDS ${generator_output_files})

    add_dependencies("${TARGET}" "${TARGET}.update")

    target_include_directories("${TARGET}" INTERFACE "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>")
    target_link_libraries("${TARGET}" INTERFACE "${ARG_USE_RUNTIME}")
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

function(_Halide_add_halide_runtime RT)
    cmake_parse_arguments(ARG "" "FROM" "TARGETS" ${ARGN})
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
                       COMMAND ${ARG_FROM} -r "${TARGET}.runtime" -o . ${GEN_ARGS}
                       # Defers reading the list of targets for which to generate a common runtime to CMake _generation_ time.
                       # This prevents issues where a lower GCD is required by a later Halide library linking to this runtime.
                       target=$<JOIN:$<TARGET_PROPERTY:${TARGET}.runtime,Halide_RT_TARGETS>,$<COMMA>>
                       DEPENDS "${ARG_FROM}"
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

    target_link_libraries("${RT}" INTERFACE Halide::Runtime Threads::Threads ${CMAKE_DL_LIBS})
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
        find_library(METAL_LIBRARY Metal)
        if (NOT METAL_LIBRARY)
            message(AUTHOR_WARNING "Metal framework dependency not found on system.")
        else ()
            target_link_libraries(${TARGET} ${VISIBILITY} "${METAL_LIBRARY}")
        endif ()

        find_library(FOUNDATION_LIBRARY Foundation)
        if (NOT FOUNDATION_LIBRARY)
            message(AUTHOR_WARNING "Foundation framework dependency not found on system.")
        else ()
            target_link_libraries(${TARGET} ${VISIBILITY} "${FOUNDATION_LIBRARY}")
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
