# FindHalide.cmake
# ... shamelessly based on FindJeMalloc.cmake

find_path(HALIDE_ROOT_DIR
    NAMES include/Halide.h include/HalideRuntime.h
)

find_library(HALIDE_LIBRARIES
    NAMES Halide
    HINTS ${HALIDE_ROOT_DIR}/lib
)

find_path(HALIDE_INCLUDE_DIR
    NAMES Halide.h HalideRuntime.h
    HINTS ${HALIDE_ROOT_DIR}/include
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Halide DEFAULT_MSG
    HALIDE_LIBRARIES
    HALIDE_INCLUDE_DIR
)

set(HALIDE_LIBRARY HALIDE_LIBRARIES)
set(HALIDE_INCLUDE_DIRS HALIDE_INCLUDE_DIR)

mark_as_advanced(
    HALIDE_ROOT_DIR
    HALIDE_LIBRARY
    HALIDE_LIBRARIES
    HALIDE_INCLUDE_DIR
    HALIDE_INCLUDE_DIRS
)