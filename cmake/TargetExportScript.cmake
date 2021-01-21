# Note: in CMake 3.18+ there is a CheckLinkerFlags module that should be used to replace this.
# Sadly, CMake does not attempt to detect the underlying linker and people can try to use, eg.
# gold or lld via CMAKE_CXX_FLAGS.
include(CheckCXXSourceCompiles)

# TODO: implement something similar for Windows/link.exe
# https://github.com/halide/Halide/issues/4651
function(target_export_script TARGET)
    set(options)
    set(oneValueArgs LINK_EXE APPLE_LD GNU_LD)
    set(multiValueArgs)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    set(dummy_source [[ int main() { return 0; } ]])
    set(extra_errors FAIL_REGEX "LNK4044: unrecognized option")

    set(version_script "LINKER:--version-script=${ARG_GNU_LD}")
    set(exported_symbols_list "LINKER:-exported_symbols_list,${ARG_APPLE_LD}")

    set(CMAKE_REQUIRED_LINK_OPTIONS "${version_script}")
    check_cxx_source_compiles("${dummy_source}" LINKER_HAS_FLAG_VERSION_SCRIPT ${extra_errors})

    set(CMAKE_REQUIRED_LINK_OPTIONS "${exported_symbols_list}")
    check_cxx_source_compiles("${dummy_source}" LINKER_HAS_FLAG_EXPORTED_SYMBOLS_LIST ${extra_errors})

    if (LINKER_HAS_FLAG_VERSION_SCRIPT)
        target_link_options(${TARGET} PRIVATE "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${version_script}>")
    elseif (LINKER_HAS_FLAG_EXPORTED_SYMBOLS_LIST)
        target_link_options(${TARGET} PRIVATE "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${exported_symbols_list}>")
    endif ()
endfunction()
