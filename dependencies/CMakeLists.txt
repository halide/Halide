##
# Standard CMake find-module dependencies
##

find_package(OpenMP)
if (TARGET OpenMP::OpenMP_CXX)
    set_target_properties(OpenMP::OpenMP_CXX PROPERTIES IMPORTED_GLOBAL TRUE)
endif ()

##
# Third-party dependencies in their own subdirectories
##

add_subdirectory(llvm)

add_subdirectory(jpeg)
add_subdirectory(png)

if (TARGET_SPIRV)
    add_subdirectory(spirv)
endif()

# Needs cache vars set by llvm, do not reorder.
add_subdirectory(wasm)
