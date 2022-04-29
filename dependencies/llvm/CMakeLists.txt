##
# Find LLVM and check the version.
##

include(CMakeDependentOption)
include(BundleStatic)

# Fallback configurations for weirdly built LLVMs
set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL MinSizeRel Release RelWithDebInfo "")
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO RelWithDebInfo Release MinSizeRel "")
set(CMAKE_MAP_IMPORTED_CONFIG_RELEASE Release MinSizeRel RelWithDebInfo "")

find_package(LLVM ${Halide_REQUIRE_LLVM_VERSION} REQUIRED)
find_package(Clang REQUIRED CONFIG HINTS "${LLVM_DIR}/../clang" "${LLVM_DIR}/../lib/cmake/clang")

set(LLVM_PACKAGE_VERSION "${LLVM_PACKAGE_VERSION}"
    CACHE INTERNAL "LLVM version")

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
message(STATUS "Using ClangConfig.cmake in: ${Clang_DIR}")

if (LLVM_PACKAGE_VERSION VERSION_LESS 13.0)
    message(FATAL_ERROR "LLVM version must be 13.0 or newer")
endif ()

if (LLVM_PACKAGE_VERSION VERSION_GREATER 15.0)
    message(WARNING "Halide is not tested on LLVM versions beyond 15.0")
endif ()

set(Halide_LLVM_DEFS ${LLVM_DEFINITIONS} $<BUILD_INTERFACE:LLVM_VERSION=${LLVM_VERSION_MAJOR}${LLVM_VERSION_MINOR}>)

##
# Options for which version of LLVM to use
##

option(Halide_SHARED_LLVM "Link against shared LLVM (ignores components)." OFF)
cmake_dependent_option(Halide_BUNDLE_LLVM "When built as a static library, include LLVM's objects." OFF
                       "NOT BUILD_SHARED_LIBS" OFF)

##
# Promote LLVM/Clang executable targets
##

set_target_properties(llvm-as clang PROPERTIES IMPORTED_GLOBAL TRUE)

# clang-tools-extra is optional, but provides the clang-format target
if (TARGET clang-format)
    set_target_properties(clang-format PROPERTIES IMPORTED_GLOBAL TRUE)
endif ()

##
# Create options for including or excluding LLVM backends.
##

set(active_components mcjit bitwriter linker passes)
set(known_components AArch64 AMDGPU ARM Hexagon Mips NVPTX PowerPC RISCV WebAssembly X86)

foreach (comp IN LISTS known_components)
    string(TOUPPER "TARGET_${comp}" OPTION)
    string(TOUPPER "WITH_${comp}" DEFINE)

    cmake_dependent_option(${OPTION} "Include ${comp} target" ON
                           "${comp} IN_LIST LLVM_TARGETS_TO_BUILD" OFF)
    if (${OPTION} OR Halide_SHARED_LLVM)
        message(STATUS "Enabling ${comp} backend")
        list(APPEND Halide_LLVM_DEFS $<BUILD_INTERFACE:${DEFINE}>)
        list(APPEND active_components ${comp})
    else ()
        message(STATUS "Disabling ${comp} backend")
    endif ()
endforeach ()

set(wasm_libs "")
if (TARGET_WEBASSEMBLY)
    find_package(LLD CONFIG REQUIRED HINTS "${LLVM_DIR}/../lld" "${LLVM_DIR}/../lib/cmake/lld")
    message(STATUS "Using LLDConfig.cmake in: ${LLD_DIR}")

    # LLVM has a mis-feature that allows it to build and export both static and shared libraries at the same
    # time, while inconsistently linking its own static libraries (for lldWasm and others) to the shared library.
    # Ignoring this causes Halide to link to both the static AND the shared LLVM libs and it breaks at runtime.
    # From issue: https://github.com/halide/Halide/issues/5471
    if (LLVM_LINK_LLVM_DYLIB AND NOT Halide_SHARED_LLVM)
        message(FATAL_ERROR "LLD was linked to shared LLVM (see: LLVM_LINK_LLVM_DYLIB), "
                "but static LLVM was requested. Re-configure with Halide_SHARED_LLVM=YES "
                "to enable WebAssembly, or disable WebAssembly with TARGET_WEBASSEMBLY=OFF.")
    endif ()

    set(wasm_libs lldWasm)
endif ()

##
# Create Halide::LLVM library alias pointing to the correct LLVM
# among shared, static, and bundled.
##

add_library(Halide_LLVM INTERFACE)
add_library(Halide::LLVM ALIAS Halide_LLVM)

set_target_properties(Halide_LLVM PROPERTIES EXPORT_NAME LLVM)
target_include_directories(Halide_LLVM INTERFACE "$<BUILD_INTERFACE:${LLVM_INCLUDE_DIRS}>")
target_compile_definitions(Halide_LLVM INTERFACE ${Halide_LLVM_DEFS})

# Link LLVM libraries to Halide_LLVM, depending on shared, static, or bundled selection.
if (Halide_SHARED_LLVM)
    # llvm_map_components_to_libnames is not safe to call if the LLVM static libraries
    # aren't in the package. This happens on Gentoo Linux at least, but might also happen
    # with custom LLVM build configurations.
    target_link_libraries(Halide_LLVM INTERFACE LLVM ${wasm_libs} ${CMAKE_DL_LIBS})
else ()
    llvm_map_components_to_libnames(llvm_targets ${active_components})
    list(APPEND llvm_targets ${wasm_libs})
    if (Halide_BUNDLE_LLVM)
        bundle_static(Halide_LLVM LIBRARIES ${llvm_targets})
    else ()
        target_link_libraries(Halide_LLVM INTERFACE ${llvm_targets})
    endif ()
endif ()

##
# Language options interface library
##

add_library(Halide_LanguageOptions INTERFACE)
add_library(Halide::LanguageOptions ALIAS Halide_LanguageOptions)

set_target_properties(Halide_LanguageOptions PROPERTIES EXPORT_NAME LanguageOptions)

option(Halide_ENABLE_RTTI "Enable RTTI" ${LLVM_ENABLE_RTTI})
if (Halide_ENABLE_RTTI AND NOT LLVM_ENABLE_RTTI)
    message(FATAL_ERROR "Can't enable RTTI. LLVM was compiled without it")
endif ()

if (Halide_ENABLE_RTTI)
    message(STATUS "Compiling Halide WITH RTTI.")
    target_compile_definitions(Halide_LanguageOptions INTERFACE HALIDE_ENABLE_RTTI)
else ()
    message(STATUS "Compiling Halide WITHOUT RTTI.")
    target_compile_options(Halide_LanguageOptions INTERFACE
                           $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/GR->
                           $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-fno-rtti>)
endif ()

option(Halide_ENABLE_EXCEPTIONS "Enable exceptions" ON)
if (Halide_ENABLE_EXCEPTIONS)
    message(STATUS "Compiling Halide WITH exceptions.")
    target_compile_definitions(Halide_LanguageOptions INTERFACE HALIDE_WITH_EXCEPTIONS)
else ()
    message(STATUS "Compiling Halide WITHOUT exceptions.")
    target_compile_options(Halide_LanguageOptions INTERFACE
                           $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/EHs-c->
                           $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-fno-exceptions>)
    target_compile_definitions(Halide_LanguageOptions INTERFACE
                               $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:_HAS_EXCEPTIONS=0>)
endif ()

if (LLVM_LIBCXX GREATER -1)
    message(STATUS "LLVM linked to libc++. Adding to interface requirements.")
    target_compile_options(Halide_LanguageOptions INTERFACE
                           $<$<STREQUAL:$<TARGET_PROPERTY:LINKER_LANGUAGE>,CXX>:-stdlib=libc++>)
    target_link_options(Halide_LanguageOptions INTERFACE
                        $<$<STREQUAL:$<TARGET_PROPERTY:LINKER_LANGUAGE>,CXX>:-stdlib=libc++>)
endif ()
