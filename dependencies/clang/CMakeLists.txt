if (DEFINED LLVM_DIR AND NOT DEFINED Clang_DIR)
    set(Clang_DIR "${LLVM_DIR}/../clang")
    find_package(Clang ${HALIDE_REQUIRE_LLVM_VERSION} QUIET CONFIG)
endif ()

if (NOT TARGET clang)
    if (DEFINED LLVM_DIR AND NOT DEFINED Clang_DIR)
        message(STATUS "Couldn't find Clang from LLVM_DIR. Trying system-wide search.")
    endif ()
    find_package(Clang ${HALIDE_REQUIRE_LLVM_VERSION} REQUIRED)
endif ()

set_target_properties(clang PROPERTIES IMPORTED_GLOBAL TRUE)

# clang-tools-extra is optional, but provides the clang-format target
if (TARGET clang-format)
    set_target_properties(clang-format PROPERTIES IMPORTED_GLOBAL TRUE)
endif ()
