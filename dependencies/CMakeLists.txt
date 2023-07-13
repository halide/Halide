##
# Third-party dependencies in their own subdirectories
##

add_subdirectory(llvm)

if (TARGET_SPIRV)
    add_subdirectory(spirv)
endif()

# Needs cache vars set by llvm, do not reorder.
add_subdirectory(wasm)
