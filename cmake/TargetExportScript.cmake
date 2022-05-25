# Note: in CMake 3.18+ there is a CheckLinkerFlags module that should be used to replace this.
# Sadly, CMake does not attempt to detect the underlying linker and people can try to use, eg.
# gold or lld via CMAKE_CXX_FLAGS.
include(CheckCXXSourceCompiles)

function(target_export_script TARGET)
    set(options)
    set(oneValueArgs LINK_EXE APPLE_LD GNU_LD)
    set(multiValueArgs)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    get_property(target_type TARGET ${TARGET} PROPERTY TYPE)
    if (NOT target_type STREQUAL "SHARED_LIBRARY" AND NOT target_type STREQUAL "MODULE_LIBRARY")
        # Linker scripts do nothing on non-shared libraries.
        return()
    endif ()

    if (MSVC)
        ## TODO: implement something similar for Windows/link.exe
        # https://github.com/halide/Halide/issues/4651
        return()
    endif ()

    set(dummy_source [[ int main() { return 0; } ]])

    # CMake doesn't recognize MSVC/ldd link.exe's unknown-option warnings
    set(extra_errors FAIL_REGEX "LNK4044: unrecognized option|warning : ignoring unknown argument")

    ## More linkers support the GNU syntax (ld, lld, gold), so try it first.
    set(version_script "LINKER:--version-script=${ARG_GNU_LD}")

    set(CMAKE_REQUIRED_LINK_OPTIONS "${version_script}")
    check_cxx_source_compiles("${dummy_source}" LINKER_HAS_FLAG_VERSION_SCRIPT ${extra_errors})

    if (LINKER_HAS_FLAG_VERSION_SCRIPT)
        target_link_options(${TARGET} PRIVATE "${version_script}")
        set_property(TARGET ${TARGET} APPEND PROPERTY LINK_DEPENDS "${ARG_GNU_LD}")
        return()
    endif ()

    ## The Apple linker expects a different flag.
    set(exported_symbols_list "LINKER:-exported_symbols_list,${ARG_APPLE_LD}")

    set(CMAKE_REQUIRED_LINK_OPTIONS "${exported_symbols_list}")
    check_cxx_source_compiles("${dummy_source}" LINKER_HAS_FLAG_EXPORTED_SYMBOLS_LIST ${extra_errors})

    if (LINKER_HAS_FLAG_EXPORTED_SYMBOLS_LIST)
        target_link_options(${TARGET} PRIVATE "${exported_symbols_list}")
        set_property(TARGET ${TARGET} APPEND PROPERTY LINK_DEPENDS "${ARG_APPLE_LD}")
        return()
    endif ()

    message(WARNING "Unknown linker! Could not attach Halide linker script.")
endfunction()
