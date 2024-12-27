# This file wraps the upstream package config modules for LLVM, Clang, and LLD
# to fix pathological issues in their implementations. It creates imported targets
# that wrap the key features needed by Halide.

set(REASON_FAILURE_MESSAGE "")

# Fallback configurations for weirdly built LLVMs
set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL MinSizeRel Release RelWithDebInfo "")
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO RelWithDebInfo Release MinSizeRel "")
set(CMAKE_MAP_IMPORTED_CONFIG_RELEASE Release MinSizeRel RelWithDebInfo "")

find_package(LLVM ${PACKAGE_FIND_VERSION} CONFIG)

set(Halide_LLVM_VERSION "${LLVM_PACKAGE_VERSION}")

# TODO: deprecated in Halide 19.0.0, remove in Halide 20.0.0
if (NOT DEFINED Halide_LLVM_SHARED_LIBS AND DEFINED Halide_SHARED_LLVM)
    set(Halide_LLVM_SHARED_LIBS "${Halide_SHARED_LLVM}")
    message(DEPRECATION
            "Halide_SHARED_LLVM has been renamed to Halide_LLVM_SHARED_LIBS.")
endif ()

if (NOT DEFINED Halide_LLVM_SHARED_LIBS)
    # Normally, we don't like making decisions for our users. However,
    # this avoids an incompatible scenario that is checked below. So
    # if we didn't do this, the package would fail to be found and
    # the user would have to either rebuild LLVM or flip this value.
    if (LLVM_FOUND AND "WebAssembly" IN_LIST LLVM_TARGETS_TO_BUILD AND LLVM_LINK_LLVM_DYLIB)
        set(Halide_LLVM_SHARED_LIBS YES)
    else ()
        set(Halide_LLVM_SHARED_LIBS NO)
    endif ()
endif ()

option(Halide_LLVM_SHARED_LIBS "Enable to link to shared libLLVM" "${Halide_LLVM_SHARED_LIBS}")

if (LLVM_FOUND)
    find_package(Clang HINTS "${LLVM_INSTALL_PREFIX}" "${LLVM_DIR}/../clang" "${LLVM_DIR}/../lib/cmake/clang")

    foreach (comp IN LISTS LLVM_TARGETS_TO_BUILD)
        if (comp STREQUAL "WebAssembly")
            set(Halide_LLVM_${comp}_FOUND 0)

            find_package(LLD HINTS "${LLVM_INSTALL_PREFIX}" "${LLVM_DIR}/../lld" "${LLVM_DIR}/../lib/cmake/lld")
            if (NOT LLD_FOUND)
                string(APPEND REASON_FAILURE_MESSAGE
                       "WebAssembly was not found because liblld is missing. "
                       "Did you `apt install liblld-dev` or `brew install lld`?\n")
                continue()
            endif ()

            # LLVM has a mis-feature that allows it to build and export both static and shared libraries at the same
            # time, while inconsistently linking its own static libraries (for lldWasm and others) to the shared
            # library. Ignoring this causes Halide to link to both the static AND the shared LLVM libs and it breaks at
            # runtime. See: https://github.com/halide/Halide/issues/5471
            if (LLVM_LINK_LLVM_DYLIB AND NOT Halide_LLVM_SHARED_LIBS)
                string(APPEND REASON_FAILURE_MESSAGE
                       "WebAssembly was not found because LLD required by was linked to shared LLVM "
                       "(LLVM_LINK_LLVM_DYLIB=${LLVM_LINK_LLVM_DYLIB}) but static LLVM was requested "
                       "(Halide_LLVM_SHARED_LIBS=${Halide_LLVM_SHARED_LIBS}).\n")
                continue()
            endif ()
        endif ()

        set(Halide_LLVM_${comp}_FOUND 1)
    endforeach ()

    set(Halide_LLVM_SHARED_LIBRARY "LLVM")
    if (Halide_LLVM_SHARED_LIBS AND NOT TARGET "${Halide_LLVM_SHARED_LIBRARY}")
        string(APPEND Halide_LLVM_SHARED_LIBRARY "-NOTFOUND")
        string(APPEND REASON_FAILURE_MESSAGE
               "Halide_LLVM_SHARED_LIBS=${Halide_LLVM_SHARED_LIBS} but the shared LLVM target does not exist.\n")
    endif ()
endif ()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
    Halide_LLVM
    REQUIRED_VARS LLVM_CONFIG Clang_CONFIG Halide_LLVM_SHARED_LIBRARY
    VERSION_VAR Halide_LLVM_VERSION
    REASON_FAILURE_MESSAGE "${REASON_FAILURE_MESSAGE}"
    HANDLE_COMPONENTS
    HANDLE_VERSION_RANGE
    NAME_MISMATCHED
)

function(_Halide_LLVM_link target visibility)
    llvm_map_components_to_libnames(comps ${ARGN})
    target_link_libraries("${target}" "${visibility}" ${comps})
endfunction()

if (Halide_LLVM_FOUND)
    set(Halide_LLVM_COMPONENTS "")
    foreach (comp IN LISTS Halide_LLVM_FIND_COMPONENTS)
        if (Halide_LLVM_${comp}_FOUND)
            list(APPEND Halide_LLVM_COMPONENTS "${comp}")
        endif ()
    endforeach ()

    if (NOT TARGET Halide_LLVM::Core)
        add_library(Halide_LLVM::Core INTERFACE IMPORTED)

        # LLVM_DEFINITIONS is a space-separated list instead of a more typical
        # CMake semicolon-separated list. For a long time, CMake could handle
        # this transparently but, since LLVM 17, the flag -D_FILE_OFFSET_BITS=64
        # appears on 32-bit Linux. The presence of the `=` here stops CMake
        # from splitting on spaces, instead corrupting the command line by
        # folding the other flags into the value of -D_FILE_OFFSET_BITS=64.
        # For better or worse, since the flag also appears twice, the second
        # `=` is folded into the value of the first and we get errors of the
        # form:
        #
        #   <command-line>: error: token "=" is not valid in preprocessor expressions
        #
        separate_arguments(LLVM_DEFINITIONS NATIVE_COMMAND "${LLVM_DEFINITIONS}")
        list(REMOVE_ITEM LLVM_DEFINITIONS "-D_GLIBCXX_ASSERTIONS") # work around https://reviews.llvm.org/D142279
        list(APPEND LLVM_DEFINITIONS "LLVM_VERSION=${LLVM_VERSION_MAJOR}${LLVM_VERSION_MINOR}")

        target_compile_definitions(Halide_LLVM::Core INTERFACE ${LLVM_DEFINITIONS})
        target_include_directories(Halide_LLVM::Core INTERFACE "${LLVM_INCLUDE_DIRS}")

        set_property(TARGET Halide_LLVM::Core PROPERTY INTERFACE_CXX_RTTI "${LLVM_ENABLE_RTTI}")
        set_property(TARGET Halide_LLVM::Core APPEND PROPERTY COMPATIBLE_INTERFACE_BOOL CXX_RTTI)

        if (LLVM_LIBCXX GREATER -1)
            target_compile_options(Halide_LLVM::Core INTERFACE "$<$<LINK_LANGUAGE:CXX>:-stdlib=libc++>")
            target_link_options(Halide_LLVM::Core INTERFACE "$<$<LINK_LANGUAGE:CXX>:-stdlib=libc++>")
        endif ()

        if (Halide_LLVM_SHARED_LIBS)
            target_link_libraries(Halide_LLVM::Core INTERFACE LLVM ${CMAKE_DL_LIBS})
        else ()
            _Halide_LLVM_link(Halide_LLVM::Core INTERFACE orcjit bitwriter linker passes)
        endif ()
    endif ()

    foreach (comp IN LISTS Halide_LLVM_COMPONENTS)
        if (NOT TARGET Halide_LLVM::${comp})
            add_library(Halide_LLVM::${comp} INTERFACE IMPORTED)
            target_link_libraries(Halide_LLVM::${comp} INTERFACE Halide_LLVM::Core)

            if (NOT Halide_LLVM_SHARED_LIBS)
                _Halide_LLVM_link(Halide_LLVM::${comp} INTERFACE ${comp})
            endif ()

            if (comp STREQUAL "WebAssembly")
                target_include_directories(Halide_LLVM::WebAssembly INTERFACE ${LLD_INCLUDE_DIRS})
                target_link_libraries(Halide_LLVM::WebAssembly INTERFACE lldWasm lldCommon)
            endif ()
        endif ()
    endforeach ()

    find_package(MLIR CONFIG HINTS "${LLVM_INSTALL_PREFIX}" "${LLVM_DIR}/../mlir" "${LLVM_DIR}/../lib/cmake/mlir")
    if (MLIR_FOUND)
        target_include_directories(Halide_LLVM::Core INTERFACE "$<BUILD_INTERFACE:${MLIR_INCLUDE_DIRS}>")
        target_link_libraries(Halide_LLVM::Core INTERFACE
            MLIRAnalysis
            MLIRIR
            MLIRArithDialect
            MLIRFuncDialect
            MLIRMemRefDialect
            MLIRSCFDialect
            MLIRVectorDialect
        )
    endif ()
endif ()
