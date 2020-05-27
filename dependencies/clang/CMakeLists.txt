find_package(Clang ${HALIDE_REQUIRE_LLVM_VERSION} QUIET)

if (NOT TARGET clang)
    if (DEFINED LLVM_DIR)
        message(STATUS "find_package(Clang) failed. Trying to infer CLANG_DIR from LLVM_DIR...")
        set(Clang_DIR "${LLVM_DIR}/../clang")
        find_package(Clang ${HALIDE_REQUIRE_LLVM_VERSION} REQUIRED)
    else ()
        message(FATAL_ERROR "Could not find Clang!")
    endif ()
endif ()

set_target_properties(clang PROPERTIES IMPORTED_GLOBAL TRUE)

# clang-tools-extra is optional, but provides the clang-format target
if (TARGET clang-format)
    set_target_properties(clang-format PROPERTIES IMPORTED_GLOBAL TRUE)
endif ()
