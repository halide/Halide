cmake_minimum_required(VERSION 3.22)

include(CheckLinkerFlag)

function(target_export_script TARGET)
    set(options)
    set(oneValueArgs LINK_EXE APPLE_LD GNU_LD)
    set(multiValueArgs)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    get_property(target_type TARGET ${TARGET} PROPERTY TYPE)
    if (NOT target_type MATCHES "(SHARED|MODULE)_LIBRARY")
        # Linker scripts do nothing on non-shared libraries.
        return()
    endif ()

    if (MSVC)
        ## TODO: implement something similar for Windows/link.exe
        # https://github.com/halide/Halide/issues/4651
        return()
    endif ()

    ## More linkers support the GNU syntax (ld, lld, gold), so try it first.
    set(VERSION_SCRIPT_FLAG "LINKER:--version-script=${ARG_GNU_LD}")
    check_linker_flag(CXX "${VERSION_SCRIPT_FLAG}" LINKER_HAS_FLAG_VERSION_SCRIPT)
    if (LINKER_HAS_FLAG_VERSION_SCRIPT)
        target_link_options(${TARGET} PRIVATE "${VERSION_SCRIPT_FLAG}")
        set_property(TARGET ${TARGET} APPEND PROPERTY LINK_DEPENDS "${ARG_GNU_LD}")
        return()
    endif ()

    ## The Apple linker expects a different flag.
    set(EXPORTED_SYMBOLS_FLAG "LINKER:-exported_symbols_list,${ARG_APPLE_LD}")
    check_linker_flag(CXX "${EXPORTED_SYMBOLS_FLAG}" LINKER_HAS_FLAG_EXPORTED_SYMBOLS_LIST)
    if (LINKER_HAS_FLAG_EXPORTED_SYMBOLS_LIST)
        target_link_options(${TARGET} PRIVATE "${EXPORTED_SYMBOLS_FLAG}")
        set_property(TARGET ${TARGET} APPEND PROPERTY LINK_DEPENDS "${ARG_APPLE_LD}")
        return()
    endif ()

    message(WARNING "Unknown linker! Could not attach Halide linker script.")
endfunction()
