##
# Utilities for setting up Sanitizer settings
##

option(Halide_ENABLE_ASAN "Build everything with Address Sanitizer enabled." OFF)
if (Halide_ENABLE_ASAN)
    message(STATUS "Enabling Address Sanitizer for project ${PROJECT_NAME}.")
    set(SANITIZER_COMPILE_OPTIONS -fsanitize=address)
    set(SANITIZER_LINK_OPTIONS -fsanitize=address $<$<CXX_COMPILER_ID:GNU>:-static-libasan>)
    # If running under ASAN, we need to suppress some errors:
    # - detect_leaks, because circular Expr chains in Halide can indeed leak,
    #   but we don't care here
    # - detect_container_overflow, because this is a known false-positive
    #   if compiling with a non-ASAN build of LLVM (which is usually the case)
    set(SANITIZER_ENV_VARS "ASAN_OPTIONS=detect_leaks=0:detect_container_overflow=0")
    # -fPIC required for some ASAN build configurations (generally .so), so just enable it unversally here
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
else()
    set(SANITIZER_COMPILE_OPTIONS )
    set(SANITIZER_LINK_OPTIONS )
    set(SANITIZER_ENV_VARS )
endif()

