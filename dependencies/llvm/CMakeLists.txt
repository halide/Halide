##
# Find LLVM and check the version.
##

include(CMakeDependentOption)

# Fallback configurations for weirdly built LLVMs
set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL MinSizeRel Release RelWithDebInfo "")
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO RelWithDebInfo Release MinSizeRel "")
set(CMAKE_MAP_IMPORTED_CONFIG_RELEASE Release MinSizeRel RelWithDebInfo "")

find_package(LLVM ${HALIDE_REQUIRE_LLVM_VERSION} REQUIRED)
find_package(Clang REQUIRED CONFIG HINTS "${LLVM_DIR}/../clang")

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

if (LLVM_PACKAGE_VERSION VERSION_LESS 9.0)
    message(FATAL_ERROR "LLVM version must be 9.0 or newer")
endif ()

if (LLVM_PACKAGE_VERSION VERSION_GREATER 12.0)
    message(WARNING "Halide is not tested on LLVM versions beyond 12.0")
endif ()

set(Halide_LLVM_DEFS ${LLVM_DEFINITIONS} $<BUILD_INTERFACE:LLVM_VERSION=${LLVM_VERSION_MAJOR}${LLVM_VERSION_MINOR}>)

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

set(LLVM_COMPONENTS mcjit bitwriter linker passes)
set(known_components AArch64 AMDGPU ARM Hexagon Mips NVPTX PowerPC RISCV WebAssembly X86)

# We don't support LLVM10 or below for wasm codegen.
if (LLVM_PACKAGE_VERSION VERSION_LESS 11.0)
    list(REMOVE_ITEM known_components WebAssembly)
endif ()

foreach (comp IN LISTS known_components)
    string(TOUPPER "TARGET_${comp}" OPTION)
    string(TOUPPER "WITH_${comp}" DEFINE)

    cmake_dependent_option(${OPTION} "Include ${comp} target" ON
                           "${comp} IN_LIST LLVM_TARGETS_TO_BUILD" OFF)
    if (${OPTION})
        message(STATUS "Enabling ${comp} backend")
        list(APPEND Halide_LLVM_DEFS $<BUILD_INTERFACE:${DEFINE}>)
        list(APPEND LLVM_COMPONENTS ${comp})
    else ()
        message(STATUS "Disabling ${comp} backend")
    endif ()
endforeach ()

##
# Create Halide::LLVM library alias pointing to the correct LLVM
# among shared, static, and bundled.
##

option(Halide_BUNDLE_LLVM "When built as a static library, include LLVM's objects." OFF)
option(Halide_SHARED_LLVM "Link against shared LLVM (disables components)." OFF)

llvm_map_components_to_libnames(LLVM_LIBNAMES ${LLVM_COMPONENTS})
if (TARGET_WEBASSEMBLY)
    find_package(LLD CONFIG REQUIRED HINTS "${LLVM_DIR}/../lld")
    list(APPEND LLVM_LIBNAMES lldWasm)
endif ()

if (Halide_BUNDLE_LLVM AND NOT BUILD_SHARED_LIBS)
    include(BundleStatic)
    bundle_static(LIBRARIES ${LLVM_LIBNAMES} TARGET Halide_LLVM)
else ()
    add_library(Halide_LLVM INTERFACE)
    if (Halide_SHARED_LLVM)
        set(LLVM_LIBNAMES LLVM)
        target_link_libraries(Halide_LLVM INTERFACE ${CMAKE_DL_LIBS})
    endif ()
    target_link_libraries(Halide_LLVM INTERFACE ${LLVM_LIBNAMES})
    set_target_properties(${LLVM_LIBNAMES} PROPERTIES IMPORTED_GLOBAL TRUE)
endif ()

# Attach the include dirs and (patched) definitions to the target, where they belong.
set_target_properties(Halide_LLVM PROPERTIES EXPORT_NAME LLVM)
target_include_directories(Halide_LLVM INTERFACE $<BUILD_INTERFACE:${LLVM_INCLUDE_DIRS}>)
target_compile_definitions(Halide_LLVM INTERFACE ${Halide_LLVM_DEFS})

add_library(Halide::LLVM ALIAS Halide_LLVM)

##
# Language options interface library
##

add_library(Halide_LanguageOptions INTERFACE)
add_library(Halide::LanguageOptions ALIAS Halide_LanguageOptions)

set_target_properties(Halide_LanguageOptions PROPERTIES EXPORT_NAME LanguageOptions)

option(HALIDE_ENABLE_RTTI "Enable RTTI" ${LLVM_ENABLE_RTTI})
if (HALIDE_ENABLE_RTTI AND NOT LLVM_ENABLE_RTTI)
    message(FATAL_ERROR "Can't enable RTTI. LLVM was compiled without it")
endif ()

if (HALIDE_ENABLE_RTTI)
    message(STATUS "Compiling Halide WITH RTTI.")
    target_compile_definitions(Halide_LanguageOptions INTERFACE HALIDE_ENABLE_RTTI)
else ()
    message(STATUS "Compiling Halide WITHOUT RTTI.")
    target_compile_options(Halide_LanguageOptions INTERFACE
                           $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/GR->
                           $<$<COMPILE_LANG_AND_ID:CXX,GCC,Clang,AppleClang>:-fno-rtti>)
endif ()

option(HALIDE_ENABLE_EXCEPTIONS "Enable exceptions" YES)
if (HALIDE_ENABLE_EXCEPTIONS)
    message(STATUS "Compiling Halide WITH exceptions.")
    target_compile_definitions(Halide_LanguageOptions INTERFACE HALIDE_WITH_EXCEPTIONS)
else ()
    message(STATUS "Compiling Halide WITHOUT exceptions.")
    target_compile_options(Halide_LanguageOptions INTERFACE
                           $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/EHs-c->
                           $<$<COMPILE_LANG_AND_ID:CXX,GCC,Clang,AppleClang>:-fno-exceptions>)
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
