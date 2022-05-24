#[=======================================================================[.rst:
FindHalideLLVM
--------------

This find module locates LLVM, Clang, and LLD the way Halide intends to
use it. LLVM and its associated components' CMake packages have many
idiosyncrasies; this module attempts to paper over them.

Optional components
^^^^^^^^^^^^^^^^^^^

This module respects several optional components:

* ``Clang``
* ``LLD``
* ``<target>`` - any of of the following Halide-supported targets:
    * ``AArch64``
    * ``AMDGPU``
    * ``ARM``
    * ``Hexagon``
    * ``Mips``
    * ``NVPTX``
    * ``PowerPC``
    * ``RISCV``
    * ``WebAssembly`` - depends on ``LLD``
    * ``X86``

Input variables
^^^^^^^^^^^^^^^

* ``Halide_SHARED_LLVM`` -- link to the shared library instead of a set
  of static libraries
* ``Halide_BUNDLE_LLVM`` -- link directly to the unpacked object files
  of the set of static libraries.

Result variables
^^^^^^^^^^^^^^^^

This package defines the following variables:

* ``HalideLLVM_FOUND`` -- set to true if the package was found,
  including all required components.
* ``HalideLLVM_<comp>_FOUND`` -- set to true if the component was
  found and false if not. Defined only for requested components.
* ``HalideLLVM_VERSION`` -- set to the LLVM version that was found.
* ``HalideLLVM_TARGETS`` -- the list of requested ``<target>``s that
  were actually found, whether required or not.

It also forwards the following variables from the underlying LLVM
package.

* ``LLVM_LIBCXX`` -- set to the version of libc++ to which LLVM
  was linked, or to -1 if it was not.
* ``LLVM_ENABLE_RTTI`` -- set to true if LLVM was compiled with
  RTII enabled.

Imported targets
^^^^^^^^^^^^^^^^

This module defines the IMPORTED targets:

* ``Halide::LLVM`` -- links to LLVM such that all of the REQUIRED targets
  and as many of the OPTIONAL targets as possible are included. Defines
  the preprocessor directive ``WITH_<TARGET>`` (uppercase) when
  ``<target>`` is included.
* ``Halide::clang`` -- the main clang executable (when ``Clang`` component is
  found).
* ``Halide::llvm-as`` -- the ``llvm-as`` executable.

#]=======================================================================]


include(FindPackageHandleStandardArgs)

# We do most of our processing inside a function to control what gets
# added to the caller's scope. In particular, we should not clobber
# the various CMAKE_MAP_IMPORTED_CONFIG_* variables and we shouldn't
# expose anything from the LLVM packages that we don't intend, either.
function(_FindHalideLLVM)
    ##
    # Set up component lists and result variables
    ##

    # List of errors that arise while finding and validating components.
    set(errors "")

    # Defensively mark all requested and special components as not found, so that
    # they cannot be accidentally set by a different find module invocation or by
    # the user.
    foreach (comp IN LISTS "${CMAKE_FIND_PACKAGE_NAME}_FIND_COMPONENTS")
        set(HalideLLVM_${comp}_FOUND 0)
    endforeach ()

    # WebAssembly depends on LLD, so if it was requested, then request LLD, too
    if ("WebAssembly" IN_LIST "${CMAKE_FIND_PACKAGE_NAME}_FIND_COMPONENTS")
        list(APPEND "${CMAKE_FIND_PACKAGE_NAME}_FIND_COMPONENTS" LLD)

        # If it was required, then mark LLD as required, too.
        if (${CMAKE_FIND_PACKAGE_NAME}_FIND_REQUIRED_WebAssembly)
            set("${CMAKE_FIND_PACKAGE_NAME}_FIND_REQUIRED_LLD" 1)
        endif ()
    endif ()

    # Compute list of targets to include in Halide::LLVM
    set(requested_targets "${${CMAKE_FIND_PACKAGE_NAME}_FIND_COMPONENTS}")
    list(REMOVE_ITEM requested_targets Clang LLD)

    ##
    # Find LLVM, respecting standard configuration settings and QUIET/REQUIRED flags
    ##

    # Fallback configurations for weirdly built LLVMs
    set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL MinSizeRel Release RelWithDebInfo "")
    set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO RelWithDebInfo Release MinSizeRel "")
    set(CMAKE_MAP_IMPORTED_CONFIG_RELEASE Release MinSizeRel RelWithDebInfo "")

    # Detect whether QUIET and/or REQUIRED flags should be passed to internal find_package() calls
    set(quiet "")
    if (${CMAKE_FIND_PACKAGE_NAME}_FIND_QUIETLY)
        set(quiet QUIET)
    endif ()

    set(required "")
    if (${CMAKE_FIND_PACKAGE_NAME}_FIND_REQUIRED)
        set(required REQUIRED)
    endif ()

    # Run the actual search for LLVM
    find_package(LLVM ${${CMAKE_FIND_PACKAGE_NAME}_FIND_VERSION} ${quiet} ${required})

    # All LLVM-related packages (incorrectly) report their version numbers via LLVM_PACKAGE_VERSION.
    # Store the LLVM version privately to validate subsequent searches for Clang and/or LLD.
    set(llvm_found_version "${LLVM_PACKAGE_VERSION}")
    unset(LLVM_PACKAGE_VERSION)

    # Check the LLVM version
    if (LLVM_FOUND)
        set(llvm_min_ver 12.0.0)
        set(llvm_max_ver 15.0)
        if (llvm_found_version VERSION_LESS llvm_min_ver)
            # We didn't find LLVM if version is too old.
            set(LLVM_FOUND 0)
            list(APPEND errors "LLVM version must be ${llvm_min_ver} or newer.")
        elseif (llvm_found_version VERSION_GREATER llvm_max_ver AND NOT quiet)
            message(WARNING "Halide is not tested on LLVM versions beyond ${llvm_max_ver}")
        endif ()
    endif ()

    # We didn't find LLVM if shared version was requested, but not not present
    if (LLVM_FOUND AND Halide_SHARED_LLVM AND NOT TARGET LLVM)
        set(LLVM_FOUND 0)
        list(APPEND errors "LLVM was found, but Halide_SHARED_LLVM was set and no shared library was present.")
    endif ()

    ##
    # Look for Clang, LLD, and target components. Check for internal inconsistencies.
    ##

    set(HalideLLVM_CONFIG "")
    if (LLVM_FOUND)
        set(HalideLLVM_CONFIG "${LLVM_CONFIG}")

        if (Clang IN_LIST "${CMAKE_FIND_PACKAGE_NAME}_FIND_COMPONENTS")
            find_package(Clang ${quiet} HINTS "${LLVM_DIR}/../clang" "${LLVM_DIR}/../lib/cmake/clang")

            # Validate Clang version
            if (Clang_FOUND AND NOT LLVM_PACKAGE_VERSION VERSION_EQUAL llvm_found_version)
                set(Clang_FOUND 0)
                list(APPEND errors "LLVM ${llvm_found_version} and Clang ${LLVM_PACKAGE_VERSION} are incompatible.")
            endif ()
            unset(LLVM_PACKAGE_VERSION)

            # Set the component-specific FOUND-variable
            set(HalideLLVM_Clang_FOUND "${Clang_FOUND}")
            if (HalideLLVM_Clang_FOUND)
                list(APPEND HalideLLVM_CONFIG "${Clang_CONFIG}")
            endif ()
        endif ()

        if (LLD IN_LIST "${CMAKE_FIND_PACKAGE_NAME}_FIND_COMPONENTS")
            find_package(LLD ${quiet} HINTS "${LLVM_DIR}/../lld" "${LLVM_DIR}/../lib/cmake/lld")

            # Validate LLD version
            if (LLD_FOUND AND NOT LLVM_PACKAGE_VERSION VERSION_EQUAL llvm_found_version)
                set(LLD_FOUND 0)
                list(APPEND errors "LLVM ${llvm_found_version} and LLD ${LLVM_PACKAGE_VERSION} are incompatible.")
            endif ()
            unset(LLVM_PACKAGE_VERSION)

            # LLD cannot be considered "found" if shared LLVM was requested and LLVM_LINK_LLVM_DYLIB is not set.
            # LLVM has a mis-feature that allows it to build and export both static and shared libraries at the
            # same time, while inconsistently linking its own static libraries (for lldWasm and others) to the
            # shared library. Ignoring this causes Halide to link to both the static AND the shared LLVM libs
            # and it breaks at runtime. From issue: https://github.com/halide/Halide/issues/5471
            if (LLD_FOUND)
                if (Halide_SHARED_LLVM AND NOT LLVM_LINK_LLVM_DYLIB)
                    set(LLD_FOUND 0)
                    list(APPEND errors
                         "LLD was linked to static LLVM (see: LLVM_LINK_LLVM_DYLIB), but shared LLVM was requested (see: Halide_SHARED_LLVM).")
                elseif (NOT Halide_SHARED_LLVM AND LLVM_LINK_LLVM_DYLIB)
                    set(LLD_FOUND 0)
                    list(APPEND errors
                         "LLD was linked to shared LLVM (see: LLVM_LINK_LLVM_DYLIB), but static LLVM was requested (see: Halide_SHARED_LLVM).")
                endif ()
                if (NOT TARGET lldWasm)
                    set(LLD_FOUND 0)
                    list(APPEND errors "LLD was found but did not contain lldWasm.")
                endif ()
            endif ()

            # Set the component-specific FOUND-variable
            set(HalideLLVM_LLD_FOUND "${LLD_FOUND}")
            if (HalideLLVM_LLD_FOUND)
                list(APPEND HalideLLVM_CONFIG "${LLD_CONFIG}")
            endif ()
        endif ()

        # Tentatively mark all requested targets as found, if they are in LLVM_TARGETS_TO_BUILD
        foreach (target IN LISTS requested_targets)
            if (target IN_LIST LLVM_TARGETS_TO_BUILD)
                set(HalideLLVM_${target}_FOUND 1)
            endif ()
        endforeach ()

        # Turn off WebAssembly if LLD was not found
        if (WebAssembly IN_LIST "${CMAKE_FIND_PACKAGE_NAME}_FIND_COMPONENTS" AND
            HalideLLVM_WebAssembly_FOUND AND NOT HalideLLVM_LLD_FOUND)
            set(HalideLLVM_WebAssembly_FOUND 0)
            list(APPEND errors "WebAssembly backend was found, but suitable LLD was not.")
        endif ()
    endif ()

    # Check for consistency and report errors
    list(JOIN errors "\n" error_msg)

    if (CMAKE_VERSION VERSION_LESS 3.18)
        set(fphsa_required_vars REQUIRED_VARS HalideLLVM_CONFIG)
    else ()
        set(fphsa_required_vars "")
    endif ()

    find_package_handle_standard_args(
        HalideLLVM
        HANDLE_COMPONENTS
        ${fphsa_required_vars}
        VERSION_VAR llvm_found_version
        REASON_FAILURE_MESSAGE "\n${error_msg}"
    )

    ##
    # Create Halide::LLVM target if find_package_handle_standard_args succeeded
    ##

    set(HalideLLVM_TARGETS "")
    foreach (target IN LISTS requested_targets)
        if (HalideLLVM_${target}_FOUND)
            list(APPEND HalideLLVM_TARGETS "${target}")
        endif ()
    endforeach ()

    if (HalideLLVM_WebAssembly_FOUND)
        set(wasm_libs lldWasm)
    else ()
        set(wasm_libs "")
    endif ()

    if (HalideLLVM_FOUND AND NOT TARGET Halide::LLVM)
        add_library(Halide::LLVM INTERFACE IMPORTED)
        target_include_directories(Halide::LLVM INTERFACE "${LLVM_INCLUDE_DIRS}")
        target_compile_definitions(
            Halide::LLVM
            INTERFACE
            ${LLVM_DEFINITIONS}
            "LLVM_VERSION=${LLVM_VERSION_MAJOR}${LLVM_VERSION_MINOR}"
        )
        foreach (target IN LISTS HalideLLVM_TARGETS)
            string(TOUPPER "WITH_${target}" target_def)
            target_compile_definitions(Halide::LLVM INTERFACE "${target_def}")
        endforeach ()

        if (Halide_SHARED_LLVM)
            target_link_libraries(Halide::LLVM INTERFACE LLVM ${wasm_libs} ${CMAKE_DL_LIBS})
        else ()
            llvm_map_components_to_libnames(libnames mcjit bitwriter linker passes ${HalideLLVM_TARGETS})
            if (Halide_BUNDLE_LLVM)
                include(BundleStatic)
                bundle_static(Halide::LLVM LIBRARIES ${libnames} ${wasm_libs})
            else ()
                target_link_libraries(Halide::LLVM INTERFACE ${libnames} ${wasm_libs})
            endif ()
        endif ()
    endif ()

    if (HalideLLVM_FOUND AND NOT TARGET Halide::llvm-as)
        add_executable(Halide::llvm-as ALIAS llvm-as)
    endif ()

    if (HalideLLVM_FOUND AND HalideLLVM_Clang_FOUND AND NOT TARGET Halide::clang)
        add_executable(Halide::clang ALIAS clang)
    endif ()

    ##
    # Export whitelisted variables
    ##

    set(HalideLLVM_FOUND "${HalideLLVM_FOUND}" PARENT_SCOPE)
    set(HALIDELLVM_FOUND "${HALIDELLVM_FOUND}" PARENT_SCOPE)
    foreach (comp IN_LIST "${CMAKE_FIND_PACKAGE_NAME}_FIND_COMPONENTS")
        set(HalideLLVM_${comp}_FOUND "${HalideLLVM_${comp}_FOUND}" PARENT_SCOPE)
    endforeach ()

    set(HalideLLVM_VERSION "${llvm_found_version}" PARENT_SCOPE)
    set(HalideLLVM_TARGETS "${HalideLLVM_TARGETS}" PARENT_SCOPE)

    set(LLVM_LIBCXX "${LLVM_LIBCXX}" PARENT_SCOPE)
    set(LLVM_ENABLE_RTTI "${LLVM_ENABLE_RTTI}" PARENT_SCOPE)
endfunction()

_FindHalideLLVM()

# Delete the function to prevent it from being improperly called outside this
# module. After the first redefinition, the original function is still present
# as __HalideLLVM_impl. The second redefinition overwrites __HalideLLVM_impl
# so that calling either one is a no-op.
function(_FindHalideLLVM)
endfunction()

function(_FindHalideLLVM)
endfunction()
